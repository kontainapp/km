
.PHONY: all
all: bin/faktory

bin/faktory:
	go build -o bin/faktory ./cmd/faktory/... 

.PHONY: clean
clean:
	rm -rf bin

.PHONY: test
test: bin/faktory
	go test ./tests/...