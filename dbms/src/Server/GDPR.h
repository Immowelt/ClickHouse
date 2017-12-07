#pragma once

#include <Poco/Util/Application.h>
#include <memory>
#include <Interpreters/GdprInterpreter.h>

namespace DB
{

//class Context;

/// Lightweight Application for clickhouse-local
/// No networking, no extra configs and working directories, no pid and status files, no dictionaries, no logging.
/// Quiet mode by default
class GDPR : public Poco::Util::Application
{
public:

	GDPR();

    void initialize(Poco::Util::Application & self) override;

    void defineOptions(Poco::Util::OptionSet& _options) override;

    int main(const std::vector<std::string> & args) override;

    ~GDPR();

private:

    void tryInitPath();
    void applyOptions();
    void attachSystemTables();
    void updateData();
    void setupUsers();
    void displayHelp();
    void handleHelp(const std::string & name, const std::string & value);

protected:

    std::unique_ptr<Context> context;
    std::unique_ptr<GdprInterpreter> interpreter;
};

}
