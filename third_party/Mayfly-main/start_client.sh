#! /bin/bash

# set -x
source global_config.sh


usage="./start_client.sh server_thread_nr client_thread_nr"

if [ ! -n "$1" ] ;then
  echo ${usage}
  exit
fi

if [ ! -n "$2" ] ;then
  echo ${usage}
  exit
fi

for ((i=1; i<${nr_client_node}; i++))
do
    ip=${clients[$i]}
    echo $ip  
    sshpass -p ${client_passwd} ssh ${ip} "cd  ${exe_path}; ./${client_exe} ${logic_nr_server_node} ${logic_nr_client_node} $1 $2 0 &>${ip}_0 &"
    sshpass -p ${client_passwd} ssh ${ip} "cd  ${exe_path}; ./${client_exe} ${logic_nr_server_node} ${logic_nr_client_node} $1 $2 1 &>${ip}_1 &"
done

cd $exe_path
echo "" > iops
echo "" > latency

ip=${clients[0]}
echo $ip
sshpass -p ${client_passwd} ssh ${ip} "cd  ${exe_path}; ./${client_exe} ${logic_nr_server_node} ${logic_nr_client_node} $1 $2 1 &>${ip}_1 &"
sshpass -p ${client_passwd} ssh ${ip} "cd  ${exe_path}; ./${client_exe} ${logic_nr_server_node} ${logic_nr_client_node} $1 $2 0 &>tmp.txt"

cd $exe_path

grep IOPS tmp.txt | awk '{print $2}' > iops
grep p50  tmp.txt > latency