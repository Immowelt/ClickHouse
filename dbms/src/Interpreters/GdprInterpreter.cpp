#include "GdprInterpreter.h"

#include <strings.h>

#include <DataStreams/OneBlockInputStream.h>
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
#include <Storages/MergeTree/ReplicatedMergeTreeBlockOutputStream.h>
#include <Storages/StorageMergeTree.h>
#include <Storages/StorageReplicatedMergeTree.h>

#include <Poco/File.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int SYNTAX_ERROR;
}


GdprInterpreter::GdprInterpreter(
        String database_,
        String table_,
        String prewhere_,
        String column_,
        String oldvalue_,
        String newvalue_,
        Context & context_):
                database(database_),
                table(table_),
                prewhere(prewhere_),
                column(column_),
                oldvalue(oldvalue_),
                newvalue(newvalue_),
                context(context_)
{
    initLogBlock();
}

void GdprInterpreter::initLogBlock()
{
    severity_column = ColumnWithTypeAndName{std::make_shared<ColumnString>(), std::make_shared<DataTypeString>(), "severity"};
    message_column = ColumnWithTypeAndName{std::make_shared<ColumnString>(), std::make_shared<DataTypeString>(), "message"};
    parameter_column = ColumnWithTypeAndName{std::make_shared<ColumnString>(), std::make_shared<DataTypeString>(), "parameter"};

    logstream= std::make_shared<OneBlockInputStream>(
        Block{severity_column, message_column, parameter_column});
}

void GdprInterpreter::log(String severity, String message, String parameter)
{
    severity_column.column->insert(severity);
    message_column.column->insert(message);
    parameter_column.column->insert(parameter);

    //std::cout << "[" << severity << "] " << message << " " << parameter << "\n";
    LOG_INFO(&Logger::get("GdprInterpreter"), message + " " + parameter);
}

GdprInterpreter::GdprInterpreter(const ASTPtr & query_ptr_, Context & context_)
: context(context_)
{
    ASTReplaceAllQuery * ast = typeid_cast<ASTReplaceAllQuery *>(query_ptr_.get());

    if(ast)
    {
        database = ast->database;
        table = ast->table;
        prewhere = ast->prewhere;
        column = ast->column;
        oldvalue = ast->oldvalue;
        newvalue = ast->newvalue;
    }
    else
    {
        throw Exception("GdprInterpreter called not for REPLACE ALL query???", ErrorCodes::SYNTAX_ERROR);
    }

    initLogBlock();
}


GdprInterpreter::~GdprInterpreter()
{
}


void GdprInterpreter::fillParts(BlockInputStreamPtr parent, MergeTreeStreamPartMap & parts)
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
            }
        }
    }

    for(auto child: parent->getChildren())
    {
        fillParts(child, parts);
    }
}


