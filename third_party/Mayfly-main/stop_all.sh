#! /bin/bash

source global_config.sh

cd $exe_path

for ip in "${servers[@]}"
do
	echo $ip  
    sshpass -p ${server_passwd} ssh ${ip} "cd  ${src_path}; bash ./local_stop.sh >/dev/null 2>&1"
done

for ip in "${clients[@]}"
do
	echo $ip  
    sshpass -p ${client_passwd} ssh ${ip} "cd  ${src_path}; bash ./local_stop.sh >/dev/null 2>&1"
done