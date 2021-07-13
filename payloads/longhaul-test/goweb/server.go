// Copyright 2021 Kontain Inc
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


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
	"time"
	"unsafe"
)

type fn func(interface{})

func selectedFunction(f fn, val interface{}) {
	f(val)
}

var functionsAsArguments = map[string]fn{
	"mmap": readAndValidateMmap,
	"read": readAndValidateRead,
}

type contextKey struct {
	key string
}

type Flags struct {
	verbose  bool
	port     string
	fileNO   int64
	function string
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
	wordSize := unsafe.Sizeof(wordsAddr[0]) // 4
	for i := 0; i < n; i++ {
		wordsAddr[i] = int32(i) * int32(wordSize)
	}
	buf := new(bytes.Buffer)
	errorPrintAndExit(binary.Write(buf, binary.BigEndian, wordsAddr), "Binary.Write failed:")
	for i := 0; i < buf.Len(); i += int(wordSize) {
		wordSizeWritten, err := testFile.Write(buf.Bytes()[i : i+int(wordSize)])
		// checks if 4 bytes were written to a file
		if wordSizeWritten != 4 {
			log.Fatal("Word write failed to write 4 bytes, wrote ", wordSizeWritten, " instead\nFilename: ", testFilename)
		}
		errorPrintAndExit(err, "TestFile.Write failed:")
	}
	testFile.Close()
}

// reading testFile, validating each word is 4*i(bytes) offset from the start from the file
func readAndValidateMmap(testFilenameArg interface{}) {
	testFilename := fmt.Sprintf("%v", testFilenameArg)
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

// reading testFile, validating each word is 4*i(bytes) offset from the start from the file
func readAndValidateRead(testFilenameArg interface{}) {
	testFilename := fmt.Sprintf("%v", testFilenameArg)
	testFileRead, err := os.OpenFile(testFilename, os.O_RDONLY, 0444)
	errorPrintAndExit(err, "TestFile open failed:")
	stat, err := testFileRead.Stat()
	errorPrintAndExit(err, "Retrieving file stat failed:")
	size := int(stat.Size())
	for i := 0; i < size; i += 4 {
		offset := make([]byte, 4)
		n, err := testFileRead.ReadAt(offset, int64(i))
		if n != 4 {
			log.Fatal("ReadAt failed:\n", err, "\nFilename: ", testFileRead.Name(), "\nExpected ", 4, ", read ", n, "\n")
		}
		offsetInt := binary.BigEndian.Uint32(offset)
		if int(offsetInt) != i {
			log.Fatal("Filename: ", testFileRead.Name(),
				"\nOffset mismatch: expected ", i, ", got ", offsetInt, "\n")
		}
	}
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
		testFilename := "/tmp/longhaul_test_" + strconv.Itoa(os.Getpid()) + "_" + strconv.FormatInt(atomic.AddInt64(&fg.fileNO, 1), 10)
		n, err := strconv.Atoi(keys[0])
		errorPrintAndExit(err, "Invalid argument for wordCount")
		writeToFile(testFilename, n)
		selectedFunction(functionsAsArguments[fg.function], testFilename)
	}
}

func main() {
	var flags Flags
	flag.StringVar(&flags.port, "port", "15213", "specify port number")
	flag.BoolVar(&flags.verbose, "v", false, "verbose output")
	flag.StringVar(&flags.function, "f", "mmap", "select function (mmap,read)")
	flag.Parse()
	flags.port = ":" + flags.port
	http.HandleFunc("/simpleIO", flags.simpleIO)
	http.HandleFunc("/stop", stopServer)
	server := http.Server{
		Addr:         flags.port,
		ConnContext:  SaveConnInContext,
		ReadTimeout:  20 * time.Second,
		WriteTimeout: 20 * time.Second,
	}
	log.Fatal(server.ListenAndServe())
}
