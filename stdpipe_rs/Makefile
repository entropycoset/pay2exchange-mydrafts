.PHONY: build clean test run help

# Default target
all: build

# Build both binaries
build:
	@echo "Building Rust binaries..."
	cargo build

# Build in release mode
release:
	@echo "Building Rust binaries in release mode..."
	cargo build --release

# Run tests
test: build
	@echo "Running pipe communication test..."
	./simple_test.sh

# Run stdpipe_back (controller)
run: build
	@echo "Running stdpipe_back..."
	./target/debug/stdpipe_back ./target/debug/stdpipe_serv

# Run individual binaries for testing
run-serv: build
	@echo "Running stdpipe_serv with FD 3,4..."
	./target/debug/stdpipe_serv 3 4

run-back: build
	@echo "Running stdpipe_back..."
	./target/debug/stdpipe_back ./target/debug/stdpipe_serv

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	cargo clean

# Check code without building
check:
	@echo "Checking Rust code..."
	cargo check

# Format code
fmt:
	@echo "Formatting Rust code..."
	cargo fmt

# Run clippy lints
clippy:
	@echo "Running Clippy lints..."
	cargo clippy

# Install dependencies
deps:
	@echo "Installing dependencies..."
	cargo fetch

# Help target
help:
	@echo "Available targets:"
	@echo "  build    - Build debug binaries (default)"
	@echo "  release  - Build release binaries"
	@echo "  test     - Run pipe communication test"
	@echo "  run      - Run stdpipe_back with stdpipe_serv"
	@echo "  run-serv - Run stdpipe_serv standalone"
	@echo "  run-back - Run stdpipe_back standalone"
	@echo "  clean    - Clean build artifacts"
	@echo "  check    - Check code without building"
	@echo "  fmt      - Format code"
	@echo "  clippy   - Run clippy lints"
	@echo "  deps     - Install dependencies"
	@echo "  help     - Show this help message"