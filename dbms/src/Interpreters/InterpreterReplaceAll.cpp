#include "InterpreterReplaceAll.h"

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
#include <Storages/MergeTree/MergedBlockOutputStream.h>
#include <Storages/StorageMergeTree.h>
#include <Storages/StorageReplicatedMergeTree.h>

#include <Poco/File.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int SYNTAX_ERROR;
}


InterpreterReplaceAll::InterpreterReplaceAll(
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

void InterpreterReplaceAll::initLogBlock()
{
    severity_column = ColumnWithTypeAndName{std::make_shared<ColumnString>(), std::make_shared<DataTypeString>(), "severity"};
    message_column = ColumnWithTypeAndName{std::make_shared<ColumnString>(), std::make_shared<DataTypeString>(), "message"};
    parameter_column = ColumnWithTypeAndName{std::make_shared<ColumnString>(), std::make_shared<DataTypeString>(), "parameter"};

    logstream= std::make_shared<OneBlockInputStream>(
        Block{severity_column, message_column, parameter_column});
}

void InterpreterReplaceAll::log(String severity, String message, String parameter)
{
    severity_column.column->insert(severity);
    message_column.column->insert(message);
    parameter_column.column->insert(parameter);

    //std::cout << "[" << severity << "] " << message << " " << parameter << "\n";
    LOG_INFO(&Logger::get("GdprInterpreter"), message + " " + parameter);
}

InterpreterReplaceAll::InterpreterReplaceAll(const ASTPtr & query_ptr_, Context & context_)
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


InterpreterReplaceAll::~InterpreterReplaceAll()
{
}


void InterpreterReplaceAll::fillParts(BlockInputStreamPtr parent, MergeTreeStreamPartMap & parts)
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

bool InterpreterReplaceAll::streamContainsOldValue(std::shared_ptr<MergeTreeBlockInputStream> stream)
{
    while(true)
    {
        Block b = stream->read();
        if (b.rows() == 0)
            return false;

        size_t pos = b.getPositionByName(column);
        ColumnWithTypeAndName & oldcolumn = b.getByPosition(pos);
        ColumnString * col = dynamic_cast<ColumnString *>(oldcolumn.column.get());
        for(size_t row = 0; row < b.rows(); ++row)
        {
            StringRef s = col->getDataAtWithTerminatingZero(row);
            if(!strcasecmp(s.data, oldvalue.c_str()))
                return true;
        }
    }
}


BlockIO InterpreterReplaceAll::execute()
{
    log("TRACE", "Replace all in", database + "." + table);
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

    // Trying to use quick method working only on columns not in the primary key
    if (merge_tree)
    {
        auto primaryKey = merge_tree->getData().getSortDescription();
        bool found = false;
        for(auto col : primaryKey)
        {
            if (col.column_name == column)
            {
                found = true;
                break;
            }
        }
        if (!found)
            return singleColumnReplace(parts, merge_tree);
    }
    return fullBlockReplace(parts, merge_tree, repl_merge_tree);
}

BlockIO InterpreterReplaceAll::singleColumnReplace(MergeTreeStreamPartMap& parts, StorageMergeTree * merge_tree)
{
    log("TRACE", "Single column replace", "");

    // Read only the column to replace from the parts
    Names singleColumn;
    singleColumn.push_back(column);

    size_t replaced = 0;
    for (auto part_pair : parts)
    {
        // Reading the single column of whole part. If the part is too large, we'll get OOM here.
        std::shared_ptr<MergeTreeBlockInputStream> readStream = std::make_shared<MergeTreeBlockInputStream>(
                part_pair.second->storage, part_pair.second, part_pair.second->rows_count, 0, 0, singleColumn,
                MarkRanges(1, MarkRange(0, part_pair.second->marks_count)),
                  false, nullptr, "", true, 0, DBMS_DEFAULT_BUFFER_SIZE, false);

        auto mypart = readStream->getDataParts()[0];
        log("TRACE", "Replacing single column in ", mypart->getNameWithPrefix());

        Block b = readAndReplace(readStream, replaced);
        if (b.rows() > 0)
        {
            MergedBlockOutputStream out(merge_tree->getData(), part_pair.second->getFullPath(), b);
            out.writeSingleColumn(b.getColumns()[0], part_pair.second->checksums);
            out.flush();
            context.dropMarkCache();
        }
        log("INFO", "Column replaced, row number: ", std::to_string(b.rows()));
    }

    log("INFO", "Occurences replaced in total ", std::to_string(replaced));

    BlockIO res;
    res.in = logstream;
    return res;

}

Block InterpreterReplaceAll::readAndReplace(std::shared_ptr<MergeTreeBlockInputStream> readStream, size_t & replaced)
{
    Block b = readStream->read();
    if (b.rows() == 0)
        return b;

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
            newcolumn.column->insertDataWithTerminatingZero(newval, newvallen);
            ++replaced;
        }
        else
        {
            newcolumn.column->insertFrom(*col, row);
        }
    }

    //if at least one replacement found, remove the old column and add the new column to the block.
    b.erase(pos);
    b.insert(pos, newcolumn);

    return b;
}


