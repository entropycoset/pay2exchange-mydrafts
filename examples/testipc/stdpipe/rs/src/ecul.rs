//! ECUL - Enhanced C/C++ Unified Logging (Rust Port)
//!
//! This module provides comprehensive logging functionality with colors, error handling,
//! and configurable formatting options, ported from the C++ ECUL library.

use anyhow::Result;
use colored::{Color as ColoredColor, Colorize};
use std::fmt;
use std::io::{self, Write};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::Instant;

/// Color enumeration matching C++ ECUL colors
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Color {
    // Basic colors (0-7)
    Black = 0,
    Red = 1,
    Green = 2,
    Yellow = 3,
    Blue = 4,
    Magenta = 5,
    Cyan = 6,
    White = 7,
    // Light/Bright colors (8-15)
    LightBlack = 8,
    LightRed = 9,
    LightGreen = 10,
    LightYellow = 11,
    LightBlue = 12,
    LightMagenta = 13,
    LightCyan = 14,
    LightWhite = 15,
    // Special values
    Default = -1,
    Reset = -2,
    Normal = -3,
}

impl Color {
    fn to_colored_color(self) -> Option<ColoredColor> {
        match self {
            Color::Black => Some(ColoredColor::Black),
            Color::Red => Some(ColoredColor::Red),
            Color::Green => Some(ColoredColor::Green),
            Color::Yellow => Some(ColoredColor::Yellow),
            Color::Blue => Some(ColoredColor::Blue),
            Color::Magenta => Some(ColoredColor::Magenta),
            Color::Cyan => Some(ColoredColor::Cyan),
            Color::White => Some(ColoredColor::White),
            Color::LightBlack => Some(ColoredColor::BrightBlack),
            Color::LightRed => Some(ColoredColor::BrightRed),
            Color::LightGreen => Some(ColoredColor::BrightGreen),
            Color::LightYellow => Some(ColoredColor::BrightYellow),
            Color::LightBlue => Some(ColoredColor::BrightBlue),
            Color::LightMagenta => Some(ColoredColor::BrightMagenta),
            Color::LightCyan => Some(ColoredColor::BrightCyan),
            Color::LightWhite => Some(ColoredColor::BrightWhite),
            Color::Default | Color::Reset | Color::Normal => None,
        }
    }
}

/// Date format options
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum DateFormat {
    LongDate,    // YYYY-MM-DD
    NoDate,      // no date shown
}

/// Time format options
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum TimeFormat {
    WithSub,     // HH:MM:SS.mmm
    Normal,      // HH:MM:SS
    ShortTime,   // HH:MM
    None,        // no time shown
}

/// Runtime format options
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum RuntimeFormat {
    None,        // no runtime shown
    Seconds,     // show seconds since start
    Ms,          // show milliseconds .000 to .999
    High,        // show 6 digits precision sub-second
}

/// Program name format options
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ProgramNameFormat {
    PreferName,  // show configured manual name, fallback to binary
    PreferBin,   // show bin name, fallback to manual name
    Both,        // show both if possible
}

/// Spacing format options
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SpacingFormat {
    Wide,        // spaces between all components
    Normal,      // skip spaces between major components
    Compact,     // also skip spaces inside components
}

/// Code location information
#[derive(Debug, Clone)]
pub struct CodePlace {
    pub file: &'static str,
    pub line: u32,
}

impl CodePlace {
    pub fn new(file: &'static str, line: u32) -> Self {
        Self { file, line }
    }
    
    pub fn to_string(&self) -> String {
        let filename = std::path::Path::new(self.file)
            .file_name()
            .and_then(|f| f.to_str())
            .unwrap_or(self.file);
        format!("{}:{}", filename, self.line)
    }
}

/// Critical exception that should never be caught (like C++ stop exception)
#[derive(Debug)]
pub struct CriticalStopException {
    pub message: String,
    pub location: CodePlace,
}

impl fmt::Display for CriticalStopException {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "STOP: {} at {}", self.message, self.location.to_string())
    }
}

impl std::error::Error for CriticalStopException {}

/// Global logging settings with thread-safe access
#[derive(Debug, Clone)]
pub struct LogSettings {
    pub date_format: DateFormat,
    pub time_format: TimeFormat,
    pub runtime_format: RuntimeFormat,
    pub program_name_format: ProgramNameFormat,
    pub line_width: i32, // -1 means no setw
    pub spacing_format: SpacingFormat,
    pub program_start_time: Instant,
    
