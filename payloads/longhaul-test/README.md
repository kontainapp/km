# Longhaul test
Simple Go web server accommodating:
* File creation
* Writing to the file 4byte words
* Validating the file with `mmap` or `read`
* Removes file if no error occurred

## MAKEFILE
* `make build` - builds go executable
* `make test` - performs a single request and stops the server
* `make test-start` - starts the server and listens on `PORT`
* `make test-start-v` - does `test-start` with `--km-log-to=km_logs.log`
* `make test-long` - performs 300000 sequential requests and stops the server
* `make force-stop` - stops the server

Flags
* `PORT` - set custom port (default: 15213)
* `WORDCOUNT` - set custom word count to write to files (default: 10)
* `VERBOSE` - displaying all requests as they come (default: false)
* `FUNCTION` - set a function to read and validate files `read` or `mmap` (default: `mmap`)

## Server info
* Currently running on port `15213` can be changed
* Hitting `localhost:[PORT]/simpleIO?wordCount=10` invokes writes and file validation

## Testing with wrk2
1. `git clone git@github.com:giltene/wrk2.git`
2. Run longhaul-test as `make test-start` (`km/payloads/longhaul-test`)
3. `cd wrk2`, `./wrk -t5 -c25 -d18000s -R100 http://127.0.0.1:15213/simpleIO\?wordCount\=10` - runs 5 threads with 25 connections, for 5h