#include "GdprInterpreter.h"

#include <strings.h>

#include <Interpreters/ExpressionAnalyzer.h>
#include <Interpreters/ExpressionActions.h>
#include <Interpreters/InterpreterSelectQuery.h>
#include <Parsers/ExpressionListParsers.h>
#include <Parsers/ParserQuery.h>
#include <Parsers/parseQuery.h>
#include <Parsers/IAST.h>
#include <Storages/MergeTree/MergeTreeThreadBlockInputStream.h>
#include <Storages/MergeTree/MergeTreeDataWriter.h>
#include <Storages/MergeTree/MergeTreeBlockOutputStream.h>
#include <Storages/StorageMergeTree.h>

#include <Poco/File.h>

namespace DB
{

GdprInterpreter::GdprInterpreter(
        String table_,
        String prewhere_,
        String column_,
        String oldvalue_,
        String newvalue_,
        Context & context_):
                table(table_),
                prewhere(prewhere_),
                column(column_),
                oldvalue(oldvalue_),
                newvalue(newvalue_),
                context(context_)
{
}

GdprInterpreter::~GdprInterpreter()
{
}


void fillStreams(BlockInputStreamPtr parent, MergeTreeStreams & streams)
{
    MergeTreeBaseBlockInputStream* p = dynamic_cast<MergeTreeBaseBlockInputStream*>(parent.get());

    if (p)
    {
//        for(auto part : p->getDataParts())
//        {
//            part->getFullPath()
//        }
        streams.push_back(p);
    }

    for(auto child: parent->getChildren())
    {
        fillStreams(child, streams);
    }
}

BlockIO GdprInterpreter::execute()
{
    String dummy_select = "select * from " + table; // + " prewhere " + prewhere;

    ParserQuery parser(dummy_select.data());
    ASTPtr ast = parseQuery(parser, dummy_select.data(), dummy_select.data() + dummy_select.size(), "GDPR dummy select query");

    //ast->dumpTree(std::cout);

    auto isq = InterpreterSelectQuery(ast, context, QueryProcessingStage::Complete);
    auto block = isq.execute();
    block.in->dumpTree(std::cout);

    //writing
    auto storage = context.getTable(context.getCurrentDatabase(), table);

    MergeTreeStreams streams;
    fillStreams(block.in, streams);

    for(auto stream : streams)
    {
        Block b = stream->read();
        size_t pos = b.getPositionByName(column);
        ColumnWithTypeAndName & oldcolumn = b.getByPosition(pos);
        ColumnWithTypeAndName newcolumn = oldcolumn.cloneEmpty();
        ColumnString * col = dynamic_cast<ColumnString *>(oldcolumn.column.get());
        for(size_t row = 0; row < b.rows(); ++row)
        {
            if(!strcasecmp(col->getDataAtWithTerminatingZero(row).data, oldvalue.c_str()))
            {
                std::cout << col->getDataAtWithTerminatingZero(row).data << " vs " << oldvalue.c_str() << " - replacing\n";
                newcolumn.column->insertDataWithTerminatingZero(newvalue.c_str(), row);
            }
            else
            {
                std::cout << col->getDataAtWithTerminatingZero(row).data << " vs " << oldvalue.c_str() << " - leaving\n";
                newcolumn.column->insertDataWithTerminatingZero(col->getDataAtWithTerminatingZero(row).data, row);
            }
        }

        b.erase(pos);
        b.insert(pos, newcolumn);

        auto merge_tree = dynamic_cast<StorageMergeTree *>(storage.get());
        MergeTreeBlockOutputStream outstream(*merge_tree);
        outstream.write(b);

        //    MergeTreeData & data = merge_tree->getData();
        //    MergeTreeDataWriter writer(data);
//        auto part_blocks = writer.splitBlockIntoParts(b);
//        for (auto & current_block : part_blocks)
//        {
            //MergeTreeData::MutableDataPartPtr part = writer.writeTempPart(current_block);
//            std::cout << part->getFullPath() <<  " has been written\n";
//        }
    }

    return {};
}

}
