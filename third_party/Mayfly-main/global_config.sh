#! /usr/bin/bash

src_path="/home/wq_workspace/Ripple/"
exe_path=${src_path}/build

servers=(10.0.2.137 10.0.2.136 10.0.2.135)
server_passwd=ptFFGnXk
nr_server_node=${#servers[@]}
server_exe=rowan_server

clients=(10.0.2.134 10.0.2.133 10.0.2.132 10.0.2.130)
client_passwd=ptFFGnXk
nr_client_node=${#clients[@]}
client_exe=rowan_client_rpc


logic_nr_server_node=$(( $nr_server_node * 2 ))
logic_nr_client_node=$(( $nr_client_node * 2 ))

disable_ddio() {
    for ip in "${servers[@]}"
    do
    echo $ip  
    sshpass -p ${server_passwd} ssh ${ip} "cd  ${exe_path}; ./disable_ddio"
    done
}

enable_ddio() {
    for ip in "${servers[@]}"
    do
    echo $ip  
    sshpass -p ${server_passwd} ssh ${ip} "cd  ${exe_path}; ./enable_ddio"
    done
}