set -x
# set -e
CapcityM=25

BIN=../build/bin/petps_server
list_of_1thread_warm="KVEngineMap KVEngineMapPM KVEngineCCEH KVEngineCCEHVM "

failed_db=""
function exists_in_list() {
	LIST=$1
	DELIMITER=$2
	VALUE=$3
	echo $LIST | tr "$DELIMITER" '\n' | grep -F -q -x "$VALUE"
}


if [ $# -gt 1 ]; then
TEST_DB="KVEnginePersistDoubleShmKV \
	KVEnginePersistShmKV KVEngineDash KVEngineCLHT \
	KVEngineLevel KVEngineClevel KVEngineCCEHVM \
	KVEngineMap KVEngineMapPM"
else
TEST_DB="$1"
fi

for each in $TEST_DB; do
	echo $each

	if exists_in_list "$list_of_1thread_warm" " " "$each"; then
		warmup_thread_num=1
	else
		warmup_thread_num=16
	fi

	rm -rf /media/aep1/*
	${BIN} --numa_id=1 --global_id=0 --num_server_processes=1 \
		--num_client_processes=2 --preload=true --thread_num=32 \
		--use_dram=false --prefetch_method=0 --db=${each} \
		--use_sglist=false --value_size=512 --key_space_m=${CapcityM} \
		--max_kv_num_per_request=500 --logtostderr=true --exit \
		--warmup_thread_num=${warmup_thread_num}
	if [ $? -ne 0 ]; then
		failed_db="${failed_db} $each"
	fi
done

echo failed_db $failed_db