BlockIO InterpreterReplaceAll::fullBlockReplace(MergeTreeStreamPartMap& parts, StorageMergeTree * merge_tree, StorageReplicatedMergeTree * repl_merge_tree)
{
    // Now create input streams that would read the whole parts. We want to read whole parts, because we must replace the whole
    // affected part with its copy with some modified values. We also want to read all columns, because the column to modify might be
    // the part of the primary key, so when it gets modified the sorting order will change and all rows must be written anew.
    // BUT, Because a part can be very large, cannot read the whole part in the memory.
    // Instead, must first read it once block for block to find out whether it contains the oldvalue
    // Then if it contains the old value, must read it block for block again to write a new part containing the new value
    MergeTreeStreams readStreams;
    log("TRACE", "Full block replace", "");
    for (auto part_pair : parts)
    {
        std::shared_ptr<MergeTreeBlockInputStream> searchStream = std::make_shared<MergeTreeBlockInputStream>(
                part_pair.second->storage, part_pair.second, blockSize, 0, 0, part_pair.second->storage.getColumnNamesList(),
                MarkRanges(1, MarkRange(0, part_pair.second->marks_count)),
                  false, nullptr, "", true, 0, DBMS_DEFAULT_BUFFER_SIZE, false);

        auto mypart = searchStream->getDataParts()[0];
        log("TRACE", "Scanning ", mypart->getNameWithPrefix());

        if(streamContainsOldValue(searchStream))
        {
            readStreams.push_back(std::move(std::make_shared<MergeTreeBlockInputStream>(
                    part_pair.second->storage, part_pair.second, blockSize, 0, 0, part_pair.second->storage.getColumnNamesList(),
                    MarkRanges(1, MarkRange(0, part_pair.second->marks_count)),
                      false, nullptr, "", true, 0, DBMS_DEFAULT_BUFFER_SIZE, false)));
        }
    }

    size_t replaced = 0;

    if (readStreams.size() > 0)
    {
        log("TRACE", "Replacing occurences in the following number of parts:", std::to_string(readStreams.size()));

        size_t rows = 0;
        std::unique_ptr<MergeTreeBlockOutputStream> outstream = std::make_unique<MergeTreeBlockOutputStream>(*merge_tree);
        std::unique_ptr<ReplicatedMergeTreeBlockOutputStream> repl_outstream;
        if (repl_merge_tree)
            repl_outstream = std::make_unique<ReplicatedMergeTreeBlockOutputStream>(*repl_merge_tree, 0, 0, false);

        for(auto stream : readStreams)
        {
            // For each block, search and replace the values in memory. Here can be some memory leaks.
            while(true)
            {
                Block b = readAndReplace(stream, replaced);
                if (b.rows() == 0)
                    break;

                if (repl_merge_tree)
                {
                    repl_outstream->write(b);
                }
                else
                {
                    outstream->write(b);
                }

                rows += b.rows();
            }

            log("TRACE", "Scanned rows", std::to_string(rows));

            log("TRACE", "New part stored.", ""); // unfortunately outstream doesn't return the name of the new part(s), so we can't log them
        }

        log("TRACE", "Detaching old parts.", "");

        for(auto part_pair : parts)
        {
            auto mypart = part_pair.second;
            log("INFO", "Detaching old part", mypart->getNameWithPrefix());
            if (repl_merge_tree)
            {
                repl_merge_tree->dropSinglePart(mypart->getNameWithPrefix(), true, context);
            }
            else
            {
                merge_tree->getData().renameAndDetachPart(mypart, "", false, true);
            }
        }
    }

    log("INFO", "Occurences replaced in total ", std::to_string(replaced));

    BlockIO res;
    res.in = logstream;
    return res;
}

}
