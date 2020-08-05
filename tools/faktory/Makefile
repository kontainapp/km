
.PHONY: all
all: bin/faktory

bin/faktory:
	go build -o bin/faktory ./cmd/faktory/... 

.PHONY: clean
clean:
	rm -rf bin

.PHONY: test
test: bin/faktory
	# sudo is required to run tests
	sudo -E env "GOPATH=${GOPATH}" env "GOBIN=${GOBIN}" /usr/local/go/bin/go test -count=1 -v ./tests/...