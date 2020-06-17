## How to check aws regression failure logs

Identify the machine ID that failed from azure kkm pipeline from line similar to

New instance i-01feb8c9e27b4cde3 created

From browser login to aws using url
https://signin.aws.amazon.com/console

You need to get username and password for account running regressions.


Browse EC2 instances from this account.

Locate and select the instance id
from drop down 
Actions->Instance State->Start
wait for a few seconds
connect button will activate
click connect
this will show DNS name of your instance.

use ssh login username/password with

once you login location of the required files is

test script used -- /home/fedora/bin/kkm-test.bash
work directory -- /home/fedora/src/km
log directory -- /home/fedora/src/log

once you are done debugging destroy the instance using dropdown
select the instance id
from drop down 
Actions->Instance State->Terminate
