#! /bin/bash

# set -x
source global_config.sh

if [ ! -n "$1" ] ;then
  echo "./start_server.sh server_thread_nr"
  exit
fi

cd $exe_path

echo $1 

make -j
./restartMemc.sh

cd -

for ip in "${servers[@]}"
do
	echo $ip  
  sshpass -p ${server_passwd} ssh ${ip} "cd  ${exe_path}; ./${server_exe} ${logic_nr_server_node} ${logic_nr_client_node} $1 0 &>${ip}_0 &"
  sshpass -p ${server_passwd} ssh ${ip} "cd  ${exe_path}; ./${server_exe} ${logic_nr_server_node} ${logic_nr_client_node} $1 1 &>${ip}_1 &"
done