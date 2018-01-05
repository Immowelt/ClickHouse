#pragma once

#include <iomanip>
#include <Parsers/IAST.h>
#include <Parsers/ASTQueryWithOutput.h>


namespace DB
{


/** Query REPLACE ALL
  */
class ASTReplaceAllQuery : public ASTQueryWithOutput
{
public:
    String table;
    String prewhere;
    String column;
    String oldvalue;
    String newvalue;

    ASTReplaceAllQuery() = default;
    ASTReplaceAllQuery(const StringRange range_) : ASTQueryWithOutput(range_) {}

    /** Get the text that identifies this element. */
    String getID() const override { return "ReplaceAll"; };

    ASTPtr clone() const override
    {
        auto res = std::make_shared<ASTReplaceAllQuery>(*this);
        res->children.clear();
        cloneOutputOptions(*res);
        return res;
    }

protected:
    void formatQueryImpl(const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const override
    {
        settings.ostr << (settings.hilite ? hilite_keyword : "") << "REPLACE ALL" << (settings.hilite ? hilite_none : "");
    }
};

}
