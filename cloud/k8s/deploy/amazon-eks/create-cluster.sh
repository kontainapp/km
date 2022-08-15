# Copyright 2021 Kontain
# Derived from:
# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#!/bin/bash

[ "$TRACE" ] && set -x

print_help() {
    echo "usage: $0  [options] prefix"
    echo "Creates EKS cluster with the name <prefix>-eks-cluster. All other associated recource names are prefixes with <prefix> "
    echo ""
    echo "Prerequisites:"
    echo "  AWS CLI"
    echo "  Environment variables"
    echo "  AWS_SECRET_ACCESS_KEY=XXXXXXXXXXXXX"
    echo "  AWS_ACCESS_KEY_ID=XXXXXXXXXXX"
    echo "  For more information see https://docs.aws.amazon.com/cli/latest/userguide/cli-chap-getting-started.html"
    echo ""
    echo "-h,--help print this help"
    echo "--region Sets aws region. Default to us-west-1"
    echo "--ami Kontain-anabled AMI id. Required to create cluster"
    echo "--cleanup Instructs script to delete cluster and all related resourses "
    exit 0
}


do_cleanup() {
    echo "cleanup"
    
    echo "delete load balancer"
    elb_name=$(kubectl get svc  -o jsonpath="{.items[?(@.spec.type == 'LoadBalancer')].status.loadBalancer.ingress[0].hostname}" | awk -F- '{print $1}')
    echo "found load balancer $elb_name"
    aws --region=$region elb delete-load-balancer --load-balancer-name=$elb_name

    echo "delete nodegroup"
    aws --no-paginate --region=$region eks delete-nodegroup --cluster-name $cluster_name --nodegroup-name $node_group_name --output text > /dev/null
    aws --no-paginate --region=$region eks wait nodegroup-deleted --cluster-name $cluster_name --nodegroup-name $node_group_name

    echo "delete cluster"
    aws --no-paginate --region=$region eks delete-cluster --name $cluster_name --no-paginate --output text > /dev/null
    aws --no-paginate --region=$region eks wait cluster-deleted --name $cluster_name --no-paginate

    echo "delete cloudformation stack"
    VPC_ID=$(aws --region=$region cloudformation describe-stacks --output text --stack-name $stack_name  --query="Stacks[0].Outputs[?OutputKey=='VpcId'].OutputValue|[0]" 2> /dev/null)
    ELB_S_GROUP=$(aws --region $region ec2 describe-security-groups --output text --filters Name=vpc-id,Values=$VPC_ID Name=group-name,Values=k8s-elb* --query "SecurityGroups[0].GroupId")
    STACK_GROUP=$(aws --region $region ec2 describe-security-groups --output text --filters Name=vpc-id,Values=$VPC_ID Name=group-name,Values=${prefix}-eks-vpc-stack* --query "SecurityGroups[0].GroupId")

    echo "  delete Stack security group"
    aws --region $region ec2 delete-security-group --group-id $STACK_GROUP --output text > /dev/null

    echo "  delete ELB security group"
    aws --region $region ec2 delete-security-group --group-id $ELB_S_GROUP --output text > /dev/null

    echo "  delete VPC and stack "
    aws --no-paginate --region=$region cloudformation delete-stack  --stack-name $stack_name --output text > /dev/null
    aws --no-paginate --no-cli-pager --region $region cloudformation wait stack-delete-complete --stack-name $stack_name

    echo "delete launch templete"
    aws --region=$region ec2 delete-launch-template --launch-template-name $launch_template_name --output text > /dev/null

    echo "delete node role"
    aws --region=$region iam detach-role-policy \
        --policy-arn arn:aws:iam::aws:policy/AmazonEKSWorkerNodePolicy \
        --role-name $node_role --output text > /dev/null
    aws --region=$region iam detach-role-policy \
        --policy-arn arn:aws:iam::aws:policy/AmazonEC2ContainerRegistryReadOnly \
        --role-name $node_role --output text > /dev/null
    aws --region=$region iam detach-role-policy \
        --policy-arn arn:aws:iam::aws:policy/AmazonEKS_CNI_Policy \
        --role-name $node_role --output text > /dev/null
    aws --region=$region iam detach-role-policy \
        --policy-arn arn:aws:iam::aws:policy/AmazonSSMManagedInstanceCore \
        --role-name $node_role --output text > /dev/null
    aws --region=$region iam delete-role --role-name $node_role

    echo "delete cluster role"
    aws --region=$region iam detach-role-policy \
        --policy-arn arn:aws:iam::aws:policy/AmazonEKSClusterPolicy \
        --role-name $cluster_role --output text > /dev/null
    aws --region=$region iam delete-role --role-name $cluster_role --output text > /dev/null

    echo "delete key pair"
    aws --region=$region ec2 delete-key-pair --key-name $key_pair_name --output text > /dev/null
    rm -f $key_pair_name.pem

    echo "remove temporary files"
    rm -f stack-config.json
    rm -f cluster-role-trust-policy.json 
    rm -f user_data.txt
    rm -f launch-config.json
    rm -f node-trust-policy.json
}

