.PHONY: all build run clean test deps

# Binary name
BINARY=gupt-signaling

# Build flags
GOFLAGS=-v
LDFLAGS=-s -w

all: build

# Download dependencies
deps:
	@echo "Downloading dependencies..."
	go mod download
	go mod tidy

# Build the server
build: deps
	@echo "Building $(BINARY)..."
	go build $(GOFLAGS) -ldflags "$(LDFLAGS)" -o $(BINARY) .

# Run the server
run: build
	@echo "Starting server..."
	./$(BINARY) --port 8080 --timeout 60s

# Run in development mode
dev:
	@echo "Running in development mode..."
	go run *.go --port 8080 --timeout 60s

# Clean build artifacts
clean:
	@echo "Cleaning..."
	rm -f $(BINARY)
	go clean

# Run tests
test:
	@echo "Running tests..."
	go test -v ./...

# Format code
fmt:
	@echo "Formatting code..."
	go fmt ./...

# Lint code
lint:
	@echo "Linting code..."
	golangci-lint run

# Build for multiple platforms
build-all:
	@echo "Building for all platforms..."
	GOOS=linux GOARCH=amd64 go build -o $(BINARY)-linux-amd64 .
	GOOS=darwin GOARCH=amd64 go build -o $(BINARY)-darwin-amd64 .
	GOOS=windows GOARCH=amd64 go build -o $(BINARY)-windows-amd64.exe .

# Install
install: build
	@echo "Installing $(BINARY)..."
	cp $(BINARY) /usr/local/bin/

# Help
help:
	@echo "GuPT Signaling Server Makefile"
	@echo ""
	@echo "Usage:"
	@echo "  make build       Build the server"
	@echo "  make run         Build and run the server"
	@echo "  make dev         Run in development mode"
	@echo "  make clean       Remove build artifacts"
	@echo "  make test        Run tests"
	@echo "  make fmt         Format code"
	@echo "  make lint        Lint code"
	@echo "  make build-all   Build for all platforms"
	@echo "  make install     Install to /usr/local/bin"
	@echo "  make deps        Download dependencies"
	@echo "  make help        Show this help"
