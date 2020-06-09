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

Boot up the instance i-08904ed50d8f229eb
Login to the instance using either km.pem file or user/password.
Update the instance with necessary packages.
Shutdown the image.
From instance Actions->Image start Create Image. This will create a new AMI.
Copy the new AMI ID to cloud/aws/kkm-regression.bash file TEST_AMI variable.
Commit changes to git and this will start using the correct AMI from next regression.