    // Program icon settings
    pub program_icon: String,
    pub program_icon_usecolor: bool,
    pub program_icon_fg: Color,
    pub program_icon_bg: Color,
    pub program_icon_show: bool,
}

impl Default for LogSettings {
    fn default() -> Self {
        Self {
            date_format: DateFormat::LongDate,
            time_format: TimeFormat::WithSub,
            runtime_format: RuntimeFormat::None,
            program_name_format: ProgramNameFormat::PreferName,
            line_width: -1,
            spacing_format: SpacingFormat::Wide,
            program_start_time: Instant::now(),
            program_icon: String::new(),
            program_icon_usecolor: false,
            program_icon_fg: Color::Default,
            program_icon_bg: Color::Default,
            program_icon_show: false,
        }
    }
}

/// Global state for ECUL logging
pub struct EculState {
    settings: Arc<Mutex<LogSettings>>,
    colors_initialized: bool,
    project_name: String,
}

impl EculState {
    fn new() -> Self {
        Self {
            settings: Arc::new(Mutex::new(LogSettings::default())),
            colors_initialized: false,
            project_name: "unknown".to_string(),
        }
    }
}

static ECUL_STATE: OnceLock<Mutex<EculState>> = OnceLock::new();

/// Initialize ECUL logging system - must be called before using logging functions
pub fn init_ecul(project_name: &str) {
    let state = ECUL_STATE.get_or_init(|| {
        let mut state = EculState::new();
        state.project_name = project_name.to_string();
        state.colors_initialized = true; // Rust colored crate handles terminal detection
        Mutex::new(state)
    });
    
    // Reset program start time
    if let Ok(ecul_state) = state.lock() {
        if let Ok(mut settings) = ecul_state.settings.lock() {
            settings.program_start_time = Instant::now();
        }
    }
}

/// Get reference to global log settings
pub fn get_log_settings() -> Arc<Mutex<LogSettings>> {
    let state = ECUL_STATE.get_or_init(|| Mutex::new(EculState::new()));
    if let Ok(ecul_state) = state.lock() {
        Arc::clone(&ecul_state.settings)
    } else {
        Arc::new(Mutex::new(LogSettings::default()))
    }
}

/// Set program icon configuration
pub fn set_program_icon(icon: &str, show: bool, use_color: bool, fg: Color, bg: Color) {
    if let Some(settings) = get_log_settings().lock().ok() {
        let mut settings = settings;
        settings.program_icon = icon.to_string();
        settings.program_icon_show = show;
        settings.program_icon_usecolor = use_color;
        settings.program_icon_fg = fg;
        settings.program_icon_bg = bg;
    }
}

/// Apply colors to text
pub fn colortxt(text: &str, fg: Color, bg: Color) -> String {
    let state = ECUL_STATE.get_or_init(|| Mutex::new(EculState::new()));
    
    let colors_enabled = if let Ok(ecul_state) = state.lock() {
        ecul_state.colors_initialized
    } else {
        false
    };
    
    if !colors_enabled {
        return text.to_string();
    }
    
    let mut colored_text = text.normal();
    
    if let Some(fg_color) = fg.to_colored_color() {
        colored_text = colored_text.color(fg_color);
    }
    
    if let Some(bg_color) = bg.to_colored_color() {
        colored_text = colored_text.on_color(bg_color);
    }
    
    colored_text.to_string()
}

/// Format timestamp according to settings
fn get_formatted_timestamp(settings: &LogSettings) -> String {
    let spacing = if settings.spacing_format == SpacingFormat::Compact { "" } else { " " };
    let mut result = String::new();
    
    // Date part
    if matches!(settings.date_format, DateFormat::LongDate) {
        let now = chrono::Local::now();
        result.push_str(&now.format("%Y-%m-%d").to_string());
        
        // Add space between date and time if both are shown
        if !matches!(settings.time_format, TimeFormat::None) {
            result.push_str(spacing);
        }
    }
    
    // Time part
    if !matches!(settings.time_format, TimeFormat::None) {
        let now = chrono::Local::now();
        let time_str = match settings.time_format {
            TimeFormat::WithSub => now.format("%H:%M:%S%.3f").to_string(),
            TimeFormat::Normal => now.format("%H:%M:%S").to_string(),
            TimeFormat::ShortTime => now.format("%H:%M").to_string(),
            TimeFormat::None => String::new(),
        };
        result.push_str(&time_str);
    }
    
    result
}

