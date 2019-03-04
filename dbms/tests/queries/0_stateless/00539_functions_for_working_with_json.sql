-- VisitParam with basic type
SELECT visitParamExtractInt('{"myparam":-1}', 'myparam');
SELECT visitParamExtractUInt('{"myparam":-1}', 'myparam');
SELECT visitParamExtractFloat('{"myparam":null}', 'myparam');
SELECT visitParamExtractFloat('{"myparam":-1}', 'myparam');
SELECT visitParamExtractBool('{"myparam":true}', 'myparam');
SELECT visitParamExtractString('{"myparam":"test_string"}', 'myparam');
SELECT visitParamExtractString('{"myparam":"test\\"string"}', 'myparam');
-- VisitParam with complex type
SELECT visitParamExtractRaw('{"myparam":"test_string"}', 'myparam');
SELECT visitParamExtractRaw('{"myparam": "test_string"}', 'myparam');
SELECT visitParamExtractRaw('{"myparam": "test\\"string"}', 'myparam');
SELECT visitParamExtractRaw('{"myparam": "test\\"string", "other":123}', 'myparam');
SELECT visitParamExtractRaw('{"myparam": ["]", "2", "3"], "other":123}', 'myparam');
SELECT visitParamExtractRaw('{"myparam": {"nested" : [1,2,3]}, "other":123}', 'myparam');
-- JsonAny
SELECT jsonAny('{"myparam":"test_string"}', 'myparam');
SELECT jsonAny('{"myparam": "test_string"}', 'myparam');
SELECT jsonAny('{"myparam": 5}', 'myparam');
SELECT jsonAny('{"myparam": ["]", "2", "3"], "other":123}', 'myparam');
SELECT jsonAny('{"myparam": {"nested": [1,2,3]}, "other":123}', 'myparam');
SELECT jsonAny('{"myparam": {"nested": [1,2,3]}, "other":123}', 'myparam.nested');
SELECT jsonAny('{"myparam": {"nested": [1,2,3]}, "other":123}', 'myparam.not_there');
SELECT jsonAny('{"myparam": [{"array_test": 1}, {"array_test": 2}, {"array_test": 3}], "other":123}', 'myparam.array_test');
SELECT jsonAny('{"myparam": {"nested": [1,2,3]}, "other":123}', '.myparam');
SELECT jsonAny('{"myparam": {"nested": {"hh": "abc"}}, "other":123"}', 'myparam.nested.hh');
SELECT jsonAny('{"myparam": {"nested": {"hh": "abc"}}, "other":123"}', 'myparam..hh');
SELECT jsonAny('{"myparam": {"nested": {"hh": "abc"}}, "other":123"}', 'myparam.');
