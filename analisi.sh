#!/bin/bash
echo "Aspetto la chiusura del Processo $1"
while [ -e /proc/$1 ]
do
    sleep 2s
done

if [ -f "statsfile.log" ]; then
    while read line; do echo $line; done < statsfile.log
else
    echo "$0:Error" 1>&2
fi