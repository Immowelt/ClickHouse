#include "GdprInterpreter.h"

#include <strings.h>

#include <Interpreters/ExpressionAnalyzer.h>
#include <Interpreters/ExpressionActions.h>
#include <Interpreters/InterpreterSelectQuery.h>
#include <Parsers/ExpressionListParsers.h>
#include <Parsers/ParserQuery.h>
#include <Parsers/parseQuery.h>
#include <Parsers/IAST.h>
#include <Storages/MergeTree/MergeTreeBlockInputStream.h>
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


void fillStreams(BlockInputStreamPtr parent, MergeTreeStreams & streams, MergeTreeStreamPartMap & parts)
{
    MergeTreeBaseBlockInputStream* p = dynamic_cast<MergeTreeBaseBlockInputStream*>(parent.get());

    if (p)
    {
        for(auto part : p->getDataParts())
        {
            String s = part->getFullPath();
            if (parts.count(s) == 0)
            {
                parts[s] = part;
                streams.push_back(std::move(std::make_shared<MergeTreeBlockInputStream>(
                          part->storage, part, 10000000, 0, 0, part->storage.getColumnNamesList(), MarkRanges(1, MarkRange(0, part->marks_count)),
                          false, nullptr, "", true, 0, DBMS_DEFAULT_BUFFER_SIZE, false)));
            }
        }
    }

    for(auto child: parent->getChildren())
    {
        fillStreams(child, streams, parts);
    }
}

BlockIO GdprInterpreter::execute()
{
    auto storage = context.getTable(context.getCurrentDatabase(), table);
    auto merge_tree = dynamic_cast<StorageMergeTree *>(storage.get());

    std::cout << "Gathering all affected parts\n";

    String dummy_select = "select * from " + table + " prewhere " + prewhere;

    ParserQuery parser(dummy_select.data());
    ASTPtr ast = parseQuery(parser, dummy_select.data(), dummy_select.data() + dummy_select.size(), "GDPR dummy select query");

    auto isq = InterpreterSelectQuery(ast, context, QueryProcessingStage::Complete);
    auto block = isq.execute();

    MergeTreeStreams streams;
    MergeTreeStreamPartMap parts;
    fillStreams(block.in, streams, parts);

    for (auto pp: merge_tree->getData().getDataParts())
    {
        if (parts.count(pp->getFullPath()) == 1)
        {
            std::cout << "* ";
        }
        else
        {
            std::cout << "  ";
        }
        std::cout << pp->getFullPath() << "\n";
    }

    std::cout << "Searching for occurences\n";
    size_t replaced = 0;
    MergeTreeBlockOutputStream outstream(*merge_tree);
    for(auto stream : streams)
    {
        auto mypart = stream->getDataParts()[0];
        std::cout << "Scanning " << mypart->getFullPath() << "\n";
        std::vector<Block> blocks;
        while(true)
        {
            Block b = stream->read();
            if (b.rows() == 0)
                break;

            blocks.push_back(std::move(b));
        }

        size_t found = 0;
        for(auto & b : blocks)
        {
            size_t pos = b.getPositionByName(column);
            ColumnWithTypeAndName & oldcolumn = b.getByPosition(pos);
            ColumnWithTypeAndName newcolumn = oldcolumn.cloneEmpty();
            ColumnString * col = dynamic_cast<ColumnString *>(oldcolumn.column.get());
            const char* newval = newvalue.c_str();
            size_t newvallen = strlen(newval) + 1;
            for(size_t row = 0; row < b.rows(); ++row)
            {
                StringRef s = col->getDataAtWithTerminatingZero(row);
                if(!strcasecmp(s.data, oldvalue.c_str()))
                {
                    std::cout << s.data << " vs " << oldvalue.c_str() << " - replacing\n";
                    newcolumn.column->insertDataWithTerminatingZero(newval, newvallen);
                    ++replaced;
                    ++found;
                }
                else
                {
                    //std::cout << s.data << " vs " << oldvalue.c_str() << " - leaving\n";
                    newcolumn.column->insertFrom(*col, row);
                }
            }

            if (found > 0)
            {
                b.erase(pos);
                b.insert(pos, newcolumn);
            }
        }

        if (found > 0)
        {
            std::cout << found << " occurences found\n";
            merge_tree->getData().renameAndDetachPart(mypart, "", false, true);
            std::cout << "Storing new part.\n";
            for(auto & b : blocks)
            {
                outstream.write(b);
            }
        }
    }

    std::cout << "Replaced " << replaced << " occurences.\n";

    return {};
}

}
