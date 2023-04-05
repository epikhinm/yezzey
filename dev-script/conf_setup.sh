
source gpAux/gpdemo/gpdemo-env.sh
source /usr/local/gpdb/greenplum_path.sh
export GPHOME=/usr/local/gpdb/
export PATH=$PATH:/usr/local/gpdb/bin


make -j32 install
make destroy-demo-cluster && make create-demo-cluster
gpconfig -c yezzey.storage_prefix -v "'wal-e/mdbrhqjnl6k5duk7loi2/6/segments_005'"
gpconfig -c yezzey.storage_bucket -v "'loh228'"
gpconfig -c yezzey.storage_config -v "'/home/reshke/s3test.conf'"
gpconfig -c yezzey.storage_host -v "'storage.yandexcloud.net'"
gpconfig -c yezzey.gpg_key_id -v  "'5697E1083B8509B8'"
gpconfig -c yezzey.walg_bin_path -v  "'/home/reshke/work/wal-g/main/gp/wal-g'"
gpconfig -c yezzey.walg_config_path -v  "'/home/reshke/work/wal-g/conf.yaml'"

gpconfig -c yezzey.autooffload -v  "on"

gpconfig -c shared_preload_libraries -v yezzey


gpstop -a -i && gpstart -a

psql postgres -f ./gpcontrib/yezzey/test/regress/yezzey.sql

make -j32 install 
gpstop -a -i && gpstart -a

source gpAux/gpdemo/gpdemo-env.sh
source /usr/local/gpdb/greenplum_path.sh
export GPHOME=/usr/local/gpdb/
export PATH=$PATH:/usr/local/gpdb/bin


psql postgres -f ./gpcontrib/yezzey/test/regress/yezzey.sql
