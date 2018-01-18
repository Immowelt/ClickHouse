#pragma once

#include <Parsers/ParserQueryWithOutput.h>


namespace DB
{

/** Query like this:
  * REPLACE ALL 'e@mail' WITH 'Revoked' IN table AT column PREWHERE bla
  */
class ParserReplaceAllQuery : public IParserBase
{
protected:
    const char * getName() const { return "REPLACE ALL query"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};

}
