#pragma once

#include <DataStreams/OneBlockInputStream.h>
#include <Interpreters/Context.h>
#include <Interpreters/IInterpreter.h>
#include <Parsers/ASTReplaceAllQuery.h>
#include <Storages/MergeTree/MergeTreeBlockInputStream.h>
#include <Storages/StorageMergeTree.h>
#include <Storages/StorageReplicatedMergeTree.h>

#include <Common/typeid_cast.h>


namespace DB
{

class ExpressionAnalyzer;

using MergeTreeStreams =  std::vector<std::shared_ptr<MergeTreeBlockInputStream>>;
using MergeTreeStreamPartMap = std::map<String, std::shared_ptr<const MergeTreeDataPart> >;

class InterpreterReplaceAll: public IInterpreter {
public:
	InterpreterReplaceAll(
	        String database_,
	        String table_,
	        String prewhere_,
	        String column_,
	        String oldvalue_,
            String newvalue_,
	        Context & context_);

	InterpreterReplaceAll(const ASTPtr & query_ptr_, Context & context_);


	virtual ~InterpreterReplaceAll();
    virtual BlockIO execute();

    String database;
    String table;
    String prewhere;
    String column;
    String oldvalue;
    String newvalue;
    size_t blockSize = 10000;

private:
    void initLogBlock();
    void log(String severity, String message, String parameter);
    void fillParts(BlockInputStreamPtr parent, MergeTreeStreamPartMap & parts);
    bool streamContainsOldValue(std::shared_ptr<MergeTreeBlockInputStream> stream);
    BlockIO fullBlockReplace(MergeTreeStreamPartMap& parts, StorageMergeTree * merge_tree, StorageReplicatedMergeTree * repl_merge_tree);
    BlockIO singleColumnReplace(MergeTreeStreamPartMap& parts, StorageMergeTree * merge_tree);
    Block readAndReplace(std::shared_ptr<MergeTreeBlockInputStream> readStream, size_t & replaced);

    std::unique_ptr<ExpressionAnalyzer> query_analyzer;
    std::shared_ptr<OneBlockInputStream> logstream;
    ColumnWithTypeAndName severity_column;
    ColumnWithTypeAndName message_column;
    ColumnWithTypeAndName parameter_column;

    Context & context;


};

}