/// Format runtime according to settings
fn get_formatted_runtime(settings: &LogSettings) -> String {
    if matches!(settings.runtime_format, RuntimeFormat::None) {
        return String::new();
    }
    
    let elapsed = settings.program_start_time.elapsed();
    
    match settings.runtime_format {
        RuntimeFormat::Seconds => format!("{}s", elapsed.as_secs()),
        RuntimeFormat::Ms => {
            let total_ms = elapsed.as_millis();
            let secs = total_ms / 1000;
            let ms = total_ms % 1000;
            format!("{}.{:03}", secs, ms)
        },
        RuntimeFormat::High => {
            let total_us = elapsed.as_micros();
            let secs = total_us / 1_000_000;
            let us = total_us % 1_000_000;
            format!("{}.{:06}", secs, us)
        },
        RuntimeFormat::None => String::new(),
    }
}

/// Get project name
fn get_project_name() -> String {
    let state = ECUL_STATE.get_or_init(|| Mutex::new(EculState::new()));
    if let Ok(ecul_state) = state.lock() {
        ecul_state.project_name.clone()
    } else {
        "unknown".to_string()
    }
}

/// Get binary name from current executable
fn get_binary_name() -> String {
    std::env::current_exe()
        .ok()
        .and_then(|path| {
            path.file_name()
                .and_then(|name| name.to_str())
                .map(|s| s.to_string())
        })
        .unwrap_or_else(|| "unknown".to_string())
}

/// Format program name according to settings
fn get_formatted_program_name(settings: &LogSettings) -> String {
    let spacing = if settings.spacing_format == SpacingFormat::Compact { "," } else { ", " };
    
    match settings.program_name_format {
        ProgramNameFormat::PreferName => {
            let project_name = get_project_name();
            if project_name != "unknown" {
                project_name
            } else {
                get_binary_name()
            }
        },
        ProgramNameFormat::PreferBin => {
            let bin_name = get_binary_name();
            if bin_name != "unknown" {
                bin_name
            } else {
                get_project_name()
            }
        },
        ProgramNameFormat::Both => {
            let bin_name = get_binary_name();
            let project_name = get_project_name();
            if bin_name != "unknown" && project_name != "unknown" {
                format!("{}{}{}", bin_name, spacing, project_name)
            } else if bin_name != "unknown" {
                bin_name
            } else {
                project_name
            }
        },
    }
}

/// Format complete log message
fn format_log_message(level: &str, message: &str, location: &CodePlace) -> String {
    let settings = if let Ok(settings) = get_log_settings().lock() {
        settings.clone()
    } else {
        LogSettings::default()
    };
    
    let mut result = String::new();
    
    // Determine spacing
    let major_space = match settings.spacing_format {
        SpacingFormat::Normal | SpacingFormat::Compact => "",
        SpacingFormat::Wide => " ",
    };
    
    // Program name part
    let program_part = format!("{{{}}}", get_formatted_program_name(&settings));
    result.push_str(&program_part);
    
    // Timestamp part
    let timestamp = get_formatted_timestamp(&settings);
    if !timestamp.is_empty() {
        result.push_str(major_space);
        result.push_str(&format!("[{}]", timestamp));
    }
    
    // Runtime part
    let runtime = get_formatted_runtime(&settings);
    if !runtime.is_empty() {
        result.push_str(major_space);
        result.push_str(&format!("[{}]", runtime));
    }
    
    // Level part
    result.push_str(major_space);
    result.push_str(&format!("[{}]", level));
    
    // Program icon part - place before location if enabled
    if settings.program_icon_show && !settings.program_icon.is_empty() {
        let mut formatted_icon = format!("[{}]", settings.program_icon);
        if settings.program_icon_usecolor {
            formatted_icon = colortxt(&formatted_icon, settings.program_icon_fg, settings.program_icon_bg);
        }
        result.push_str(major_space);
        result.push_str(&formatted_icon);
    }
    
    // Location part
    result.push_str(major_space);
    result.push_str(&format!("[{}]", location.to_string()));
    
    // Message part
    result.push_str(major_space);
    result.push_str(message);
    
    result
}

/// Output critical message to both stdout and stderr
fn output_critical_message(plain_msg: &str, colored_msg: &str) {
    // First output plain to both streams
    println!("{}", plain_msg);
    eprintln!("{}", plain_msg);
    let _ = io::stdout().flush();
    let _ = io::stderr().flush();
    
    // Then output colored version
    println!("{}", colored_msg);
    eprintln!("{}", colored_msg);
    let _ = io::stdout().flush();
    let _ = io::stderr().flush();
}

