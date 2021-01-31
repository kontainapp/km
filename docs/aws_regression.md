# CI/CD aws hooks

## Notes

Regression test in aws is part of kontainapp.km-kkm test.
After the regression test with kkm on azure is complete, aws kkm test is triggered from templates/kkm.yaml. 

amazon access key id, secret access key as well as instace password are saved in azure pipeline as secret variables.

There are 3 components to this test

1. templates/kkm.yaml - This copies the secret variables to env before running cloud/aws/kkm-regression.bash
2. cloud/aws/kkm-regression.bash - This script does instance management. This script provisions a new aws ec2 instance and copies cloud/aws/kkm-test.bash to the newly creates instance and run the test. Upon successfull completion of test instance is terminated, otherwise the instance is shutdown. we can restart the instance to debug the problem. Instance ID is logged as part of azure pipeline logs.
3. cloud/aws/kkm-test.bash - This script checkout source code, builds and runs the test. Each step logs the output to a separate file in /home/fedora/src/log directory.


## creating new regression AMI

Boot up the instance i-04d5278eda0468447(ami-creation-source-donot-delete)
Login to the instance using either km.pem file or user/password.
Update the instance with necessary packages.
Shutdown the image.
From instance Actions-Image and templates->Create Image.
Add new name for the AMI.(Use the next available number from AMIs page.)
This will create a new AMI.
Copy the new AMI ID to cloud/aws/kkm-regression.bash file TEST_AMI variable.
Commit changes to git and this will start using the correct AMI from next regression.

## Ubuntu 20.04 KKM technology preview image

This AMI is hosted in US West(N.California) us-west-1 region.

To create a new AMI start from source instance i-0433f4b375afbd5cc (ubuntu-20.04-ami-creation-source-donot-delete)
Login to the instance using either pem file or user/password.

Setup github ssh access to kkm repository

git clone git@github.com:kontainapp/kkm.git /root/kkm
git checkout <release-tag>
cd /root/kkm
make

KKM_BIN_DIR=/lib/modules/`uname -r`/kernel/arch/x86/kkm/
mkdir -p \${KKM_BIN_DIR}
cp kkm.ko \${KKM_BIN_DIR}/kkm.ko
depmod -a `uname -r`

Cleanup
rm -fr /root/kkm
delete ssh keys that are setup.


Shutdown the image.
From instance Actions-Image and templates->Create Image.
Add new name for the AMI.(Use the next available number from AMIs page.)
This will create a new AMI.
