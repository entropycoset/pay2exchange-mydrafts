# StdPipe RS - Rust Implementation

This is a Rust port of the C++ stdpipe programs, implementing inter-process communication using anonymous pipes and file descriptors.

## Overview

The project consists of two main binaries:

1. **stdpipe_serv** - Server that processes commands via file descriptors 3 and 4
2. **stdpipe_back** - Controller that launches and manages the server process

## Library Choices

### `nix` crate (v0.27)
- **Why chosen**: Provides safe Rust wrappers around POSIX system calls
- **Features used**: 
  - `process` - for fork(), exec(), waitpid()
  - `fs` - for pipe(), dup2(), read(), write()
  - `signal` - for process termination
- **Benefits**: Type-safe system programming, comprehensive POSIX API coverage

### `anyhow` crate (v1.0)
- **Why chosen**: Ergonomic error handling for applications
- **Benefits**: Simple error propagation, context attachment, good for CLI programs

## Architecture

### MyFd struct
- Manages file descriptor lifecycle with RAII
- Automatic cleanup on drop
- Ownership tracking to prevent double-close

### MyPipe struct  
- Encapsulates pipe creation and management
- Provides `side_read()` and `side_write()` accessors
- Returns mutable/immutable MyFd references

### StdPipeServer
- Processes commands: "ping" → "pong", "quit" → "goodbye"
- Uses BufReader/BufWriter for efficient I/O
- Comprehensive error handling

### StdPipeController
- Forks child process and sets up FD 3/4 mapping
- Manages bidirectional pipe communication
- Handles process lifecycle and cleanup

## Building

### Using Cargo
```bash
cargo build                 # Debug build
cargo build --release      # Release build
```

### Using Makefile
```bash
make build                  # Debug build
make release               # Release build
make clean                 # Clean artifacts
```

## Running

### Using Makefile
```bash
make test                   # Run full test
make run                    # Run controller
```

### Manual execution
```bash
./target/debug/stdpipe_back ./target/debug/stdpipe_serv
```

## VSCode Integration

The project includes `.vscode/tasks.json` with:
- **Build task**: `Ctrl+Shift+P` → "Tasks: Run Build Task"
- **Test task**: `Ctrl+Shift+P` → "Tasks: Run Test Task"
- Individual binary runners

## Key Differences from C++

1. **Memory Safety**: (TODO in C++ side later easily) Automatic RAII, no manual memory management
2. **Error Handling**: `Result<T, E>` types instead of exceptions
3. **Type Safety**: Strong typing prevents many runtime errors
4. **Ownership**: Borrow checker prevents use-after-free bugs
5. **Concurrency**: Safe by default (though not used in this example)

## Testing

The test demonstrates:
- ✅ Pipe creation and FD mapping
- ✅ Process forking and execution  
- ✅ Bidirectional communication
- ✅ Command processing (ping/pong)
- ✅ Clean process termination
