#!/bin/bash

while [ -e /proc/$1 ]
do
    echo "Aspetto che Esca il Supermercato";
    sleep 1s
done

clear;

if [ -f "statsfile.log" ]; then
    while read line; do echo $line; done < statsfile.log
else
    echo "$0:Error" 1>&2
fi
