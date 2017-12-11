ls -l /var/lib/clickhouse/data/default/testdata/
build/dbms/src/Server/clickhouse --local --config=gdprconfig.xml --query="select * from default.testdata where someid=2"

build/dbms/src/Server/clickhouse --gdpr --config=gdprconfig.xml --table=testdata --prewhere="someid=2" --column=someprivacyvalue --newvalue=WIDERRUF --oldvalue=2@gmail.com

build/dbms/src/Server/clickhouse --local --config=gdprconfig.xml --query="select * from default.testdata where someprivacyvalue='WIDERRUF'"
build/dbms/src/Server/clickhouse --local --config=gdprconfig.xml --query="select count() from default.testdata "
ls -l /var/lib/clickhouse/data/default/testdata/
