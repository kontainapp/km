//  Copyright © 2021 Kontain Inc. All rights reserved.

//  Kontain Inc CONFIDENTIAL

//   This file includes unpublished proprietary source code of Kontain Inc. The
//   copyright notice above does not evidence any actual or intended publication of
//   such source code. Disclosure of this source code or any related proprietary
//   information is strictly prohibited without the express written permission of
//   Kontain Inc.

//   GO webserver with simple IO, before it is fully integrated with .mk files and CI

package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"strconv"
	"sync/atomic"
	"syscall"
	"unsafe"
)

type contextKey struct {
	key string
}

type Flags struct {
	verbose bool
	port    string
	fileNO  int64
}

var ConnContextKey = &contextKey{"http-conn"}

// Saving connection in context signal
func SaveConnInContext(ctx context.Context, c net.Conn) context.Context {
	return context.WithValue(ctx, ConnContextKey, c)
}

// Getting connection from request
func GetConn(r *http.Request) net.Conn {
	return r.Context().Value(ConnContextKey).(net.Conn)
}

// Error loging
func errorPrintAndExit(e error, msg string) {
	if e == nil {
		return
	}
	if msg != "" {
		fmt.Println(msg)
	}
	panic(e)
}

// writing 4 byte integers into testFile, where content of the file are offsets from the begining of the file for each word
func writeToFile(testFilename string, n int) {
	testFile, err := os.OpenFile(testFilename, os.O_RDWR|os.O_CREATE|os.O_TRUNC, 0666)
	errorPrintAndExit(err, "OpenFile failed:")
	wordsAddr := make([]int32, n)
	wordSize := unsafe.Sizeof(wordsAddr[0])
	for i := 0; i < n; i++ {
		wordsAddr[i] = int32(i) * int32(wordSize)
	}
	buf := new(bytes.Buffer)
	errorPrintAndExit(binary.Write(buf, binary.BigEndian, wordsAddr), "Binary.Write failed:")
	for i := 0; i < buf.Len(); i += int(wordSize) {
		_, err = testFile.Write(buf.Bytes()[i : i+int(wordSize)])
		errorPrintAndExit(err, "TestFile.Write failed:")
	}
	testFile.Close()
}

// reading testFile, validating each word is 4*i(bytes) offset from the start from the file
func readAndValidate(testFilename string) {
	testFileRead, err := os.OpenFile(testFilename, os.O_RDONLY, 0444)
	errorPrintAndExit(err, "TestFile open failed:")
	fd := int(testFileRead.Fd())
	stat, err := testFileRead.Stat()
	errorPrintAndExit(err, "Retrieving file stat failed:")
	size := int(stat.Size())
	b, err := syscall.Mmap(fd, 0, size, syscall.PROT_READ, syscall.MAP_SHARED)
	errorPrintAndExit(err, "Mmap failed:")
	for i := 0; i < len(b); i += 4 {
		offset := binary.BigEndian.Uint32(b[i : i+4])
		if int(offset) != i {
			fmt.Printf("Filename: %s\nb[%d:%d] = %d;\ti = %d\n", testFileRead.Name(), i, i+4, offset, i)
			errorPrintAndExit(syscall.Munmap(b), "File mismatch")
		}
	}
	errorPrintAndExit(syscall.Munmap(b), "Unmapping the file")
	testFileRead.Close()
	errorPrintAndExit(os.Remove(testFilename), "Removing file")
}

// Stops the server by hitting http://127.0.0.1/stop
func stopServer(w http.ResponseWriter, req *http.Request) {
	conn := GetConn(req)
	conn.Write([]byte("Shutting down\n"))
	os.Exit(0)
}

// HTTP handler accepting a GET request, following format http:127.0.0.1:[PORT]/simpleIO?wordCount=[WORDCOUNT]
// for curl `http:127.0.0.1:[PORT]/simpleIO\?wordCount\=[WORDCOUNT]`
// performs simple IO for chosen 4 byte words count
func (fg *Flags) simpleIO(w http.ResponseWriter, req *http.Request) {
	if fg.verbose {
		fmt.Printf("Connection received: %s\n", req.RemoteAddr)
	}
	if req.Method == "GET" {
		keys, ok := req.URL.Query()["wordCount"]
		if !ok || len(keys[0]) < 1 || len(keys) != 1 {
			log.Println("Url Param 'wordCount' is missing\nEx: http:127.0.0.1:8090/simpleIO?wordCount=10")
			return
		}
		testFilename := "/tmp/longhaul_test_" + strconv.FormatInt(atomic.AddInt64(&fg.fileNO, 1), 10)
		n, err := strconv.Atoi(keys[0])
		errorPrintAndExit(err, "Invalid argument for wordCount")
		writeToFile(testFilename, n)
		readAndValidate(testFilename)
	}
}

func main() {
	var flags Flags
	flag.StringVar(&flags.port, "port", "15213", "specify port number")
	flag.BoolVar(&flags.verbose, "v", false, "verbose output")
	flag.Parse()
	flags.port = ":" + flags.port
	http.HandleFunc("/simpleIO", flags.simpleIO)
	http.HandleFunc("/stop", stopServer)
	server := http.Server{
		Addr:        flags.port,
		ConnContext: SaveConnInContext,
	}
	server.ListenAndServe()
}
