#! /bin/bash

source global_config.sh

pkill ${server_exe}
pkill ${client_exe}
ps aux | grep rowan | grep -v grep | awk '{print $2}' | xargs kill -9
str=`ps aux | grep rowan | grep -v grep | awk '{print $2}'`
while true
do
    if [ -n "$str" ]; then
        str=`ps aux  |grep rowan | grep -v grep | awk '{print $2}'`
    else
        break
    fi
done 