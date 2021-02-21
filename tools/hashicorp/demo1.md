# demo script for dev experience and InstaStart with vagrant

this is a quick helper for a demo arount Vagrant boxes/ dev experience

## Dev experience: Provision

* **We can** Provision Ubuntu vm based on ubuntu box with pre-installed KM and KKM on Mac
  * See tools/hashicorp/readme.md for instructions
  * ~5 min. We will use pre-created box instead

* **We will** use a pre-created box name (kontain/km-ubuntu-2.21.2021 for now)
 * to start pre-created VM box `mkdir; cd ; vagrant init boxname; vagrant up; ssh`
 * to create a box: 'package' on a stopped VM, then add a box with the package name . See Vagrant docs

## Instastart for Spring Boot

* ssh and conduct the spring boot demo  - see demo/spring-boot/readme.md
  * run Kontainer with Spring Boot , wait until it's up and do curl
  * take a snapshot
  * Run Kontain with a snapshot, wait until it's up and do curl

## Dev experience: IDE, Debugging

* Connect VS from Mac to the vagrant VM
* If Needed (note for now: it is in the `kontain/km-ubuntu-2.21.2021` box)
  * install gdb
  * install VSC C++/C++ support
  * add a launch.json   - see below (TODO: add it to km-releases)
 * create test.c file (source from km-releases)
   * /opt/kontain/bin/kontain-gcc -g -o test.km test.c
   * run debug in VC
* create hello_2_loop.c (if we want fancier example)
  * do not forget greatest/greatest.h
* compile and do the same

### launch.json example

```json
{
   // Use IntelliSense to learn about possible attributes.
   // Hover to view descriptions of existing attributes.
   // For more information, visit: https://go.microsoft.com/fwlink/?linkid=830387
   "version": "0.2.0",
   "configurations": [
      {
         "name": "Kontain Unikernel Debug",
         "type": "cppdbg",
         "request": "launch",
         "program": "${workspaceFolder}/${input:tests}",
         "cwd": "/home/vagrant",
         "args": [
            "set debug remote 0"
         ],
         "stopAtEntry": true,
         "miDebuggerServerAddress": "localhost:2159",
         "miDebuggerArgs": "--silent",
         "debugServerPath": "/opt/kontain/bin/km",
         "debugServerArgs": "-g ${workspaceFolder}/${input:tests}",
         "serverLaunchTimeout": 5000,
         "filterStderr": true,
         "filterStdout": true,
         "serverStarted": "GdbServerStubStarted",
         "logging": {
            "moduleLoad": false,
            "trace": false,
            "engineLogging": false,
            "programOutput": true,
            "exceptions": false,
            "traceResponse": false
         },
         "environment": [],
         "externalConsole": false,
         "setupCommands": [
            {
               "description": "Enable pretty-printing for gdb",
               "text": "-enable-pretty-printing",
               "ignoreFailures": true
            }
         ]
      },
   ],
   "inputs": [
      {
         "id": "tests",
         "description": "Test payloads ",
         "default": "test.km",
         "type": "pickString",
         "options": [
            "test.km",
            "hello_2_loops.km"
         ]
      }
   ]
}
```