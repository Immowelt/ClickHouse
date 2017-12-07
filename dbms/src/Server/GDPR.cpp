#include "GDPR.h"

#include <Poco/Util/XMLConfiguration.h>
#include <Poco/Util/HelpFormatter.h>
#include <Poco/Util/OptionCallback.h>
#include <Poco/String.h>
#include <Databases/DatabaseOrdinary.h>
#include <Storages/System/attachSystemTables.h>
#include <Interpreters/Context.h>
#include <Interpreters/ProcessList.h>
#include <Interpreters/executeQuery.h>
#include <Interpreters/loadMetadata.h>
#include <Common/Exception.h>
#include <Common/Macros.h>
#include <Common/ConfigProcessor.h>
#include <Common/escapeForFileName.h>
#include <IO/ReadBufferFromString.h>
#include <IO/WriteBufferFromString.h>
#include <IO/WriteBufferFromFileDescriptor.h>
#include <Parsers/parseQuery.h>
#include <Parsers/IAST.h>
#include <common/ErrorHandlers.h>
#include <common/ApplicationServerExt.h>
#include "StatusFile.h"
#include <Functions/registerFunctions.h>
#include <AggregateFunctions/registerAggregateFunctions.h>
#include <TableFunctions/registerTableFunctions.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int SYNTAX_ERROR;
    extern const int CANNOT_LOAD_CONFIG;
}


GDPR::GDPR() = default;

GDPR::~GDPR()
{
    if (context)
        context->shutdown(); /// required for properly exception handling
}


void GDPR::initialize(Poco::Util::Application & self)
{
    Poco::Util::Application::initialize(self);
}


void GDPR::defineOptions(Poco::Util::OptionSet& _options)
{
    Poco::Util::Application::defineOptions (_options);

    _options.addOption(
        Poco::Util::Option("config-file", "", "Load configuration from a given file")
            .required(false)
            .repeatable(false)
            .argument("[config.xml]")
            .binding("config-file"));

    _options.addOption(
        Poco::Util::Option("table", "T", "Table to update")
            .required(true)
            .repeatable(false)
            .argument("<table name>")
            .binding("table"));

    _options.addOption(
        Poco::Util::Option("where", "W", "Which records to update")
            .required(true)
            .repeatable(false)
            .argument("<where clause>")
            .binding("where"));

    _options.addOption(
        Poco::Util::Option("column", "C", "Which column to update")
            .required(true)
            .repeatable(false)
            .argument("<column name>")
            .binding("column"));

    _options.addOption(
        Poco::Util::Option("value", "V", "New value to set")
            .required(true)
            .repeatable(false)
            .argument("<new column value>")
            .binding("value"));


    _options.addOption(
        Poco::Util::Option("help", "", "Display help information")
        .required(false)
        .repeatable(false)
        .noArgument()
        .binding("help")
        .callback(Poco::Util::OptionCallback<GDPR>(this, &GDPR::handleHelp)));

    /// These arrays prevent "variable tracking size limit exceeded" compiler notice.
    static const char * settings_names[] = {
#define DECLARE_SETTING(TYPE, NAME, DEFAULT) #NAME,
    APPLY_FOR_SETTINGS(DECLARE_SETTING)
#undef DECLARE_SETTING
    nullptr};

    static const char * limits_names[] = {
#define DECLARE_SETTING(TYPE, NAME, DEFAULT) #NAME,
    APPLY_FOR_LIMITS(DECLARE_SETTING)
#undef DECLARE_SETTING
    nullptr};

    for (const char ** name = settings_names; *name; ++name)
        _options.addOption(Poco::Util::Option(*name, "", "Settings.h").required(false).argument("<value>")
        .repeatable(false).binding(*name));

    for (const char ** name = limits_names; *name; ++name)
        _options.addOption(Poco::Util::Option(*name, "", "Limits.h").required(false).argument("<value>")
        .repeatable(false).binding(*name));
}


