rm data-gdpr
python filldata.py

build/dbms/src/Server/clickhouse --server --config=gdprconfig.xml &
sleep 5


build/dbms/src/Server/clickhouse --client --config=gdprconfig.xml --query="drop table if exists default.testdata"
build/dbms/src/Server/clickhouse --client --config=gdprconfig.xml --query="drop table if exists default.testdata_1"
build/dbms/src/Server/clickhouse --client --config=gdprconfig.xml --query="drop table if exists default.testdata_2"

build/dbms/src/Server/clickhouse --client --config=gdprconfig.xml --query="create table default.testdata (historydate Date, someid Int32, someprivacyvalue String) Engine=MergeTree(historydate, someid, 8192)"

build/dbms/src/Server/clickhouse --client --config=gdprconfig.xml --query="create table default.testdata_1 (historydate Date, someid Int32, someprivacyvalue String) Engine=ReplicatedMergeTree('/clickhouse/tables/dev/testdata_r', 'node1', historydate, someid, 8192)"

build/dbms/src/Server/clickhouse --client --config=gdprconfig.xml --query="create table default.testdata_2 (historydate Date, someid Int32, someprivacyvalue String) Engine=ReplicatedMergeTree('/clickhouse/tables/dev/testdata_r', 'node2', historydate, someid, 8192)"


cat data-gdpr | build/dbms/src/Server/clickhouse --client --config=gdprconfig.xml --query="insert into default.testdata format Values";
cat data-gdpr | build/dbms/src/Server/clickhouse --client --config=gdprconfig.xml --query="insert into default.testdata_1 format Values";

sleep 5

kill %1

sleep 2

build/dbms/src/Server/clickhouse --local --config=gdprconfig.xml --query="select count() from default.testdata"
build/dbms/src/Server/clickhouse --local --config=gdprconfig.xml --query="select count() from default.testdata_1"

