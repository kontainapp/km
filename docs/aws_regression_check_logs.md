# How to check aws regression failures

## Identify EC2 Instance

Identify the machine ID that failed from azure kkm pipeline from line similar to

`New instance i-01feb8c9e27b4cde3 created`

From browser login to aws using url
https://signin.aws.amazon.com/console

You need to get username and password for account running regressions.

Browse EC2 instances from this account.
Locate and select the instance id.

## Start EC2 instance

From drop down,

_Actions->Instance State->Start_

Wait for a few seconds for machine to start, connect button will activate.

## connect to EC2 instance

Click connect, this will show DNS name of your instance.

use ssh login username/password:

```bash
sshpass -p CMX596al ssh fedora@<IP number>
```

## Once you login location of the required files

Test script used: `/home/fedora/bin/kkm-test.bash` \
Work directory: `/home/fedora/src/km` \
Log directory: `/home/fedora/src/log`

`coredumpctl list` will show km cores if any.

## Running tests

To load KKM module

```bash
sudo insmod /home/fedora/src/km/kkm/kkm/kkm.ko
```

## Shutting down

Once you are done debugging destroy the instance using dropdown on AWS console:

_Actions->Instance State->Terminate_
