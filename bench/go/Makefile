.PHONY: run build clean

run:
	go run .

build:
	go build -ldflags="-s -w" -o bench-go .

clean:
	rm -f bench-go
