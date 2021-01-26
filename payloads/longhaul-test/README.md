# Longhaul test
Simple Go web server accommodating:
* File creation
* Writing to the file 4byte words
* Validating the file with `mmap()`
* Removes file if no error occurred

## MAKEFILE
* `make build` - builds go executable
* `make test` - performs a single request and stops the server
* `make test-long` - performs 300000 sequential requests and stops the server

## Server info
* Currently running on port specified port
* Hitting `localhost:[PORT]/simpleIO?wordCount=10` invokes writes and file validation