# M1 planning

This directory contains dev planning docs (architecture, design) for M1 planning. Work items and open issues are tracked in this file (README.md)

M1 is the release we want to put together by Oct'20.

Requirements and experience are documented in [Kontain Platform Requirements](https://docs.google.com/document/d/1LPeGZEuRdgeGx-fvsZ3Gs8ltYp6xOB7MCk10zFwtpsE/edit#) . Note that we keep  code-related stuff in the code but make requirements/use cases/experience docs in google docs to make it easier to review by folks not on git

Planning doc for work items is located at [google docs](https://docs.google.com/document/d/1B7qhKES-VLhOsUbBuNL6e7GDaKRZ6IVPqaQtGMeoj0s/edit?usp=sharing)

Latest demo readmes are as follows:
* Factory demo:  https://github.com/kontainapp/km/tree/master/demo/faktory/java
  * sequence: create Azure VM. while it is installing , do faktory demo
* Install and first unikernel demo
  * Sequence: Install gcc, chmod 666 /dev/kvm. Then follow https://github.com/kontainapp/km-releases/blob/master/GettingStarted.md "Build + run first unikernel"
