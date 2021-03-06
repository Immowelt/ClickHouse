#include <Interpreters/Context.h>
#include <Interpreters/InterpreterCheckQuery.h>
#include <Storages/IStorage.h>
#include <Parsers/ASTCheckQuery.h>
#include <DataStreams/OneBlockInputStream.h>
#include <DataTypes/DataTypesNumber.h>
#include <DataTypes/DataTypeString.h>
#include <Columns/ColumnsNumber.h>
#include <Common/typeid_cast.h>


namespace DB
{

namespace
{

NamesAndTypes getBlockStructure()
{
    return {
        {"part_path", std::make_shared<DataTypeString>()},
        {"is_passed", std::make_shared<DataTypeUInt8>()},
        {"message", std::make_shared<DataTypeString>()},
    };
}

}


InterpreterCheckQuery::InterpreterCheckQuery(const ASTPtr & query_ptr_, const Context & context_)
    : query_ptr(query_ptr_), context(context_)
{
}


BlockIO InterpreterCheckQuery::execute()
{
    const auto & check = query_ptr->as<ASTCheckQuery &>();
    const String & table_name = check.table;
    String database_name = check.database.empty() ? context.getCurrentDatabase() : check.database;

    StoragePtr table = context.getTable(database_name, table_name);
    auto check_results = table->checkData(query_ptr, context);

    auto block_structure = getBlockStructure();
    auto path_column = block_structure[0].type->createColumn();
    auto is_passed_column = block_structure[1].type->createColumn();
    auto message_column = block_structure[2].type->createColumn();

    for (const auto & check_result : check_results)
    {
        path_column->insert(check_result.fs_path);
        is_passed_column->insert(static_cast<UInt8>(check_result.success));
        message_column->insert(check_result.failure_message);
    }

    Block block({
        {std::move(path_column), block_structure[0].type, block_structure[0].name},
        {std::move(is_passed_column), block_structure[1].type, block_structure[1].name},
        {std::move(message_column), block_structure[2].type, block_structure[2].name}});

    BlockIO res;
    res.in = std::make_shared<OneBlockInputStream>(block);

    return res;
}

}
