#!/bin/bash
#
# Copyright © 2020 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# script to trigger regression in aws

[ "$TRACE" ] && set -x

# The following variables are stored in azure pipeline
# AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY and AWS_FEDORA_PASSWD
#
# paramters are TEST_BRANCH
#

export AWS_ACCESS_KEY_ID=$KONTAIN_AWS_ACCESS_KEY_ID
export AWS_SECRET_ACCESS_KEY=$KONTAIN_AWS_SECRET_ACCESS_KEY
readonly AWS_FEDORA_PASSWD=$KONTAIN_AWS_FEDORA_PASSWD
readonly TEST_BRANCH=$1

export AWS_DEFAULT_OUTPUT='text'
export AWS_DEFAULT_REGION='us-east-2'

readonly TEST_AMI='ami-0e3c11c690a214de5'
readonly TEST_VM_TYPE='m5.large'
readonly TEST_SG='sg-0a319ac77d674c77f'

function error_exit {
   echo $1
   aws ec2 stop-instances --instance-ids ${INSTANCE_ID} >& /dev/null
   echo "Triggered shutdown command for instance ${INSTANCE_ID}"
   exit 1
}

function get_instance_state {
   state=`aws ec2 describe-instances --instance-ids $1 --query "Reservations[].Instances[].State.Name"`
   if [ $? -ne 0 ]
   then
      error_exit "failed to get instance state"
   fi
}

function wait_for_state {
   single_wait=1
   count=60
   for i in $(seq 1 $count); do
      get_instance_state $1
      if [ $state = $2 ]
      then
         break
      fi
      sleep $single_wait
      echo "waiting for state change iteration $i current state $state expected $2"
   done
   if [ $i -eq $count ]
   then
      error_exit "Timeout waiting for state change"
   fi
}

# make new instance from AMI

INSTANCE_ID=`aws ec2 run-instances --image-id $TEST_AMI --count 1 --instance-type $TEST_VM_TYPE  --key-name km --security-group-ids $TEST_SG --query "Instances[].InstanceId"`
if [ $? -ne 0 ]
then
      error_exit "New instance creation failed"
fi
echo "New instance ${INSTANCE_ID} created"
echo "Waiting for new instance ${INSTANCE_ID} to enter running state"

# wait for instance to enter running state

wait_for_state ${INSTANCE_ID} 'running'
echo "Instance ${INSTANCE_ID} entered running state"

# give it a couple of seconds

sleep 5

# get the public IP address from newly created instance

INSTANCE_IP=`aws ec2 describe-instances --instance-ids ${INSTANCE_ID} --query "Reservations[].Instances[].PublicIpAddress"`
if [ $? -ne 0 ]
then
      error_exit "Failed to obtain IP address for ${INSTANCE_ID}"
fi

echo "Instance ${INSTANCE_ID} IP address ${INSTANCE_IP}"

# run tests on newly created instance

export readonly SSHPASS=$AWS_FEDORA_PASSWD
sshpass -e scp -oStrictHostKeyChecking=no cloud/aws/kkm-test.bash fedora@${INSTANCE_IP}:bin/kkm-test.bash
if [ $? -ne 0 ]
then
      error_exit "Copying test script failed for ${INSTANCE_ID}"
fi

echo "Starting tests on instance ${INSTANCE_ID} IP address ${INSTANCE_IP}"
TEST_STRING=`sshpass -e ssh -oStrictHostKeyChecking=no fedora@${INSTANCE_IP} /home/fedora/bin/kkm-test.bash ${TEST_BRANCH}`
if [[ $TEST_STRING == *"tests successfull"* ]]; then
   echo "TEST PASSED"
else
   error_exit "TEST FAILED"
fi

# tests are successfull terminate instance

echo "Terminating instance ${INSTANCE_ID}"

aws ec2 terminate-instances --instance-ids ${INSTANCE_ID} >& /dev/null
wait_for_state ${INSTANCE_ID} 'terminated'

echo "REGRESSION COMPLTED"

exit 0