main() {

    echo "create key pair"
    if [ ! -f $key_pair_name.pem ]; then 
        aws --region=$region ec2 create-key-pair --key-name $key_pair_name --query 'KeyMaterial' --output text > $key_pair_name.pem
        chmod 400 $key_pair_name.pem
    else
        echo "  already exists"
    fi

    echo "create Cloudformation VPC Stack"

    yaml_string='
---
AWSTemplateFormatVersion: "2010-09-09"
Description: "Kontain Amazon EKS Stack VPC - Public subnets only"

Parameters:
  VpcBlock:
    Type: String
    Default: 192.168.0.0/16
    Description: The CIDR range for the VPC. This should be a valid private (RFC 1918) CIDR range.

  Subnet01Block:
    Type: String
    Default: 192.168.64.0/18
    Description: CidrBlock for subnet 01 within the VPC

  Subnet02Block:
    Type: String
    Default: 192.168.128.0/18
    Description: CidrBlock for subnet 02 within the VPC

  Subnet03Block:
    Type: String
    Default: 192.168.192.0/18
    Description: CidrBlock for subnet 03 within the VPC. This is used only if the region has more than 2 AZs.

Metadata:
  AWS::CloudFormation::Interface:
    ParameterGroups:
      - Label:
          default: "Worker Network Configuration"
        Parameters:
          - VpcBlock
          - Subnet01Block
          - Subnet02Block
          - Subnet03Block

Conditions:
  Has2Azs:
    Fn::Or:
      - Fn::Equals:
          - { Ref: "AWS::Region" }
          - ap-south-1
      - Fn::Equals:
          - { Ref: "AWS::Region" }
          - ap-northeast-2
      - Fn::Equals:
          - { Ref: "AWS::Region" }
          - ca-central-1
      - Fn::Equals:
          - { Ref: "AWS::Region" }
          - cn-north-1
      - Fn::Equals:
          - { Ref: "AWS::Region" }
          - sa-east-1
      - Fn::Equals:
          - { Ref: "AWS::Region" }
          - us-west-1
      - Fn::Equals:
          - { Ref: "AWS::Region" }
          - us-east-2

  HasMoreThan2Azs:
    Fn::Not:
      - Condition: Has2Azs

Resources:
  VPC:
    Type: AWS::EC2::VPC
    Properties:
      CidrBlock: !Ref VpcBlock
      EnableDnsSupport: true
      EnableDnsHostnames: true
      Tags:
        - Key: Name
          Value: !Sub "${AWS::StackName}-VPC"

  InternetGateway:
    Type: "AWS::EC2::InternetGateway"

  VPCGatewayAttachment:
    Type: "AWS::EC2::VPCGatewayAttachment"
    Properties:
      InternetGatewayId: !Ref InternetGateway
      VpcId: !Ref VPC

  RouteTable:
    Type: AWS::EC2::RouteTable
    Properties:
      VpcId: !Ref VPC
      Tags:
        - Key: Name
          Value: Public Subnets
        - Key: Network
          Value: Public

  Route:
    DependsOn: VPCGatewayAttachment
    Type: AWS::EC2::Route
    Properties:
      RouteTableId: !Ref RouteTable
      DestinationCidrBlock: 0.0.0.0/0
      GatewayId: !Ref InternetGateway

  Subnet01:
    Type: AWS::EC2::Subnet
    Metadata:
      Comment: Subnet 01
    Properties:
      MapPublicIpOnLaunch: true
      AvailabilityZone:
        Fn::Select:
          - "0"
          - Fn::GetAZs:
              Ref: AWS::Region
      CidrBlock:
        Ref: Subnet01Block
      VpcId:
        Ref: VPC
      Tags:
        - Key: Name
          Value: !Sub "${AWS::StackName}-Subnet01"
        - Key: kubernetes.io/role/elb
          Value: 1

  Subnet02:
    Type: AWS::EC2::Subnet
    Metadata:
      Comment: Subnet 02
    Properties:
      MapPublicIpOnLaunch: true
      AvailabilityZone:
        Fn::Select:
          - "1"
          - Fn::GetAZs:
              Ref: AWS::Region
      CidrBlock:
        Ref: Subnet02Block
      VpcId:
        Ref: VPC
      Tags:
        - Key: Name
          Value: !Sub "${AWS::StackName}-Subnet02"
        - Key: kubernetes.io/role/elb
          Value: 1

  Subnet03:
    Condition: HasMoreThan2Azs
    Type: AWS::EC2::Subnet
    Metadata:
      Comment: Subnet 03
    Properties:
      MapPublicIpOnLaunch: true
      AvailabilityZone:
        Fn::Select:
          - "2"
          - Fn::GetAZs:
              Ref: AWS::Region
      CidrBlock:
        Ref: Subnet03Block
      VpcId:
        Ref: VPC
      Tags:
        - Key: Name
          Value: !Sub "${AWS::StackName}-Subnet03"
        - Key: kubernetes.io/role/elb
          Value: 1

  Subnet01RouteTableAssociation:
    Type: AWS::EC2::SubnetRouteTableAssociation
    Properties:
      SubnetId: !Ref Subnet01
      RouteTableId: !Ref RouteTable

  Subnet02RouteTableAssociation:
    Type: AWS::EC2::SubnetRouteTableAssociation
    Properties:
      SubnetId: !Ref Subnet02
      RouteTableId: !Ref RouteTable

  Subnet03RouteTableAssociation:
    Condition: HasMoreThan2Azs
    Type: AWS::EC2::SubnetRouteTableAssociation
    Properties:
      SubnetId: !Ref Subnet03
      RouteTableId: !Ref RouteTable

  ControlPlaneSecurityGroup:
    Type: AWS::EC2::SecurityGroup
    Properties:
      GroupDescription: Cluster communication with worker nodes
      VpcId: !Ref VPC

Outputs:
  SubnetIds:
    Description: All subnets in the VPC
    Value:
      Fn::If:
        - HasMoreThan2Azs
        - !Join [",", [!Ref Subnet01, !Ref Subnet02, !Ref Subnet03]]
        - !Join [",", [!Ref Subnet01, !Ref Subnet02]]

  SecurityGroups:
    Description: Security group for the cluster control plane communication with worker nodes
    Value: !Join [",", [!Ref ControlPlaneSecurityGroup]]

  VpcId:
    Description: The VPC Id
    Value: !Ref VPC
'
     echo "$yaml_string" > stack-config.yaml

    VPC_DESC=$(aws --region=$region cloudformation describe-stacks --stack-name $stack_name --query "Stacks[0].Outputs" 2> /dev/null)
    RET=$?
    if [ $RET != 0 ]; then  
        STACK_ID=$(aws cloudformation create-stack \
        --region $region \
        --stack-name $stack_name \
        --template-body file://stack-config.yaml \
        | jq -r '.StackId')
        echo STACK_ID = ${STACK_ID}

        echo "waiting for stack to be created"
        aws --no-paginate --region $region cloudformation wait stack-create-complete --stack-name $stack_name

        VPC_DESC=$(aws --region=$region cloudformation describe-stacks --stack-name $stack_name --query "Stacks[0].Outputs")
    else
        echo "  already exists"
    fi

    SECURITY_GROUP_IDS=$(echo ${VPC_DESC} | jq -r '.[] | select(.OutputKey | contains("SecurityGroups")) | .OutputValue')
    SUBNET_IDS=$(echo ${VPC_DESC} | jq -r '.[] | select(.OutputKey | contains("SubnetIds")) | .OutputValue' | tr ',' ' ')
    VPC_CONFIG=$(echo ${VPC_DESC} \
        | jq -r '[ .[] | select(.OutputKey | contains("SecurityGroups")), select(.OutputKey | contains("SubnetIds")) | .OutputValue ]' \
        | jq -rj '"securityGroupIds=", .[0], ",subnetIds=", .[1]')

    if [ $RET != 0 ]; then  
        echo "add ingress rules"
        aws --region=$region ec2 authorize-security-group-ingress --group-id ${SECURITY_GROUP_IDS} \
            --protocol tcp \
            --port 22 \
            --cidr 0.0.0.0/0

        aws --region=$region ec2 authorize-security-group-ingress --group-id ${SECURITY_GROUP_IDS} \
            --protocol tcp \
            --port 80 \
            --cidr 0.0.0.0/0

        aws --region=$region ec2 authorize-security-group-ingress --group-id ${SECURITY_GROUP_IDS} \
            --protocol tcp \
            --port 443 \
            --cidr 0.0.0.0/0 > /dev/null

        aws --region=$region ec2 authorize-security-group-ingress --group-id ${SECURITY_GROUP_IDS} \
            --protocol tcp \
            --port 10250 \
            --cidr 0.0.0.0/0 > /dev/null

        aws --region=$region ec2 authorize-security-group-ingress --group-id ${SECURITY_GROUP_IDS} \
            --protocol tcp \
            --port 53 \
            --cidr 0.0.0.0/0 > /dev/null

        aws --region=$region ec2 authorize-security-group-ingress --group-id ${SECURITY_GROUP_IDS} \
            --protocol udp \
            --port 53 \
            --cidr 0.0.0.0/0 > /dev/null
    fi

    echo "create cluster role"
    CLUSTER_ROLE_ARN=$(aws --region=$region iam   get-role --role-name $cluster_role  2> /dev/null | jq -r '.Role |.Arn')
    if [ -z ${CLUSTER_ROLE_ARN} ]; then
        echo "  create cluster policy config file"
        json_string='{
        "Version": "2012-10-17",
            "Statement": [
                {
                "Effect": "Allow",
                "Principal": {
                    "Service": "eks.amazonaws.com"
                },
                "Action": "sts:AssumeRole"
                }
            ]
        }'
        echo "$json_string" > cluster-role-trust-policy.json
        echo "  create role"
        CLUSTER_ROLE_ARN=$(aws --region=$region iam create-role \
        --role-name $cluster_role \
        --assume-role-policy-document file://cluster-role-trust-policy.json |jq -r '.Role |.Arn')

        echo "  attach policies"
        aws --region=$region iam attach-role-policy \
        --policy-arn arn:aws:iam::aws:policy/AmazonEKSClusterPolicy \
        --role-name $cluster_role --output text > /dev/null
    else
        echo "  already exists"
    fi

    echo "create cluster"
    CLUSTER_ARN=$(aws --region=$region --no-paginate eks describe-cluster --name $cluster_name  2> /dev/null | jq -r '.cluster | .arn')
    if [ -z ${CLUSTER_ARN} ]; then 
        aws --region=$region eks create-cluster --name $cluster_name \
        --role-arn ${CLUSTER_ROLE_ARN} \
        --resources-vpc-config ${VPC_CONFIG} --output text > /dev/null

        echo " waiting for cluster to become active"
        aws --no-paginate --region=$region eks wait cluster-active --name $cluster_name
    else 
        echo " already exists"
    fi

    echo "create a kubeconfig file for cluster"
    aws --region=$region eks update-kubeconfig --name $cluster_name

    echo "create node role"
    NODE_ROLE_ARN=$(aws --region=$region iam get-role --role-name $node_role  2> /dev/null | jq -r '.Role |.Arn')
    if [ -z ${NODE_ROLE_ARN} ]; then
        echo "  create node policy config file"
        json_string='{
            "Version": "2012-10-17",
            "Statement": [
                {
                "Effect": "Allow",
                "Principal": {
                    "Service": "ec2.amazonaws.com"
                },
                "Action": "sts:AssumeRole"
                }
            ]
        }'
        echo "$json_string" >  node-trust-policy.json 

        echo "  create node role"
        NODE_ROLE_ARN=$(aws --region $region iam create-role --role-name $node_role --assume-role-policy-document file://node-trust-policy.json \
            | jq -r '.Role |.Arn')

        echo "  assign policies"
        aws --region=$region iam attach-role-policy \
        --policy-arn arn:aws:iam::aws:policy/AmazonEKSWorkerNodePolicy \
        --role-name $node_role --output text > /dev/null
        aws --region=$region iam attach-role-policy \
        --policy-arn arn:aws:iam::aws:policy/AmazonEC2ContainerRegistryReadOnly \
        --role-name $node_role --output text > /dev/null
        aws --region=$region iam attach-role-policy \
        --policy-arn arn:aws:iam::aws:policy/AmazonEKS_CNI_Policy \
        --role-name $node_role --output text > /dev/null
        aws --region=$region iam attach-role-policy \
        --policy-arn arn:aws:iam::aws:policy/AmazonSSMManagedInstanceCore \
        --role-name $node_role --output text > /dev/null
    else 
        echo "already exists"
    fi

    echo "create launch template"
    aws --region=$region ec2 describe-launch-templates --launch-template-names $launch_template_name --output text >& /dev/null
    RET=$?
    if [ $RET != 0 ]; then  
        json_string="#!/bin/bash
