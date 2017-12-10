rm data-gdpr
python filldata.py
build/dbms/src/Server/clickhouse --local --config=gdprconfig.xml --query="drop table if exists default.testdata"
build/dbms/src/Server/clickhouse --local --config=gdprconfig.xml --query="create table default.testdata (historydate Date, someid Int32, someprivacyvalue String) Engine=MergeTree(historydate, someid, 8192)"
build/dbms/src/Server/clickhouse --server --config=gdprconfig.xml &
sleep 5
cat data-gdpr | build/dbms/src/Server/clickhouse --client --config=gdprconfig.xml --query="insert into default.testdata format Values";
kill %1
sleep 2
build/dbms/src/Server/clickhouse --local --config=gdprconfig.xml --query="select count() from default.testdata"

