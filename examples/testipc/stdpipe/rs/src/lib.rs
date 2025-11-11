//! StdPipe RS - Rust Implementation
//!
//! This library provides utilities for inter-process communication using anonymous pipes
//! and file descriptor management.

pub mod envcleaner;
pub mod libcmdformat;
pub mod libstdpipeutil;

pub use envcleaner::*;
pub use libcmdformat::*;
pub use libstdpipeutil::*;