/etc/eks/bootstrap.sh $cluster_name"

        echo "  writing user data"
        echo "$json_string" > user_data.txt
        
        USER_DATA=$(base64 --wrap=0 user_data.txt)

        json_string=$(jq  -n \
            "{
                \"ImageId\":        \"$ami_id\",
                \"InstanceType\":   \"t3.small\",
                \"KeyName\":       \"$key_pair_name\",
                \"UserData\": \"$USER_DATA\",
                \"NetworkInterfaces\": [
                    {
                        \"DeviceIndex\": 0,
                        \"Groups\": [\"$SECURITY_GROUP_IDS\"]
                    }
                ]
            }")
        
        echo "  writing launch config"
        echo "$json_string" > launch-config.json

        echo "  creating template"
        aws --region=$region ec2  create-launch-template --launch-template-name $launch_template_name \
            --launch-template-data file://launch-config.json > /dev/null
    else
        echo "already exists"
    fi

    echo "create node group with subnets ${SUBNET_IDS}"
    NODE_GROUP_STATUS=$(aws --region=$region eks describe-nodegroup --cluster-name $cluster_name --nodegroup-name $node_group_name 2> /dev/null | jq -r  '.nodegroup | .status')
    echo "NODE_GROUP_STATUS = $NODE_GROUP_STATUS"
    if [ ! -z $NODE_GROUP_STATUS ] && [ $NODE_GROUP_STATUS == "DELETING" ]; then 
        echo "  node group deleting - wait for completing"
        aws --no-paginate --region=$region eks wait nodegroup-deleted --cluster-name $cluster_name --nodegroup-name $node_group_name
    elif [ -z $NODE_GROUP_STATUS ]; then 
        echo "  creating node group"
        aws --region=$region eks create-nodegroup --cluster-name $cluster_name --nodegroup-name $node_group_name \
            --launch-template name=$launch_template_name,version='$Latest' \
            --subnets ${SUBNET_IDS} \
            --node-role ${NODE_ROLE_ARN} \
            --scaling-config minSize=1,maxSize=1,desiredSize=1 > /dev/null
    else   
        echo "already exists"
    fi
    echo "wait for nodegrop to be active"
    aws --region=$region eks wait nodegroup-active --cluster-name $cluster_name --nodegroup-name $node_group_name
}

