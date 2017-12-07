#pragma once

#include <Interpreters/Context.h>
#include <Interpreters/IInterpreter.h>


namespace DB
{

class ExpressionAnalyzer;

class GdprInterpreter: IInterpreter {
public:
	GdprInterpreter(
	        String table_,
	        String where_,
	        String column_,
	        String value_,
	        Context & context_);

	virtual ~GdprInterpreter();
    virtual BlockIO execute();

    String table;
    String where;
    String column;
    String value;

private:
    std::unique_ptr<ExpressionAnalyzer> query_analyzer;
    Context & context;
};

}
