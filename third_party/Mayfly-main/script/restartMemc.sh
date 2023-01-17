#!/bin/bash
cd $(dirname $(readlink -f $0))

addr=$(head -1 ../memcached.conf)
port=$(awk 'NR==2{print}' ../memcached.conf)

echo restart memcached in $addr:$port

user_name=xieminhui
passwd=1234

# kill old me
sshpass -p ${passwd} ssh -o StrictHostKeyChecking=no ${user_name}@${addr} "sudo service memcached stop"
sshpass -p ${passwd} ssh -o StrictHostKeyChecking=no ${user_name}@${addr} "ps aux |grep memcac |grep -v grep | awk '{print \$2}'|xargs sudo kill -9"
sleep 1

# launch memcached
sshpass -p ${passwd} ssh -o StrictHostKeyChecking=no ${user_name}@${addr} "memcached -u root -l ${addr} -p  ${port} -c 10000 -d -P /tmp/memcached.wq.pid"
sleep 3

# init
echo -e "set serverNum 0 0 1\r\n0\r\nquit\r" | nc ${addr} ${port}
echo -e "set clientNum 0 0 1\r\n0\r\nquit\r" | nc ${addr} ${port}

sleep 1