# Demo-dweb - simple Web server

Based on https://github.com/davidsblog/dweb "A lightweight webserver for C programs, with no dependencies".

Since not all Kontain code is ready (specifically auto-conversion to payloads)  the dweb code is slightly modified:

* added Makefile line to link existing .o files into .km payload
* dropped non-needed pthread_cancel() and `tcsetattr` and use pause() instead of weird waiting for a key press.

This goal is to demo the following:

* Illustrate points about Kontain:
  * Building from the same source `and the same object files`
  * size and start speed of the Kontain VM
  * the fact it's a VM (we should CPU VendorId)
* Show process of packaging in Docker and the run
* [TODO] Show process of pushing to Kubernetes and accessing UI for Payload KM
* [OPTION] Show process debugging payload vs debugging on regular linus app, in Studio - if requested
