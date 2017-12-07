#include "GdprInterpreter.h"

#include <Interpreters/ExpressionAnalyzer.h>
#include <Interpreters/ExpressionActions.h>
#include <Interpreters/InterpreterSelectQuery.h>
#include <Parsers/ExpressionListParsers.h>
#include <Parsers/ParserQuery.h>
#include <Parsers/parseQuery.h>
#include <Parsers/IAST.h>
#include <Storages/MergeTree/MergeTreeThreadBlockInputStream.h>
#include <Storages/MergeTree/MergeTreeDataWriter.h>
#include <Storages/StorageMergeTree.h>

#include <Poco/File.h>

namespace DB
{

GdprInterpreter::GdprInterpreter(
        String table_,
        String where_,
        String column_,
        String value_,
        Context & context_):
                table(table_),
                where(where_),
                column(column_),
                value(value_),
                context(context_)
{
}

GdprInterpreter::~GdprInterpreter()
{
}


void pr(BlockInputStreamPtr p, String t, int k)
{
    std::cout << t << p->getID() << "\n";
    MergeTreeBaseBlockInputStream* ptr = dynamic_cast<MergeTreeBaseBlockInputStream*>(p.get());

    if (ptr)
    {
        for(auto dpart : ptr->getDataParts())
        {
            std::cout << t << "..." << dpart->getFullPath() << "\t";
            if (k == 1)
            {
                Block b = ptr->read();
                std::cout << b.dumpNames() << "\t" << b.rows() << "x" << b.columns();

            }
            std::cout << "\n";
        }
    }

    std::cout << "\n";

    int i = 0;
    for(const auto & child : p->getChildren())
    {
        pr(child, t + "\t", i++);
    }
}

BlockIO GdprInterpreter::execute()
{
    Poco::File("/var/lib/clickhouse//data/default/daten/tmp_insert_20180617_20180617_3_3_0/manu/").createDirectories();

    String dummy_select = "select * from " + table + " where " + where;

    ParserQuery parser(dummy_select.data());
    ASTPtr ast = parseQuery(parser, dummy_select.data(), dummy_select.data() + dummy_select.size(), "GDPR dummy select query");

    //ast->dumpTree(std::cout);

    auto isq = InterpreterSelectQuery(ast, context, QueryProcessingStage::FetchColumns);
    auto block = isq.execute();
    block.in->dumpTree(std::cout);

    //pr(block.in, "", 0);

    //writing
    auto storage = context.getTable(context.getCurrentDatabase(), table);
    auto merge_tree = dynamic_cast<StorageMergeTree *>(&*storage);
    MergeTreeData & data = merge_tree->getData();
    MergeTreeDataWriter writer(data);

    for(const auto & child : block.in->getChildren())
    {
        MergeTreeBaseBlockInputStream* ptr = dynamic_cast<MergeTreeBaseBlockInputStream*>(child.get());
        Block b = ptr->read();
        if (b.rows() > 0)
        {
            for(auto & bwp : writer.splitBlockIntoParts(b))
            {
                auto newpart = writer.writeTempPart(bwp);
                String s = newpart->getFullPath() + "manu/";
                std::cout << ":::" << s << ":::\n" ;
                //Poco::File(s).createDirectories();
                Poco::File("/var/lib/clickhouse//data/default/daten/tmp_insert_20180617_20180617_3_3_0/manu/").createDirectories();
                std::cout << ">>> " << newpart->getFullPath() << "\n";
            }

        }
    }

    return {};
}

}
