#!/bin/bash

while [ -e /proc/$1 ]
do
    sleep 1s
done

if [ -f "statsfile.log" ]; then
    while read line; do echo $line; done < statsfile.log
else
    echo "$0:Error" 1>&2
fi
