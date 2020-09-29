
.PHONY: all
all: bin/faktory

.PHONY: release
release: all

bin/faktory:
	go build -o bin/faktory ./cmd/faktory/... 

.PHONY: clean
clean:
	rm -rf bin

.PHONY: test
test: bin/faktory
	# sudo is required to run tests
	sudo -E env "GOPATH=${GOPATH}" env "GOBIN=${GOBIN}" /usr/local/go/bin/go test -p 1 -count=1 ./tests/...

.DEFAULT:
	@echo $(notdir $(CURDIR)): ignoring target '$@'