void GDPR::applyOptions()
{
    /// settings and limits could be specified in config file, but passed settings has higher priority
#define EXTRACT_SETTING(TYPE, NAME, DEFAULT) \
        if (config().has(#NAME) && !context->getSettingsRef().NAME.changed) \
            context->setSetting(#NAME, config().getString(#NAME));
        APPLY_FOR_SETTINGS(EXTRACT_SETTING)
#undef EXTRACT_SETTING

#define EXTRACT_LIMIT(TYPE, NAME, DEFAULT) \
        if (config().has(#NAME) && !context->getSettingsRef().limits.NAME.changed) \
            context->setSetting(#NAME, config().getString(#NAME));
        APPLY_FOR_LIMITS(EXTRACT_LIMIT)
#undef EXTRACT_LIMIT
}


void GDPR::displayHelp()
{
    Poco::Util::HelpFormatter helpFormatter(options());
    helpFormatter.setCommand(commandName());
    helpFormatter.setUsage("--table=<table name> --where=<where clause> --column=<column name> --value=<new column value>");
    helpFormatter.setHeader("\n"
        "clickhouse-gdpr updates values of some column with a new value.\n"
        "It is intended to be used to satisfy the EU GDPR law requiring\n"
        "being able to remove personal information upon request."
    );
    helpFormatter.setWidth(132); /// 80 is ugly due to wide settings params

    helpFormatter.format(std::cerr);
}


void GDPR::handleHelp(const std::string & name, const std::string & value)
{
    displayHelp();
    stopOptionsProcessing();
}


/// If path is specified and not empty, will try to setup server environment and load existing metadata
void GDPR::tryInitPath()
{
	std::string path;

    if (!config().has("path") || (path = config().getString("path")).empty())
        return;

    Poco::trimInPlace(path);
    if (path.empty())
        return;
    if (path.back() != '/')
        path += '/';

    context->setPath(path);

    StatusFile status{path + "status"};
}


int GDPR::main(const std::vector<std::string> & args)
try
{
    Logger * log = &logger();

    /// Load config files if exists
    if (config().has("config-file") || Poco::File("config.xml").exists())
    {
        ConfigurationPtr processed_config = ConfigProcessor(false, true)
            .loadConfig(config().getString("config-file", "config.xml"))
            .configuration;
        config().add(processed_config.duplicate(), PRIO_DEFAULT, false);
    }

    context = std::make_unique<Context>(Context::createGlobal());
    context->setGlobalContext(*context);
    context->setApplicationType(Context::ApplicationType::LOCAL);
    tryInitPath();

    applyOptions();

    interpreter = std::make_unique<GdprInterpreter>(
            config().getString("table"),
            config().getString("where"),
            config().getString("column"),
            config().getString("value"),
            *context);


    /// We will terminate process on error
    static KillingErrorHandler error_handler;
    Poco::ErrorHandler::set(&error_handler);

    registerFunctions();
    registerAggregateFunctions();
    registerTableFunctions();

    setupUsers();

    /// Load global settings from default profile.
    String default_profile_name = config().getString("default_profile", "default");
    context->setDefaultProfileName(default_profile_name);
    context->setSetting("profile", default_profile_name);

    context->setUser("default", "", Poco::Net::SocketAddress{}, "");

    /// Limit on total number of concurrently executing queries.
    /// Threre are no need for concurrent threads, override max_concurrent_queries.
    context->getProcessList().setMaxSize(0);

    /// Size of cache for uncompressed blocks. Zero means disabled.
    size_t uncompressed_cache_size = config().getUInt64("uncompressed_cache_size", 0);
    if (uncompressed_cache_size)
        context->setUncompressedCache(uncompressed_cache_size);

    /// Size of cache for marks (index of MergeTree family of tables). It is necessary.
    /// Specify default value for mark_cache_size explicitly!
    size_t mark_cache_size = config().getUInt64("mark_cache_size", 5368709120);
    if (mark_cache_size)
        context->setMarkCache(mark_cache_size);


    std::string default_database = config().getString("default_database", "default");

    LOG_INFO(log, "Loading metadata.");
    loadMetadataSystem(*context);
    attachSystemTablesLocal(*context->getDatabase("system"));
    /// Then, load remaining databases
    loadMetadata(*context);
    LOG_DEBUG(log, "Loaded metadata.");

    context->setCurrentDatabase(default_database);

    updateData();

    context->shutdown();
    context.reset();

    return Application::EXIT_OK;
}
catch (const Exception & e)
{
    bool print_stack_trace = config().has("stacktrace");

    std::string text = e.displayText();

    auto embedded_stack_trace_pos = text.find("Stack trace");
    if (std::string::npos != embedded_stack_trace_pos && !print_stack_trace)
        text.resize(embedded_stack_trace_pos);

    std::cerr << "Code: " << e.code() << ". " << text << std::endl << std::endl;

    if (print_stack_trace && std::string::npos == embedded_stack_trace_pos)
    {
        std::cerr << "Stack trace:" << std::endl << e.getStackTrace().toString();
    }

    /// If exception code isn't zero, we should return non-zero return code anyway.
    return e.code() ? e.code() : -1;
}



void GDPR::attachSystemTables()
{
    DatabasePtr system_database = context->tryGetDatabase("system");
    if (!system_database)
    {
        /// TODO: add attachTableDelayed into DatabaseMemory to speedup loading
        system_database = std::make_shared<DatabaseMemory>("system");
        context->addDatabase("system", system_database);
    }

    attachSystemTablesLocal(*system_database);
}


void GDPR::updateData()
{
    Logger * log = &logger();

    LOG_INFO(log, "Updating " << interpreter->table << "." << interpreter->column << " where " << interpreter->where << " with " << interpreter->value);

    interpreter->execute();

//
//    for (const auto & query : queries)
//    {
//        ReadBufferFromString read_buf(query);
//        WriteBufferFromFileDescriptor write_buf(STDOUT_FILENO);
//
//        if (verbose)
//            LOG_INFO(log, "Executing query: " << query);
//
//        executeQuery(read_buf, write_buf, /* allow_into_outfile = */ true, *context, {});
//    }
}

static const char * minimal_default_user_xml =
"<yandex>"
"    <profiles>"
"        <default></default>"
"    </profiles>"
"    <users>"
"        <default>"
"            <password></password>"
"            <networks>"
"                <ip>::/0</ip>"
"            </networks>"
"            <profile>default</profile>"
"            <quota>default</quota>"
"        </default>"
"    </users>"
"    <quotas>"
"        <default></default>"
"    </quotas>"
"</yandex>";


void GDPR::setupUsers()
{
    ConfigurationPtr users_config;

    if (config().has("users_config") || config().has("config-file") || Poco::File("config.xml").exists())
    {
        auto users_config_path = config().getString("users_config", config().getString("config-file", "config.xml"));
        users_config = ConfigProcessor().loadConfig(users_config_path).configuration;
    }
    else
    {
        std::stringstream default_user_stream;
        default_user_stream << minimal_default_user_xml;

        Poco::XML::InputSource default_user_source(default_user_stream);
        users_config = ConfigurationPtr(new Poco::Util::XMLConfiguration(&default_user_source));
    }

    if (users_config)
        context->setUsersConfig(users_config);
    else
        throw Exception("Can't load config for users", ErrorCodes::CANNOT_LOAD_CONFIG);


}

}

YANDEX_APP_MAIN_FUNC(DB::GDPR, mainEntryClickHouseGDPR);
