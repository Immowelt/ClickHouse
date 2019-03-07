#include <Functions/FunctionFactory.h>
#include <Functions/FunctionsVisitParam.h>
#include <Functions/FunctionsStringSearch.h>


namespace DB
{

struct NameVisitParamHas           { static constexpr auto name = "visitParamHas"; };
struct NameVisitParamExtractUInt   { static constexpr auto name = "visitParamExtractUInt"; };
struct NameVisitParamExtractInt    { static constexpr auto name = "visitParamExtractInt"; };
struct NameVisitParamExtractFloat  { static constexpr auto name = "visitParamExtractFloat"; };
struct NameVisitParamExtractBool   { static constexpr auto name = "visitParamExtractBool"; };
struct NameVisitParamExtractRaw    { static constexpr auto name = "visitParamExtractRaw"; };
struct NameVisitParamExtractString { static constexpr auto name = "visitParamExtractString"; };
struct NameJson                    { static constexpr auto name = "json"; };
struct NameJsons                   { static constexpr auto name = "jsons"; };
struct NameMultiJson               { static constexpr auto name = "multiJson"; };


using FunctionVisitParamHas = FunctionsStringSearch<ExtractParamImpl<HasParam>, NameVisitParamHas>;
using FunctionVisitParamExtractUInt = FunctionsStringSearch<ExtractParamImpl<ExtractNumericType<UInt64>>, NameVisitParamExtractUInt>;
using FunctionVisitParamExtractInt = FunctionsStringSearch<ExtractParamImpl<ExtractNumericType<Int64>>, NameVisitParamExtractInt>;
using FunctionVisitParamExtractFloat = FunctionsStringSearch<ExtractParamImpl<ExtractNumericType<Float64>>, NameVisitParamExtractFloat>;
using FunctionVisitParamExtractBool = FunctionsStringSearch<ExtractParamImpl<ExtractBool>, NameVisitParamExtractBool>;
using FunctionVisitParamExtractRaw = FunctionsStringSearchToString<ExtractParamToStringImpl<ExtractRaw>, NameVisitParamExtractRaw>;
using FunctionVisitParamExtractString = FunctionsStringSearchToString<ExtractParamToStringImpl<ExtractString>, NameVisitParamExtractString>;
using FunctionJson = FunctionsStringSearchToString<ExtractJsonWithPathSupportImpl, NameJson>;
using FunctionJsons = FunctionJsonsWithPathSupport<NameJsons>;
using FunctionMultiJson = FunctionMultiJsonWithPathSupport<NameMultiJson>;


void registerFunctionsVisitParam(FunctionFactory & factory)
{
    factory.registerFunction<FunctionVisitParamHas>();
    factory.registerFunction<FunctionVisitParamExtractUInt>();
    factory.registerFunction<FunctionVisitParamExtractInt>();
    factory.registerFunction<FunctionVisitParamExtractFloat>();
    factory.registerFunction<FunctionVisitParamExtractBool>();
    factory.registerFunction<FunctionVisitParamExtractRaw>();
    factory.registerFunction<FunctionVisitParamExtractString>();
    factory.registerFunction<FunctionJson>();
    factory.registerFunction<FunctionJsons>();
    factory.registerFunction<FunctionMultiJson>();
}

}
