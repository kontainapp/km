#!/bin/bash
#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#
#
#
# A helper to run AWS instance based on $AMI_name.
#
# $1 - instance type
# $2 - keypair
set -e ; [ "$TRACE" ] && set -x

[ "$TRACE" ] && set -x

instance_type=$1
key_pair=$2

# AMI name to create. Existing AMI with name will be deleted
# need to be synced with the one in ubuntu*aws*hcl file
AMI_name=Kontain_ubuntu_20.04
VM_name=test-$AMI_name

echo Fetching AMI id for AMI \"$AMI_name\" ...
ImageId=$(aws ec2 describe-images  --region us-west-1  \
		      --filters "Name=name,Values=$AMI_name" \
		      --output text --query "reverse(sort_by(Images, &CreationDate))[:1].ImageId")

echo Creating an instance \"$VM_name\" from AMI $ImageId...
InstanceId=$(aws ec2 run-instances --image-id $ImageId \
		--security-group-ids ssh-access --instance-type  $instance_type \
      --key-name $key_pair \
		--tag-specifications "ResourceType=instance,Tags=[{Key=Name,Value=$VM_name}]" \
		--output text --query "Instances[:1].InstanceId")

echo Waiting for instance \"$VM_name\" $InstanceId to become ready and expose an IP...
aws ec2 wait instance-running --instance-ids $InstanceId && \
IP=$(aws ec2 describe-instances --instance-ids $InstanceId \
		 --output text --query 'Reservations[*].Instances[*].PublicIpAddress')

echo "Connect to instance when sshd is up: ssh -i ~/.ssh/$key_pair.pem ubuntu@$IP"