/// Log abort message and terminate program
pub fn ecul_log_abort(message: &str, location: CodePlace) -> ! {
    let plain_msg = format_log_message("ABORT", message, &location);
    
    // Create colored version with critical error prefix
    let critical_prefix = colortxt("CRITICAL ERROR! will abort program!", Color::Black, Color::Red);
    let colored_body = colortxt(&format_log_message("ABORT", message, &location), Color::Red, Color::LightYellow);
    let colored_msg = format!("{} {}", critical_prefix, colored_body);
    
    output_critical_message(&plain_msg, &colored_msg);
    
    std::process::abort();
}

/// Log stop message and create stop exception
pub fn ecul_log_stop(message: &str, location: CodePlace) -> CriticalStopException {
    let plain_msg = format_log_message("STOP", message, &location);
    
    // Create colored version with stop error prefix
    let stop_prefix = colortxt("STOP-ERROR, will soon stop program or part...", Color::Magenta, Color::LightYellow);
    let colored_body = colortxt(&format_log_message("STOP", message, &location), Color::Magenta, Color::LightYellow);
    let colored_msg = format!("{} {}", stop_prefix, colored_body);
    
    output_critical_message(&plain_msg, &colored_msg);
    
    CriticalStopException {
        message: message.to_string(),
        location,
    }
}

/// Log error message
pub fn ecul_log_erro(message: &str, location: CodePlace) {
    let formatted = format_log_message("ERRO", message, &location);
    let colored = colortxt(&formatted, Color::Red, Color::Black);
    eprintln!("{}", colored);
    let _ = io::stderr().flush();
}

/// Log warning message
pub fn ecul_log_warn(message: &str, location: CodePlace) {
    let formatted = format_log_message("warn", message, &location);
    let colored = colortxt(&formatted, Color::Yellow, Color::Black);
    eprintln!("{}", colored);
    let _ = io::stderr().flush();
}

/// Log info message
pub fn ecul_log_info(message: &str, location: CodePlace) {
    let formatted = format_log_message("info", message, &location);
    let colored = colortxt(&formatted, Color::Blue, Color::Black);
    eprintln!("{}", colored);
    let _ = io::stderr().flush();
}

/// Macro to get current code location
#[macro_export]
macro_rules! ecul_here {
    () => {
        $crate::ecul::CodePlace::new(file!(), line!())
    };
}

/// Logging macros similar to C++ ECUL
#[macro_export]
macro_rules! ecul_abort {
    ($msg:expr) => {
        $crate::ecul::ecul_log_abort($msg, $crate::ecul_here!())
    };
}

#[macro_export]
macro_rules! ecul_stop {
    ($msg:expr) => {
        Err($crate::ecul::ecul_log_stop($msg, $crate::ecul_here!()))
    };
}

#[macro_export]
macro_rules! ecul_erro {
    ($msg:expr) => {{
        let msg_str = $msg.to_string();
        $crate::ecul::ecul_log_erro(&msg_str, $crate::ecul_here!());
        Err(anyhow::anyhow!(msg_str))
    }};
}

#[macro_export]
macro_rules! ecul_warn {
    ($msg:expr) => {{
        let msg_str = $msg.to_string();
        $crate::ecul::ecul_log_warn(&msg_str, $crate::ecul_here!())
    }};
}

#[macro_export]
macro_rules! ecul_info {
    ($msg:expr) => {{
        let msg_str = $msg.to_string();
        $crate::ecul::ecul_log_info(&msg_str, $crate::ecul_here!())
    }};
}

/// Result type alias that uses CriticalStopException as error
pub type EculResult<T> = std::result::Result<T, CriticalStopException>;

/// Convert anyhow::Result to EculResult by logging error and creating stop exception
pub trait ToEculResult<T> {
    fn to_ecul_result(self, message: &str, location: CodePlace) -> EculResult<T>;
}

impl<T> ToEculResult<T> for Result<T> {
    fn to_ecul_result(self, message: &str, location: CodePlace) -> EculResult<T> {
        match self {
            Ok(value) => Ok(value),
            Err(err) => {
                let full_message = format!("{}: {}", message, err);
                Err(ecul_log_stop(&full_message, location))
            }
        }
    }
}

/// Macro for converting anyhow::Result to EculResult
#[macro_export]
macro_rules! ecul_context {
    ($result:expr, $msg:expr) => {
        $result.to_ecul_result($msg, $crate::ecul_here!())
    };
}