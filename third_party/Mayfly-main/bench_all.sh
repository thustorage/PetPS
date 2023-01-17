#! /bin/bash

# set -x
source global_config.sh


# Rowan
disable_ddio

sed -i 's/^.*#define RPC_VERSION.*$/\/\/#define RPC_VERSION/g' include/Common.h
sed -i 's/^.*#define PARALLEL_LOG_VERSION.*$/\/\/#define PARALLEL_LOG_VERSION/g' include/Common.h

python3 ./runBenchMark.py our


# RPC
enable_ddio

sed -i 's/^.*#define PARALLEL_LOG_VERSION.*$/\/\/#define PARALLEL_LOG_VERSION/g' include/Common.h
sed -i 's/^.*#define RPC_VERSION.*$/#define RPC_VERSION/g' include/Common.h

python3 ./runBenchMark.py rpc


# Parallel Logging

disable_ddio

sed -i 's/^.*#define RPC_VERSION.*$/\/\/#define RPC_VERSION/g' include/Common.h
sed -i 's/^.*#define PARALLEL_LOG_VERSION.*$/#define PARALLEL_LOG_VERSION/g' include/Common.h

python3 ./runBenchMark.py parallel_log


# Batch 
sed -i 's/^.*#define BATCH_REQ.*$/#define BATCH_REQ/g' include/Common.h

python3 ./runBenchMark.py batch



# Share
sed -i 's/^.*#define BATCH_REQ.*$/\/\/#define BATCH_REQ/g' include/Common.h
sed -i 's/^.*#define SHARE_LOG.*$/#define SHARE_LOG 1/g' include/Common.h

python3 ./runBenchMark.py share