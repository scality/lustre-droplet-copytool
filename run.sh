#!/bin/bash

CURR_IP="$(ifconfig | sed -En 's/127.0.0.1//;s/.*inet (addr:)?(([0-9]*\.){3}[0-9]*).*/\2/p')"

CURR_USER="$(whoami)"

echo "user '$CURR_USER@$CURR_IP'"

INSTANCE="$(aws ec2 run-instances --image-id ami-463d4226 --count 1 --key-name OlivierBuildAMI --instance-type t2.small --subnet-id subnet-4f50362a --security-group-ids sg-57915d33 --instance-initiated-shutdown-behavior terminate --associate-public-ip-address)"

INSTANCEID=$(echo $INSTANCE| sed -r 's/.*"InstanceId": "([^"]+)".*/ \1/')

SEARCH=0

while [ $SEARCH -eq 0 ]; do

INPUT="$(aws ec2 describe-instances --instance-ids $INSTANCEID)"

if [[ $INPUT == *"PublicIpAddress"* ]]; then
    IPADDR=$(echo $INPUT| sed -r 's/.*"PublicIpAddress": "([^"]+)".*/\1/')
    SEARCH=1;
else
    sleep 1;
fi
done

echo "vm's ip address '$IPADDR'"

ISAVAIL=0

while [ $ISAVAIL -eq 0 ]; do

NCRET="$(nc -z $IPADDR 22 > /dev/null)"

if [[ $NCRET -eq 0 ]]; then
    ISAVAIL=1;
else
    sleep 1;
fi
done

ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no centos@$IPADDR
