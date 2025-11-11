//! Command format encoding/decoding library
//! 
//! This module provides utilities for encoding and decoding commands in different formats
//! for inter-process communication over pipes.

use anyhow::{Context, Result, anyhow};
use std::io::{Read, BufRead, BufReader};

/// Maximum command length (1MB)
pub const MAX_CMD_LEN: usize = 1024 * 1024;

/// Command format enumeration
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum CmdFormat {
    /// Send command as-is with newline (original behavior)
    CmdformatRaw,
    /// Send length;command;END format
    CmdformatV1lenend,
}

/// Encode a command using the specified format
pub fn encode_command(command: &str, format: CmdFormat) -> String {
    match format {
        CmdFormat::CmdformatRaw => encode_raw(command),
        CmdFormat::CmdformatV1lenend => encode_v1lenend(command),
    }
}

/// Decode a command from a reader using the specified format
pub fn decode_command<R: Read>(input: &mut R, format: CmdFormat) -> Result<String> {
    match format {
        CmdFormat::CmdformatRaw => decode_raw(input),
        CmdFormat::CmdformatV1lenend => decode_v1lenend(input),
    }
}

/// Encode command in raw format (just append newline)
fn encode_raw(command: &str) -> String {
    format!("{}\n", command)
}

/// Encode command in v1lenend format (length;command;END)
fn encode_v1lenend(command: &str) -> String {
    let len = command.len();
    format!("{};{};END", len, command)
}

/// Decode command from raw format (read line)
fn decode_raw<R: Read>(input: &mut R) -> Result<String> {
    let mut reader = BufReader::new(input);
    let mut command = String::new();
    
    match reader.read_line(&mut command) {
        Ok(0) => Err(anyhow!("EOF while reading raw command")),
        Ok(_) => {
            // Remove trailing newline
            if command.ends_with('\n') {
                command.pop();
                if command.ends_with('\r') {
                    command.pop();
                }
            }
            Ok(command)
        }
        Err(e) => Err(anyhow!("Failed to read raw command: {}", e)),
    }
}

/// Read exactly the specified number of bytes from input
fn read_exact_bytes<R: Read>(input: &mut R, bytes_to_read: usize) -> Result<Vec<u8>> {
    let mut buffer = vec![0u8; bytes_to_read];
    let mut total_read = 0;
    
    while total_read < bytes_to_read {
        let remaining = bytes_to_read - total_read;
        match input.read(&mut buffer[total_read..total_read + remaining]) {
            Ok(0) => {
                return Err(anyhow!(
                    "Unexpected EOF while reading from stream - got {} bytes, expected {}",
                    total_read, bytes_to_read
                ));
            }
            Ok(bytes_read) => {
                total_read += bytes_read;
            }
            Err(e) => {
                return Err(anyhow!("Stream error while reading: {}", e));
            }
        }
    }
    
    Ok(buffer)
}

/// Decode command from v1lenend format (length;command;END)
fn decode_v1lenend<R: Read>(input: &mut R) -> Result<String> {
    let mut reader = BufReader::new(input);
    
    // Read length
    let mut length_str = String::new();
    let mut byte = [0u8; 1];
    
    // Read until semicolon
    loop {
        reader.read_exact(&mut byte)
            .context("Failed to read command length")?;
        
        if byte[0] == b';' {
            break;
        }
        
        if byte[0].is_ascii_digit() {
            length_str.push(byte[0] as char);
        } else {
            return Err(anyhow!("Invalid character in command length: {}", byte[0]));
        }
    }
    
    if length_str.is_empty() {
        return Err(anyhow!("Empty command length"));
    }
    
    // Parse length
    let cmd_len: usize = length_str.parse()
        .context("Failed to parse command length as integer")?;
    
    if cmd_len > MAX_CMD_LEN {
        return Err(anyhow!("Command length {} exceeds maximum {}", cmd_len, MAX_CMD_LEN));
    }
    
    // Read command data
    let command_bytes = read_exact_bytes(&mut reader, cmd_len)
        .context("Failed to read command data")?;
    
    let command = String::from_utf8(command_bytes)
        .context("Command data is not valid UTF-8")?;
    
    // Read end marker ";END"
    let endmark_bytes = read_exact_bytes(&mut reader, 4)
        .context("Failed to read end marker")?;
    
    let endmark = String::from_utf8(endmark_bytes)
        .context("End marker is not valid UTF-8")?;
    
    if endmark != ";END" {
        return Err(anyhow!("Invalid end marker: expected ';END', got '{}'", endmark));
    }
    
    Ok(command)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    #[test]
    fn test_encode_raw() {
        let command = "ping";
        let encoded = encode_raw(command);
        assert_eq!(encoded, "ping\n");
    }

    #[test]
    fn test_encode_v1lenend() {
        let command = "ping";
        let encoded = encode_v1lenend(command);
        assert_eq!(encoded, "4;ping;END");
    }

    #[test]
    fn test_decode_raw() {
        let input_data = b"ping\n";
        let mut cursor = Cursor::new(input_data);
        let decoded = decode_raw(&mut cursor).unwrap();
        assert_eq!(decoded, "ping");
    }

    #[test]
    fn test_decode_v1lenend() {
        let input_data = b"4;ping;END";
        let mut cursor = Cursor::new(input_data);
        let decoded = decode_v1lenend(&mut cursor).unwrap();
        assert_eq!(decoded, "ping");
    }

    #[test]
    fn test_roundtrip_raw() {
        let original = "test command";
        let encoded = encode_command(original, CmdFormat::CmdformatRaw);
        let mut cursor = Cursor::new(encoded.as_bytes());
        let decoded = decode_command(&mut cursor, CmdFormat::CmdformatRaw).unwrap();
        assert_eq!(original, decoded);
    }

    #[test]
    fn test_roundtrip_v1lenend() {
        let original = "test command";
        let encoded = encode_command(original, CmdFormat::CmdformatV1lenend);
        let mut cursor = Cursor::new(encoded.as_bytes());
        let decoded = decode_command(&mut cursor, CmdFormat::CmdformatV1lenend).unwrap();
        assert_eq!(original, decoded);
    }

    #[test]
    fn test_v1lenend_empty_command() {
        let original = "";
        let encoded = encode_command(original, CmdFormat::CmdformatV1lenend);
        assert_eq!(encoded, "0;;END");
        let mut cursor = Cursor::new(encoded.as_bytes());
        let decoded = decode_command(&mut cursor, CmdFormat::CmdformatV1lenend).unwrap();
        assert_eq!(original, decoded);
    }

    #[test]
    fn test_v1lenend_long_command() {
        let original = "a".repeat(1000);
        let encoded = encode_command(&original, CmdFormat::CmdformatV1lenend);
        let mut cursor = Cursor::new(encoded.as_bytes());
        let decoded = decode_command(&mut cursor, CmdFormat::CmdformatV1lenend).unwrap();
        assert_eq!(original, decoded);
    }
}