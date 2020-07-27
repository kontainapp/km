#!/bin/bash

CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -ldflags '-T 0x201000 -w -extldflags "-no-pie -static -Wl,--gc-sections"' -o server.km server.go
