#pragma once

#include <Interpreters/Context.h>
#include <Interpreters/IInterpreter.h>
#include <Storages/MergeTree/MergeTreeBlockInputStream.h>

namespace DB
{

class ExpressionAnalyzer;

using MergeTreeStreams =  std::vector<std::shared_ptr<MergeTreeBlockInputStream>>;
using MergeTreeStreamPartMap = std::map<String, std::shared_ptr<const MergeTreeDataPart> >;

class GdprInterpreter: IInterpreter {
public:
	GdprInterpreter(
	        String table_,
	        String prewhere_,
	        String column_,
	        String oldvalue_,
            String newvalue_,
	        Context & context_);

	virtual ~GdprInterpreter();
    virtual BlockIO execute();

    String table;
    String prewhere;
    String column;
    String oldvalue;
    String newvalue;

private:
    std::unique_ptr<ExpressionAnalyzer> query_analyzer;
    Context & context;
};

}
