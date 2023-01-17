#! /bin/bash

# set -x


usage="./result_format.sh result_file"

if [ ! -n "$1" ] ;then
  echo ${usage}
  exit
fi

echo "---------------Throughput---------------------\n"

grep "\\[" $1 | awk -F '[\\[,]' '{ printf "%f\n", $2*8/1000000}'

echo "---------------GET P50---------------------\n"

grep p50 $1  | awk 'NR%2==1' | awk  '{print $2}'

echo "---------------GET P99---------------------\n"

grep p50 $1  | awk 'NR%2==1' | awk  '{print $8}'

echo "---------------GET P999---------------------\n"

grep p50 $1  | awk 'NR%2==1' | awk  '{print $10}'



echo "---------------PUT P50---------------------\n"

grep p50 $1  | awk 'NR%2==0' | awk  '{print $2}'

echo "---------------PUT P99---------------------\n"

grep p50 $1  | awk 'NR%2==0' | awk  '{print $8}'

echo "---------------PUT P999---------------------\n"

grep p50 $1  | awk 'NR%2==0' | awk  '{print $10}'
