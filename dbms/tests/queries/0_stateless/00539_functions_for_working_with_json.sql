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
-- Json
SELECT json('{"myparam":"test_string"}', 'myparam');
SELECT json('{"myparam": "test_string"}', 'myparam');
SELECT json('{"myparam": 5}', 'myparam');
SELECT json('{"myparam": ["]", "2", "3"], "other":123}', 'myparam');
SELECT json('{"myparam": {"nested": [1,2,3]}, "other":123}', 'myparam');
SELECT json('{"myparam": {"nested": [1,2,3]}, "other":123}', 'myparam.nested');
SELECT json('{"myparam": {"nested": [1,2,3]}, "other":123}', 'myparam.not_there');
SELECT json('{"myparam": [{"array_test": 1}, {"array_test": 2}, {"array_test": 3}], "other":123}', 'myparam.array_test');
SELECT json('{"myparam": {"nested": [1,2,3]}, "other":123}', '.myparam');
SELECT json('{"myparam": {"nested": {"hh": "abc"}}, "other":123}', 'myparam.nested.hh');
SELECT json('{"myparam": {"nested": {"hh": "abc"}}, "other":123}', 'myparam..hh');
SELECT json('{"myparam": {"nested": {"hh": "abc"}}, "other":123}', 'myparam.');
-- Jsons
SELECT jsons('{"myparam": "test_string"}', 'myparam');
SELECT jsons('{"myparam": 5}', 'myparam');
SELECT jsons('{"myparam": {"nested": [1,2,3]}, "other":123}', 'myparam');
SELECT jsons('{"myparam": {"nested": [1,2,3]}, "other":123}', 'myparam.nested');
SELECT jsons('{"myparam": [{"array_test": 1}, {"array_test": 2}, {"array_test": 3}], "other":123}', 'myparam.array_test');
-- multiJson
SELECT multiJson('{"myparam": [{"A": 1, "B": [{"leaf": 14},{"leaf": 15},{"leaf": 16}]}, {"A": 2, "B": [{"leaf": 24},{"leaf": 25},{"leaf": 26}]}, {"A": 3, "B": [{"leaf": 34},{"leaf": 35},{"leaf": 36}]}]}', 'myparam.A');
SELECT multiJson('{"myparam": [{"A": 1, "B": [{"leaf": 14},{"leaf": 15},{"leaf": 16}]}, {"A": 2, "B": [{"leaf": 24},{"leaf": 25},{"leaf": 26}]}, {"A": 3, "B": [{"leaf": 34},{"leaf": 35},{"leaf": 36}]}]}', 'myparam.B.leaf');
