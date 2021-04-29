# Work items  for the km and km-releases merge

- [done] drop KM submodule and add actual code (we will lose history but who cares)
- check km-release pipeline and fix it to work directly on the repo.
  - [done] fix install script to install from KM repo, and grep+update all for km-releases reference
  - [done] Generate releases in KM
  - test release from KM
  - validate nightly CI
- update Vagrant boxes and AMI to use new location
- add a line to km-releases readme that it's frozen while we are working on restructuring repo
- move  CodeOfCOnduct upstairs, remove LICENCE, check CONTRIBUTPRS file(s)
- modify km-releases/readme (add link to top-level readme and check for correct URLs)
- grep/check docs referring to km-releases
Next:
- add CI steps for Vagrant/AMI creation

