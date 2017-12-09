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
        std::cout << "1. " << stream->getID() << "\n";
        std::cout.flush();
        Block b = stream->read();
        std::cout << "2. " << stream->getID() << "\n";
        std::cout.flush();
        size_t pos = b.getPositionByName(column);
        std::cout << "3. " << pos << "\n";
        std::cout.flush();
        ColumnWithTypeAndName & oldcolumn = b.getByPosition(pos);
        ColumnWithTypeAndName newcolumn = oldcolumn.cloneEmpty();
        ColumnString * col = dynamic_cast<ColumnString *>(oldcolumn.column.get());
        std::cout << "4. " << (col == nullptr ? "null": "notnull") << "\n";
        std::cout.flush();
        const char* newval = newvalue.c_str();
        size_t newvallen = strlen(newval);
        for(size_t row = 0; row < b.rows(); ++row)
        {
            std::cout << "5. \n";
            std::cout.flush();
            StringRef s = col->getDataAtWithTerminatingZero(row);
            if(!strcasecmp(s.data, oldvalue.c_str()))
            {
                std::cout << s.data << " vs " << oldvalue.c_str() << " - replacing\n";
                std::cout.flush();
                newcolumn.column->insertDataWithTerminatingZero(newval, newvallen);
            }
            else
            {
                std::cout << s.data << " vs " << oldvalue.c_str() << " - leaving\n";
                std::cout.flush();
                newcolumn.column->insertFrom(*col, row);
            }
        }

        std::cout << "6. \n";
        b.erase(pos);
        std::cout << "7. \n";

        b.insert(pos, newcolumn);

        auto merge_tree = dynamic_cast<StorageMergeTree *>(storage.get());
        std::cout << "8. \n";

//        MergeTreeBlockOutputStream outstream(*merge_tree);
//        std::cout << "9. \n";
//
//        outstream.write(b);

        MergeTreeData & data = merge_tree->getData();
        MergeTreeDataWriter writer(data);
        std::cout << "9. \n";
        auto part_blocks = writer.splitBlockIntoParts(b);
        std::cout << "10. \n";
        for (auto & current_block : part_blocks)
        {
            std::cout << "11. \n";
            MergeTreeData::MutableDataPartPtr part = writer.writeTempPart(current_block);
            std::cout << "12. " << part->getFullPath() << " \n";
            data.renameTempPartAndAdd(part, merge_tree->getIncrementPtr());
            std::cout << "13. \n";
        }


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
