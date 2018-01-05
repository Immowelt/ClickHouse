#include <Parsers/ASTLiteral.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/ASTReplaceAllQuery.h>

#include <Parsers/CommonParsers.h>
#include <Parsers/ParserReplaceAllQuery.h>
#include <Parsers/ExpressionElementParsers.h>
#include <Parsers/ExpressionListParsers.h>

#include <Common/typeid_cast.h>


namespace DB
{


bool ParserReplaceAllQuery::parseImpl(Pos & pos, ASTPtr & node, Expected & expected)
{
    Pos begin = pos;

    ParserKeyword s_replace("REPLACE");
    ParserKeyword s_all("ALL");
    ParserKeyword s_with("WITH");
    ParserKeyword s_in("IN");
    ParserKeyword s_at("AT");
    ParserKeyword s_prewhere("PREWHERE");
    ParserStringLiteral oldvalue_p;
    ParserStringLiteral newvalue_p;
    ParserIdentifier tablename_p;
    ParserIdentifier columnname_p;
    ParserExpressionWithOptionalAlias exp_elem(false);

    ASTPtr oldvalue;
    ASTPtr newvalue;
    ASTPtr tablename;
    ASTPtr columnname;

    auto query = std::make_shared<ASTReplaceAllQuery>();

    if (!s_replace.ignore(pos, expected))
        return false;

    if (!s_all.ignore(pos, expected))
        return false;

    if (!oldvalue_p.parse(pos, oldvalue, expected))
        return false;

    if (!s_with.ignore(pos, expected))
        return false;

    if (!newvalue_p.parse(pos, newvalue, expected))
        return false;

    if (!s_in.ignore(pos, expected))
        return false;

    if (!tablename_p.parse(pos, tablename, expected))
        return false;

    if (!s_at.ignore(pos, expected))
        return false;

    if (!columnname_p.parse(pos, columnname, expected))
        return false;

    query->range = StringRange(begin, pos);

    query->oldvalue = safeGet<const String &>(typeid_cast<ASTLiteral &>(*oldvalue).value);
    query->newvalue = safeGet<const String &>(typeid_cast<ASTLiteral &>(*newvalue).value);
    query->table = typeid_cast<ASTIdentifier &>(*tablename).name;
    query->column = typeid_cast<ASTIdentifier &>(*columnname).name;
    query->prewhere = "1=1";

    node = query;

    return true;
}


}