region=us-west-1
ami_id=''
cleanup_only=''
arg_count=$#

for arg in "$@"
do
   case "$arg" in
        --region=*)
            region="${1#*=}"
            arg_count=$((arg_count-1))
        ;;
        --ami=*)
        ami_id="${1#*=}"
        ;;
        --cleanup)
            cleanup='yes'
        ;;
        --help | -h)
            print_help
        ;;
        -* | --*)
            echo "unknown option ${1}"
            print_help
        ;; 
        *)
            if [[ -z $prefix ]]; then 
                prefix=$arg 
                arg_count=$((arg_count-1))
            else
                echo "Too many arguments"
                print_help
            fi
        ;;
    esac
    shift
done

if [ -z $prefix ];then
    echo "Prefix is equired "
    exit 1
fi

readonly key_pair_name=eks-key
readonly stack_name=${prefix}-eks-vpc-stack
readonly cluster_role=${prefix}AmazonEKSClusterRole
readonly cluster_name=${prefix}-eks-cluster
readonly node_role=${prefix}AmazonEKSNodeRole
readonly launch_template_name=${prefix}-eks-launch-template
readonly node_group_name=${prefix}-eks-node-group

if [ ! -z $cleanup ] && [ $arg_count == 1 ]; then
    do_cleanup
    exit
fi

[[ -z $ami_id ]] && echo "ami is required" && exit 1

ACCOUNT_ID=$(aws sts get-caller-identity --query "Account" --output text)


main

#clean at the end if requested
if [ ! -z $cleanup ]; then 
    do_cleanup
fi
