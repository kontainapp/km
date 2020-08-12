package main

import (
    "fmt"
    "net/http"
    "os"
)

func main() {
    fmt.Fprintf(os.Stdout, "Starting on localhost:8080 ...\n")
    http.HandleFunc("/", HelloServer)
    http.ListenAndServe(":8080", nil)
}

func HelloServer(w http.ResponseWriter, r *http.Request) {
    fmt.Fprintf(w, "Hello World\n")
}