BlockIO GdprInterpreter::execute()
{
    log("TRACE", "GDPR in", database + "." + table);
    context.setCurrentDatabase(database);
    auto storage = context.getTable(database, table);
    StorageMergeTree * merge_tree = dynamic_cast<StorageMergeTree *>(storage.get());
    StorageReplicatedMergeTree * repl_merge_tree = nullptr;

    if(!merge_tree)
    {
        repl_merge_tree = dynamic_cast<StorageReplicatedMergeTree *>(storage.get());
        if (!repl_merge_tree)
        {
            log("ERROR", "Only MergeTree and ReplicatedMergeTree engines are supported", table);
            BlockIO res;
            res.in = logstream;
            return res;
        }
    }

    log("TRACE", "Determining all affected parts", "");

    // Its not possible to directly ask a MergeTree to return all parts that could match a specific prewhere statement.
    // So we construct a dummy select statement, and then retrieve the affected parts from the returned input streams.
    String dummy_select = "select * from " + table + " prewhere " + prewhere;

    ParserQuery parser(dummy_select.data());
    ASTPtr ast = parseQuery(parser, dummy_select.data(), dummy_select.data() + dummy_select.size(), "GDPR dummy select query");

    auto isq = InterpreterSelectQuery(ast, context, QueryProcessingStage::Complete);
    auto block = isq.execute();

    MergeTreeStreamPartMap parts;
    fillParts(block.in, parts);

    // Now create input streams that would read the whole parts. We want to read whole parts, because we must replace the whole
    // affected part with its copy with some modified values. We also want to read all columns, because the column to modify might be
    // the part of the primary key, so when it gets modified the sorting order will change and all rows must be written anew.
    MergeTreeStreams streams;
    for (auto part_pair : parts)
    {
        streams.push_back(std::move(std::make_shared<MergeTreeBlockInputStream>(
                part_pair.second->storage, part_pair.second, 10000000, 0, 0, part_pair.second->storage.getColumnNamesList(),
                MarkRanges(1, MarkRange(0, part_pair.second->marks_count)),
                  false, nullptr, "", true, 0, DBMS_DEFAULT_BUFFER_SIZE, false)));
    }

    // Display affected parts to check how many of them are affected, to be able to track performance problems.
    if (repl_merge_tree)
    {
        for (auto pp: repl_merge_tree->getData().getDataParts())
        {
            log("TRACE", (parts.count(pp->getFullPath()) == 1) ? "AFFECTED" : "NOT AFFECTED", pp->getNameWithPrefix());
        }
    }
    else
    {
        for (auto pp: merge_tree->getData().getDataParts())
        {
            log("TRACE", (parts.count(pp->getFullPath()) == 1) ? "AFFECTED" : "NOT AFFECTED", pp->getNameWithPrefix());
        }
    }

    // Now go through all streams, read them, update the value in memory and write out into a new part
    log("TRACE", "Searching for occurences", "");
    size_t replaced = 0;
    std::unique_ptr<MergeTreeBlockOutputStream> outstream = std::make_unique<MergeTreeBlockOutputStream>(*merge_tree);
    std::unique_ptr<ReplicatedMergeTreeBlockOutputStream> repl_outstream;
    if (repl_merge_tree)
        repl_outstream = std::make_unique<ReplicatedMergeTreeBlockOutputStream>(*repl_merge_tree, 0, 0, false);
    for(auto stream : streams)
    {
        auto mypart = stream->getDataParts()[0];
        log("TRACE", "Scanning ", mypart->getNameWithPrefix());
        std::vector<Block> blocks;
        int rows = 0;

        // Read all blocks from the current part
        while(true)
        {
            Block b = stream->read();
            if (b.rows() == 0)
                break;

            rows += b.rows();
            blocks.push_back(std::move(b));
        }


        // For each block, search and replace the values in memory. Here can be some memory leaks.
        size_t found = 0;
        for(auto & b : blocks)
        {
            size_t pos = b.getPositionByName(column);
            ColumnWithTypeAndName & oldcolumn = b.getByPosition(pos);
            ColumnWithTypeAndName newcolumn = oldcolumn.cloneEmpty();
            ColumnString * col = dynamic_cast<ColumnString *>(oldcolumn.column.get());
            const char* newval = newvalue.c_str();
            size_t newvallen = strlen(newval) + 1;

            // Iterate through all rows of a block. It seems to be compilicated to vectorize this operation.
            for(size_t row = 0; row < b.rows(); ++row)
            {
                StringRef s = col->getDataAtWithTerminatingZero(row);
                if(!strcasecmp(s.data, oldvalue.c_str()))
                {
                    //std::cout << s.data << " vs " << oldvalue.c_str() << " - replacing\n";
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

            //if at least one replacement found, remove the old column and add the new column to the block.
            if (found > 0)
            {
                b.erase(pos);
                b.insert(pos, newcolumn);
            }
        }

        log("TRACE", "Scanned rows", std::to_string(rows));

        if (found > 0)
        {
            log("INFO", "Occurences found", std::to_string(found));

            // As soon as the first occurence found in the current part, we can already detach it, because we know
            // that we are going to replace it. If we add a new part first, something could happen that would
            // merge the new and the old parts together, making further deduplication complicated.
            // OTOH if we detach the part first and then crash failing to write a new part, it would be
            // possible to recover by attaching the old part again.
            if (repl_merge_tree)
            {
                //repl_merge_tree->getData().renameAndDetachPart(mypart, "", false, true);
                repl_merge_tree->dropSinglePart(mypart->getNameWithPrefix(), true, context);
            }
            else
            {
                merge_tree->getData().renameAndDetachPart(mypart, "", false, true);
            }

            log("INFO", "Detaching old part", mypart->getNameWithPrefix());
            log("TRACE", "Storing new part", "");
            if (repl_merge_tree)
            {
                for(auto & b : blocks)
                {
                    repl_outstream->write(b);
                }
            }
            else
            {
                for(auto & b : blocks)
                {
                    outstream->write(b);
                }
            }
            log("TRACE", "New part stored.", ""); // unfortunately outstream doesn't return the name of the new part(s), so we can't log them
        }
    }

    log("INFO", "Occurences replaced in total ", std::to_string(replaced));


    BlockIO res;
    res.in = logstream;
    return res;
}

}
