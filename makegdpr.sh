build/dbms/src/Server/clickhouse --local --config=gdprconfig.xml --query="drop table if exists default.testdata"

build/dbms/src/Server/clickhouse --local --config=gdprconfig.xml --query="create table default.testdata (historydate Date, someid Int32, someprivacyvalue String) Engine=MergeTree(historydate, someid, 8192)"

build/dbms/src/Server/clickhouse --local --config=gdprconfig.xml --query="insert into default.testdata values (today(), 1, '1@gmail.com')"

build/dbms/src/Server/clickhouse --local --config=gdprconfig.xml --query="insert into default.testdata values (today(), 2, '2@gmail.com'), (today(), 3, '3@gmail.com')"

build/dbms/src/Server/clickhouse --local --config=gdprconfig.xml --query="insert into default.testdata values (today()-1, 4, '4@gmail.com')"

