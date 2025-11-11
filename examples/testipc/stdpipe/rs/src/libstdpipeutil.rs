//! Standard pipe utilities for file descriptor and error handling
//! 
//! This module provides utilities for syscall error handling and stderr messaging
//! with color reset protection.

use anyhow::{Context, Result};
use std::io::{self, Write};
use colored::*;

/// Check a syscall result and return it or convert error to anyhow::Error
/// 
/// This function wraps system call results and converts errors to anyhow errors
/// with context about the syscall name.
/// 
/// # Arguments
/// 
/// * `result` - The syscall result to check
/// * `syscall_name` - Name of the syscall for error messages
/// 
/// # Returns
/// 
/// Returns the result if successful, or an error with syscall context
pub fn check_syscall<T>(result: Result<T, nix::errno::Errno>, syscall_name: &str) -> Result<T> {
    result.with_context(|| format!("{} failed", syscall_name))
}

/// Write a message to stderr with color reset protection
/// 
/// This function writes a message to stderr and ensures proper color reset
/// to prevent color bleeding into subsequent terminal output.
/// 
/// # Arguments
/// 
/// * `message` - The message to write to stderr
pub fn stderr_msg(message: &str) {
    let reset_msg = make_reset_msg(message);
    let _ = io::stderr().write_all(reset_msg.as_bytes());
    let _ = io::stderr().flush();
}

/// Create a message with color reset protection
/// 
/// This function wraps a message with color reset codes to ensure
/// terminal colors are properly reset after the message.
/// 
/// # Arguments
/// 
/// * `message` - The message to wrap with reset codes
/// 
/// # Returns
/// 
/// Returns the message with color reset protection
pub fn make_reset_msg(message: &str) -> String {
    format!("{}\n\x1b[0m", message)
}

/// Parse a strict integer from a string with comprehensive validation
/// 
/// This function parses integers with strict validation, ensuring the input
/// contains only a valid integer without junk characters.
/// 
/// # Arguments
/// 
/// * `input` - The string to parse
/// 
/// # Returns
/// 
/// Returns the parsed integer or an error if invalid
/// 
/// # Errors
/// 
/// Returns an error if:
/// - The string is empty
/// - The string contains non-digit characters (except leading + or -)
/// - The value is out of range for the target type
/// - The string has leading/trailing whitespace or junk
pub fn parse_strict_integer<T>(input: &str) -> Result<T>
where
    T: std::str::FromStr + std::fmt::Display,
    T::Err: std::error::Error + Send + Sync + 'static,
{
    if input.is_empty() {
        return Err(anyhow::anyhow!("Empty string"));
    }

    // Check for whitespace
    if input.chars().any(|c| c.is_whitespace()) {
        return Err(anyhow::anyhow!("String contains whitespace"));
    }

    // Parse the integer
    let value = input.parse::<T>()
        .context("Failed to parse as integer")?;

    // Verify that the parsed value, when converted back to string, matches the input
    // This catches cases like "123abc" where parse() would succeed on "123" but ignore "abc"
    let normalized = value.to_string();
    if input != normalized && input != format!("+{}", normalized) {
        return Err(anyhow::anyhow!("Not normal integer string (junk besides the integer)"));
    }

    Ok(value)
}

/// Color-coded message utilities for different message types
pub mod colors {
    use colored::*;

    /// Create a colored error message (red)
    pub fn error_msg(message: &str) -> String {
        message.red().to_string()
    }

    /// Create a colored warning message (yellow)
    pub fn warning_msg(message: &str) -> String {
        message.yellow().to_string()
    }

    /// Create a colored info message (blue)
    pub fn info_msg(message: &str) -> String {
        message.blue().to_string()
    }

    /// Create a colored success message (green)
    pub fn success_msg(message: &str) -> String {
        message.green().to_string()
    }

    /// Create a colored command message (white on blue background)
    pub fn command_msg(message: &str) -> String {
        message.white().on_blue().to_string()
    }

    /// Create a colored response message (bright white on black background)
    pub fn response_msg(message: &str) -> String {
        message.bright_white().on_black().to_string()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_strict_integer_valid() {
        assert_eq!(parse_strict_integer::<i32>("123").unwrap(), 123);
        assert_eq!(parse_strict_integer::<i32>("-456").unwrap(), -456);
        assert_eq!(parse_strict_integer::<i32>("+789").unwrap(), 789);
        assert_eq!(parse_strict_integer::<u32>("0").unwrap(), 0);
        assert_eq!(parse_strict_integer::<i64>("9223372036854775807").unwrap(), 9223372036854775807);
    }

    #[test]
    fn test_parse_strict_integer_invalid() {
        // Empty string
        assert!(parse_strict_integer::<i32>("").is_err());
        
        // Whitespace
        assert!(parse_strict_integer::<i32>(" 123").is_err());
        assert!(parse_strict_integer::<i32>("123 ").is_err());
        assert!(parse_strict_integer::<i32>("12 3").is_err());
        
        // Junk characters
        assert!(parse_strict_integer::<i32>("123abc").is_err());
        assert!(parse_strict_integer::<i32>("abc123").is_err());
        assert!(parse_strict_integer::<i32>("12.3").is_err());
        
        // Multiple signs
        assert!(parse_strict_integer::<i32>("++123").is_err());
        assert!(parse_strict_integer::<i32>("--123").is_err());
    }

    #[test]
    fn test_make_reset_msg() {
        let msg = make_reset_msg("test message");
        assert!(msg.contains("test message"));
        assert!(msg.ends_with("\x1b[0m"));
    }

    #[test]
    fn test_color_functions() {
        let test_msg = "test";
        
        // Just verify the functions work and return strings
        assert!(!colors::error_msg(test_msg).is_empty());
        assert!(!colors::warning_msg(test_msg).is_empty());
        assert!(!colors::info_msg(test_msg).is_empty());
        assert!(!colors::success_msg(test_msg).is_empty());
        assert!(!colors::command_msg(test_msg).is_empty());
        assert!(!colors::response_msg(test_msg).is_empty());
    }

    #[test]
    fn test_check_syscall() {
        // Test successful case
        let result: Result<i32, nix::errno::Errno> = Ok(42);
        assert_eq!(check_syscall(result, "test").unwrap(), 42);
        
        // Test error case
        let error_result: Result<i32, nix::errno::Errno> = Err(nix::errno::Errno::EINVAL);
        assert!(check_syscall(error_result, "test").is_err());
    }
}