#pragma once

#include <DataStreams/OneBlockInputStream.h>
#include <Interpreters/Context.h>
#include <Interpreters/IInterpreter.h>
#include <Parsers/ASTReplaceAllQuery.h>
#include <Storages/MergeTree/MergeTreeBlockInputStream.h>

#include <Common/typeid_cast.h>


namespace DB
{

class ExpressionAnalyzer;

using MergeTreeStreams =  std::vector<std::shared_ptr<MergeTreeBlockInputStream>>;
using MergeTreeStreamPartMap = std::map<String, std::shared_ptr<const MergeTreeDataPart> >;

class GdprInterpreter: public IInterpreter {
public:
	GdprInterpreter(
	        String table_,
	        String prewhere_,
	        String column_,
	        String oldvalue_,
            String newvalue_,
	        Context & context_);

	GdprInterpreter(const ASTPtr & query_ptr_, Context & context_);


	virtual ~GdprInterpreter();
    virtual BlockIO execute();

    String table;
    String prewhere;
    String column;
    String oldvalue;
    String newvalue;

private:
    void initLogBlock();
    void log(String severity, String message, String parameter);
    void fillParts(BlockInputStreamPtr parent, MergeTreeStreamPartMap & parts);

    std::unique_ptr<ExpressionAnalyzer> query_analyzer;
    std::shared_ptr<OneBlockInputStream> logstream;
    ColumnWithTypeAndName severity_column;
    ColumnWithTypeAndName message_column;
    ColumnWithTypeAndName parameter_column;

    Context & context;


};

}
