use anyhow::{Context, Result};
use nix::sys::wait::{waitpid, WaitStatus};
use nix::sys::signal::{kill, Signal};
use nix::unistd::{close, execv, fork, pipe, write, read, ForkResult, Pid};
use nix::fcntl::{fcntl, FcntlArg, OFlag};
use std::env;
use std::ffi::CString;
use std::os::unix::io::{RawFd, AsRawFd};
use std::thread;
use std::time::{Duration, Instant};
use std::io::{self, BufRead, BufReader, Write};
use std::collections::HashMap;
use clap::{Parser, ValueEnum};
use serde_json::Value;
use stdpipe_rs::{envcleaner, libcmdformat, libstdpipeutil};

#[derive(Debug, Clone, ValueEnum)]
enum StdOutErrMode {
    /// Redirect stdout/stderr to /dev/null
    Hide,
    /// Capture stdout/stderr via pipes
    Capture,
    /// Current behavior - inherit parent's stdout/stderr
    Direct,
}

impl Default for StdOutErrMode {
    fn default() -> Self {
        StdOutErrMode::Direct
    }
}

struct MyFd {
    fd: RawFd,
    owned: bool,
}

impl MyFd {
    fn new() -> Self {
        MyFd { fd: -1, owned: false }
    }

    fn set_fd(&mut self, fd: RawFd, owned: bool) -> Result<()> {
        if fd < 0 {
            return Err(anyhow::anyhow!("Invalid fd being set"));
        }
        self.close();
        self.fd = fd;
        self.owned = owned;
        Ok(())
    }

    fn close(&mut self) {
        if self.owned && self.fd >= 0 {
            let _ = close(self.fd);
            self.owned = false;
            self.fd = -1;
        }
    }

    fn is_open(&self) -> bool {
        self.fd >= 0
    }

    fn get_fd(&self) -> Result<RawFd> {
        if !self.is_open() {
            return Err(anyhow::anyhow!("Tried to use invalid/closed FD"));
        }
        Ok(self.fd)
    }
}

impl Drop for MyFd {
    fn drop(&mut self) {
        self.close();
    }
}

struct MyPipe {
    read_side: MyFd,
    write_side: MyFd,
}

impl MyPipe {
    fn new() -> Self {
        MyPipe {
            read_side: MyFd::new(),
            write_side: MyFd::new(),
        }
    }

    fn spawn(&mut self) -> Result<()> {
        let (read_fd, write_fd) = pipe().context("Failed to create pipe")?;
        
        self.read_side.set_fd(read_fd, true)?;
        self.write_side.set_fd(write_fd, true)?;
        
        Ok(())
    }

    fn side_read(&self) -> &MyFd {
        &self.read_side
    }

    fn side_write(&self) -> &MyFd {
        &self.write_side
    }

    fn side_read_mut(&mut self) -> &mut MyFd {
        &mut self.read_side
    }

    fn side_write_mut(&mut self) -> &mut MyFd {
        &mut self.write_side
    }
}

struct StdPipeController {
    cmd_pipe: MyPipe,
    resp_pipe: MyPipe,
    child_stdout_pipe: Option<MyPipe>,
    child_stderr_pipe: Option<MyPipe>,
    server_pid: Option<Pid>,
    stdouterr_mode: StdOutErrMode,
    cmd_format: libcmdformat::CmdFormat,
    accumulated_stdout: String,
    accumulated_stderr: String,
    max_timeout: Duration,
    warn_timeout: Duration,
}

impl StdPipeController {
    fn new(
        server_path: &str,
        cleanup_exec_prog: Option<&str>,
        stdouterr_mode: StdOutErrMode,
        server_args: Vec<String>,
    ) -> Result<Self> {
        eprintln!("StdPipeController: Starting server process: {}", server_path);
        if let Some(cleanup_exec) = cleanup_exec_prog {
            eprintln!("StdPipeController: Using cleanup_exec: {}", cleanup_exec);
        } else {
            eprintln!("StdPipeController: Warning: No cleanup_exec program specified - server will run in current environment");
        }

        let mut controller = StdPipeController {
            cmd_pipe: MyPipe::new(),
            resp_pipe: MyPipe::new(),
            child_stdout_pipe: None,
            child_stderr_pipe: None,
            server_pid: None,
            stdouterr_mode,
            cmd_format: libcmdformat::CmdFormat::CmdformatV1lenend,
            accumulated_stdout: String::new(),
            accumulated_stderr: String::new(),
            max_timeout: Duration::from_secs(5),
            warn_timeout: Duration::from_millis(2500),
        };

        // Create pipes
        controller.cmd_pipe.spawn().context("Failed to create command pipe")?;
        controller.resp_pipe.spawn().context("Failed to create response pipe")?;

        // Create stdout/stderr capture pipes if needed
        if matches!(controller.stdouterr_mode, StdOutErrMode::Capture) {
            let mut stdout_pipe = MyPipe::new();
            let mut stderr_pipe = MyPipe::new();
            
            stdout_pipe.spawn().context("Failed to create stdout capture pipe")?;
            stderr_pipe.spawn().context("Failed to create stderr capture pipe")?;

            // Set non-blocking mode on read ends
            let stdout_fd = stdout_pipe.side_read().get_fd()?;
            let stderr_fd = stderr_pipe.side_read().get_fd()?;
            
            fcntl(stdout_fd, FcntlArg::F_SETFL(OFlag::O_NONBLOCK))?;
            fcntl(stderr_fd, FcntlArg::F_SETFL(OFlag::O_NONBLOCK))?;

            controller.child_stdout_pipe = Some(stdout_pipe);
            controller.child_stderr_pipe = Some(stderr_pipe);

            eprintln!("StdPipeController: Created secure anonymous pipes for stdout/stderr capture");
        }

        eprintln!("StdPipeController: Created pipes - cmd_pipe[{},{}], resp_pipe[{},{}]",
                  controller.cmd_pipe.side_read().get_fd()?,
                  controller.cmd_pipe.side_write().get_fd()?,
                  controller.resp_pipe.side_read().get_fd()?,
                  controller.resp_pipe.side_write().get_fd()?);

        // Get FD values for child process
        let cmd_read_fd = controller.cmd_pipe.side_read().get_fd()?;
        let resp_write_fd = controller.resp_pipe.side_write().get_fd()?;

        eprintln!("StdPipeController: Will pass FDs {} and {} to child process", cmd_read_fd, resp_write_fd);

        // Prepare arguments
        let mut all_args = server_args.clone();
        
        // Add FD arguments based on server type
        if server_args.is_empty() {
            // For stdpipe_serv, use individual FD arguments
            all_args.push(cmd_read_fd.to_string());
            all_args.push(resp_write_fd.to_string());
        } else {
            // For cli_wallet, use --cmd-pipe XXX,YYY format
            all_args.push("--cmd-pipe".to_string());
            all_args.push(format!("{},{}", cmd_read_fd, resp_write_fd));
        }

        // Debug: print all arguments
        eprint!("StdPipeController: Starting with args:");
        for arg in &all_args {
            eprint!(" '{}'", arg);
        }
        eprintln!();

        // Fork and execute
        match unsafe { fork() }.context("Failed to fork server process")? {
            ForkResult::Parent { child } => {
                controller.server_pid = Some(child);
                
                // Close child's ends in parent
                controller.cmd_pipe.side_read_mut().close();
                controller.resp_pipe.side_write_mut().close();

                // Close write ends of capture pipes in parent
                if let Some(ref mut stdout_pipe) = controller.child_stdout_pipe {
                    stdout_pipe.side_write_mut().close();
                }
                if let Some(ref mut stderr_pipe) = controller.child_stderr_pipe {
                    stderr_pipe.side_write_mut().close();
                }

                eprintln!("StdPipeController: Created anonymous pipes - cmd input FD {}, response output FD {}",
                         cmd_read_fd, resp_write_fd);

                // Give child a moment to start
                thread::sleep(Duration::from_millis(100));

                eprintln!("StdPipeController: Server process started successfully");
            }
            ForkResult::Child => {
                // Child process setup
                eprintln!("Child: Using cmd_read_fd={} for reading commands", cmd_read_fd);
                eprintln!("Child: Using resp_write_fd={} for writing responses", resp_write_fd);

                // Handle stdout/stderr redirection
                match controller.stdouterr_mode {
                    StdOutErrMode::Hide => {
                        // Redirect to /dev/null
                        let dev_null = std::fs::OpenOptions::new()
                            .write(true)
                            .open("/dev/null")?;
                        nix::unistd::dup2(dev_null.as_raw_fd(), 1)?;
                        nix::unistd::dup2(dev_null.as_raw_fd(), 2)?;
                    }
                    StdOutErrMode::Capture => {
                        if let (Some(ref stdout_pipe), Some(ref stderr_pipe)) = 
                            (&controller.child_stdout_pipe, &controller.child_stderr_pipe) {
                            nix::unistd::dup2(stdout_pipe.side_write().get_fd()?, 1)?;
                            nix::unistd::dup2(stderr_pipe.side_write().get_fd()?, 2)?;
                        }
                    }
                    StdOutErrMode::Direct => {
                        // Keep inherited stdout/stderr
                    }
                }

                // Clean up file descriptors if cleanup_exec is not used
                if cleanup_exec_prog.is_none() {
                    let allowed_fds = vec![0, 1, 2, cmd_read_fd, resp_write_fd];
                    let closed_count = envcleaner::close_all_except_allowed(allowed_fds)
                        .context("Child: Failed to close non-essential file descriptors")?;
                    eprintln!("Child: Closed {} non-essential FDs", closed_count);
                }

                // Execute the server
                if let Some(cleanup_exec) = cleanup_exec_prog {
                    // Execute via cleanup_exec
                    let mut clean_fd_except = format!("0,1,2,{},{}", cmd_read_fd, resp_write_fd);
                    if matches!(controller.stdouterr_mode, StdOutErrMode::Capture) {
                        if let (Some(ref stdout_pipe), Some(ref stderr_pipe)) = 
                            (&controller.child_stdout_pipe, &controller.child_stderr_pipe) {
                            clean_fd_except += &format!(",{},{}", 
                                stdout_pipe.side_write().get_fd()?,
                                stderr_pipe.side_write().get_fd()?);
                        }
                    }
                    let clean_env_except = "HOME,USER";

                    let mut exec_args = vec![
                        CString::new(cleanup_exec)?,
                        CString::new("--run")?,
                        CString::new("--clean-fd-except")?,
                        CString::new(clean_fd_except)?,
                        CString::new("--clean-env-except")?,
                        CString::new(clean_env_except)?,
                        CString::new(server_path)?,
                    ];

                    for arg in &all_args {
                        exec_args.push(CString::new(arg.as_str())?);
                    }

                    execv(&exec_args[0], &exec_args)?;
                } else {
                    // Direct execution
                    let mut exec_args = vec![CString::new(server_path)?];
                    for arg in &all_args {
                        exec_args.push(CString::new(arg.as_str())?);
                    }

                    execv(&exec_args[0], &exec_args)?;
                }

                unreachable!("execv should not return");
            }
        }

        Ok(controller)
    }

    fn set_timeouts(&mut self, max_timeout_seconds: u64, warn_timeout_millis: Option<u64>) {
        self.max_timeout = Duration::from_secs(max_timeout_seconds);
        self.warn_timeout = Duration::from_millis(
            warn_timeout_millis.unwrap_or(max_timeout_seconds * 500)
        );
    }

    fn send_command(&mut self, command: &str) -> Result<()> {
        let colored_msg = libstdpipeutil::colors::command_msg(
            &format!("StdPipeController: Sending command: {}", command)
        );
        eprintln!("{}", colored_msg);

        let start_time = Instant::now();

        // Use libcmdformat to encode the command
        let formatted_command = libcmdformat::encode_command(command, self.cmd_format);

        eprintln!("{}", libstdpipeutil::colors::info_msg(
            &format!("StdPipeController: Sending RAW: {}", formatted_command)
        ));

        write(self.cmd_pipe.side_write().get_fd()?, formatted_command.as_bytes())
            .context("Failed to send command to server")?;

        let duration = start_time.elapsed();
        if duration >= self.warn_timeout {
            eprintln!("Warning: operation took long sending command - {:.3} seconds", 
                     duration.as_secs_f64());
        }

        eprintln!("StdPipe...: written the command into pipe - done sending.");
        Ok(())
    }

    fn read_response(&mut self) -> Result<String> {
        let start_time = Instant::now();

        println!("StdPipe...: Reading the reply...");

        // Create a temporary file from the FD for libcmdformat
        use std::fs::File;
        use std::os::fd::FromRawFd;
        
        let mut temp_file = unsafe { File::from_raw_fd(self.resp_pipe.side_read().get_fd()?) };
        let decoded_response = libcmdformat::decode_command(&mut temp_file, self.cmd_format)
            .context("Failed to decode response")?;
        
        // Prevent the temp_file from closing the FD when dropped
        std::mem::forget(temp_file);

        println!("StdPipe...: Successfully decoded response of length: {}", decoded_response.len());

        let display_response = if decoded_response.len() > 200 {
            format!("{}...[truncated]", &decoded_response[..200])
        } else {
            decoded_response.clone()
        };

        eprintln!("{}", libstdpipeutil::colors::response_msg(
            &format!("StdPipeController: Decoded response: {}", display_response)
        ));

        let duration = start_time.elapsed();
        if duration >= self.warn_timeout {
            eprintln!("Warning: operation took long reading reply - {:.3} seconds", 
                     duration.as_secs_f64());
        }

        Ok(decoded_response)
    }

    fn send_command_and_read_reply(&mut self, command: &str) -> Result<String> {
        self.handle_child(); // Capture any pending child output before sending
        self.send_command(command)?;
        let response = self.read_response()?;
        self.handle_child(); // Capture any child output after command processing
        Ok(response)
    }

    fn handle_child(&mut self) {
        if !matches!(self.stdouterr_mode, StdOutErrMode::Capture) {
            return;
        }

        let mut buffer = [0u8; 4096];

        // Non-blocking read from child stdout pipe
        if let Some(ref stdout_pipe) = self.child_stdout_pipe {
            if stdout_pipe.side_read().is_open() {
                if let Ok(bytes) = read(stdout_pipe.side_read().get_fd().unwrap_or(-1), &mut buffer) {
                    if bytes > 0 {
                        let data = String::from_utf8_lossy(&buffer[..bytes]);
                        self.accumulated_stdout.push_str(&data);
                    }
                }
            }
        }

        // Non-blocking read from child stderr pipe
        if let Some(ref stderr_pipe) = self.child_stderr_pipe {
            if stderr_pipe.side_read().is_open() {
                if let Ok(bytes) = read(stderr_pipe.side_read().get_fd().unwrap_or(-1), &mut buffer) {
                    if bytes > 0 {
                        let data = String::from_utf8_lossy(&buffer[..bytes]);
                        self.accumulated_stderr.push_str(&data);
                    }
                }
            }
        }
    }

    fn display_and_clear_captured(&mut self) {
        if !matches!(self.stdouterr_mode, StdOutErrMode::Capture) {
            return;
        }

        if !self.accumulated_stdout.is_empty() {
            print!("{}{}", 
                   libstdpipeutil::colors::success_msg("[CHILD STDOUT] "),
                   libstdpipeutil::colors::success_msg(&self.accumulated_stdout));
            if !self.accumulated_stdout.ends_with('\n') {
                println!();
            }
            self.accumulated_stdout.clear();
        }

        if !self.accumulated_stderr.is_empty() {
            print!("{}{}", 
                   libstdpipeutil::colors::error_msg("[CHILD STDERR] "),
                   libstdpipeutil::colors::error_msg(&self.accumulated_stderr));
            if !self.accumulated_stderr.ends_with('\n') {
                println!();
            }
            self.accumulated_stderr.clear();
        }
    }

    fn run_test(&mut self) -> Result<()> {
        eprintln!("StdPipeController: Starting communication test");

        // Test 1: Send ping, expect pong
        let response1 = self.send_command_and_read_reply("ping")?;
        if response1 != "pong" {
            return Err(anyhow::anyhow!("Expected 'pong' but got: '{}'", response1));
        }
        eprintln!("✓ Ping test passed");

        self.display_and_clear_captured();
        thread::sleep(Duration::from_millis(100));

        // Test 2: Send quit
        let response2 = self.send_command_and_read_reply("quit")?;
        if response2 != "goodbye" {
            return Err(anyhow::anyhow!("Expected 'goodbye' but got: '{}'", response2));
        }
        eprintln!("✓ Quit test passed");

        self.display_and_clear_captured();

        // Close our end of the pipes
        self.cmd_pipe.side_write_mut().close();
        self.resp_pipe.side_read_mut().close();

        // Wait for server to exit
        if let Some(pid) = self.server_pid {
            match waitpid(pid, None)? {
                WaitStatus::Exited(_, exit_code) => {
                    if exit_code != 0 {
                        return Err(anyhow::anyhow!("Server process exited with code: {}", exit_code));
                    }
                }
                status => {
                    return Err(anyhow::anyhow!("Server process exited with status: {:?}", status));
                }
            }
        }

        eprintln!("✓ All tests passed successfully");
        Ok(())
    }

    fn run_cli_mode(&mut self) -> Result<()> {
        eprintln!("StdPipeController: Starting CLI interactive mode");
        println!("Interactive CLI mode. Type 'quit', 'abort', or 'abort2' to exit.");

        let stdin = io::stdin();
        let mut reader = stdin.lock();

        loop {
            // Display any accumulated child output before prompt
            self.handle_child();
            self.display_and_clear_captured();

            print!("> ");
            io::stdout().flush()?;

            let mut line = String::new();
            match reader.read_line(&mut line) {
                Ok(0) => {
                    // EOF reached (Ctrl+D)
                    println!("\nEOF reached, sending quit and exiting.");
                    let response = self.send_command_and_read_reply("quit")?;
                    println!("Server response: {}", response);
                    break;
                }
                Ok(_) => {
                    let line = line.trim();
                    
                    if line == "quit" {
                        let response = self.send_command_and_read_reply("quit")?;
                        println!("Server response: {}", response);
                        break;
                    } else if line == "abort" {
                        self.send_command("quit")?;
                        println!("Sent quit command, exiting without waiting for response.");
                        break;
                    } else if line == "abort2" {
                        println!("Exiting immediately without sending quit.");
                        if let Some(pid) = self.server_pid {
                            let _ = kill(pid, Signal::SIGTERM);
                        }
                        break;
                    } else if !line.is_empty() {
                        match self.send_command_and_read_reply(line) {
                            Ok(response) => {
                                println!("Server response: {}", response);
                            }
                            Err(e) => {
                                eprintln!("Error communicating with server: {}", e);
                                break;
                            }
                        }
                    }
                }
                Err(e) => {
                    eprintln!("Error reading input: {}", e);
                    break;
                }
            }
        }

        // Close our end of the pipes
        self.cmd_pipe.side_write_mut().close();
        self.resp_pipe.side_read_mut().close();

        // Wait for server to exit
        if let Some(pid) = self.server_pid {
            let _ = waitpid(pid, None);
        }

        eprintln!("CLI mode completed");
        Ok(())
    }

    fn run_demo_gdgp(&mut self) -> Result<()> {
        eprintln!("StdPipeController: Starting demo mode - get_dynamic_global_properties");

        self.set_timeouts(15, Some(7500));

        // Give cli_wallet time to initialize
        eprintln!("StdPipeController: Waiting for cli_wallet to initialize and connect to RPC...");
        thread::sleep(Duration::from_secs(5));

        // Send get_dynamic_global_properties command
        eprintln!("StdPipeController: Sending get_dynamic_global_properties command...");
        let response = self.send_command_and_read_reply("get_dynamic_global_properties")?;

        // Parse JSON response
        let json_response: Value = serde_json::from_str(&response)
            .context("Failed to parse JSON response")?;

        // Print nicely formatted JSON
        println!("JSON Response (formatted):");
        println!("{}", serde_json::to_string_pretty(&json_response)?);

        // Extract required values
        if let (Some(head_block_number), Some(head_block_id), Some(time)) = (
            json_response["head_block_number"].as_i64(),
            json_response["head_block_id"].as_str(),
            json_response["time"].as_str(),
        ) {
            println!("\nExtracted values:");
            println!("head_block_number: {}", head_block_number);
            println!("head_block_id: {}", head_block_id);
            println!("time: {}", time);
        } else {
            return Err(anyhow::anyhow!("Failed to extract required JSON fields"));
        }

        // Also get global properties
        let response2 = self.send_command_and_read_reply("get_global_properties")?;
        let json_response2: Value = serde_json::from_str(&response2)
            .context("Failed to parse JSON response for get_global_properties")?;

        if let Some(active_witnesses) = json_response2["active_witnesses"].as_array() {
            println!("Active Witnesses:");
            for witness in active_witnesses {
                println!("{}", witness);
            }
        }

        // Send quit command
        eprintln!("StdPipeController: Sending quit command...");
        match self.send_command_and_read_reply("quit") {
            Ok(quit_response) => {
                eprintln!("✓ Demo completed, server response to quit: {}", quit_response);
            }
            Err(_) => {
                eprintln!("✓ Demo completed (quit response not received - wallet closed connection, which is expected)");
            }
        }

        self.display_and_clear_captured();

        // Close our pipes and wait for server
        self.cmd_pipe.side_write_mut().close();
        self.resp_pipe.side_read_mut().close();

        if let Some(pid) = self.server_pid {
            let _ = waitpid(pid, None);
        }

        eprintln!("✓ Demo mode completed successfully");
        Ok(())
    }
}

impl Drop for StdPipeController {
    fn drop(&mut self) {
        if let Some(pid) = self.server_pid {
            eprintln!("StdPipeController: Terminating server process");
            let _ = kill(pid, Signal::SIGTERM);
            let _ = waitpid(pid, None);
        }
    }
}

#[derive(Parser)]
#[command(name = "stdpipe_back")]
#[command(about = "StdPipe Backend Controller")]
struct Args {
    /// Operation mode
    #[arg(value_enum)]
    mode: Mode,

    /// Optional submode string
    #[arg(default_value = "")]
    submode: String,

    /// Child stdout/stderr handling
    #[arg(value_enum, default_value = "direct")]
    stdouterr: StdOutErrMode,

    /// Path to server executable
    #[arg(default_value = "./target/debug/stdpipe_serv")]
    server_path: String,

    /// Optional path to clean_exec program
    cleanup_exec_prog: Option<String>,
}

#[derive(Debug, Clone, ValueEnum)]
enum Mode {
    /// Run automated ping/quit test
    Test,
    /// Demo mode with submodes
    Demo,
    /// Interactive command-line interface
    Cli,
}

fn print_usage() {
    println!("StdPipe Backend Controller\n");
    println!("Usage: stdpipe_back <mode> [submode] [stdouterr] [server_path] [cleanup_exec_prog]\n");
    println!("Arguments:");
    println!("  mode              Operation mode: 'test', 'demo', or 'cli'");
    println!("  submode           Optional submode string (default: empty)");
    println!("  stdouterr         Child stdout/stderr handling: 'direct', 'hide', or 'capture' (default: direct)");
    println!("  server_path       Path to stdpipe_serv executable (default: ./target/debug/stdpipe_serv)");
    println!("  cleanup_exec_prog Optional path to clean_exec program for environment cleanup\n");
    println!("Modes:");
    println!("  test              Run automated ping/quit test (original behavior)");
    println!("  demo              Demo mode with submodes:");
    println!("                    - demo1/gdgp: Send get_dynamic_global_properties, parse JSON, extract values");
    println!("                    - (empty): Same as test mode");
    println!("  cli               Interactive command-line interface\n");
    println!("Examples:");
    println!("  stdpipe_back test                         # Run test mode with defaults");
    println!("  stdpipe_back demo demo1                   # Run demo with get_dynamic_global_properties");
    println!("  stdpipe_back cli                          # Interactive CLI mode");
    println!("  stdpipe_back test \"\" capture              # Test mode with captured child output\n");
}

fn main() -> Result<()> {
    let args = Args::parse();

    eprintln!("StdPipe Backend Controller starting...");
    eprintln!("Mode: {:?}, Submode: '{}', StdOutErr: {:?}", args.mode, args.submode, args.stdouterr);
    eprintln!("Server path: {}", args.server_path);
    if let Some(ref cleanup_exec) = args.cleanup_exec_prog {
        eprintln!("Cleanup exec: {}", cleanup_exec);
    }

    // Determine server path and arguments based on mode
    let (actual_server_path, server_args) = match (&args.mode, args.submode.as_str()) {
        (Mode::Demo, "demo1" | "gdgp") => {
            // Use cli_wallet for demo modes
            let server_path = "/home/joe/work/pay2exchange-core/use/programs/cli_wallet/cli_wallet";
            let server_args = vec![
                "--server-rpc-endpoint=ws://127.0.0.1:1025".to_string(),
                "--chain-id".to_string(),
                "08140cebb7723d4f8ffc6ce380b5dbb4c8cdd51b4e3c644d52af1918098a7643".to_string(),
                "--mutelog".to_string(),
            ];
            (server_path.to_string(), server_args)
        }
        _ => {
            // Use stdpipe_serv for other modes
            (args.server_path.clone(), Vec::new())
        }
    };

    let mut controller = StdPipeController::new(
        &actual_server_path,
        args.cleanup_exec_prog.as_deref(),
        args.stdouterr,
        server_args,
    )?;

    match args.mode {
        Mode::Test => {
            controller.run_test()?;
        }
        Mode::Demo => {
            match args.submode.as_str() {
                "demo1" | "gdgp" => {
                    controller.run_demo_gdgp()?;
                }
                _ => {
                    controller.run_test()?; // Default demo acts like test
                }
            }
        }
        Mode::Cli => {
            controller.run_cli_mode()?;
        }
    }

    eprintln!("StdPipe Backend Controller completed successfully");
    Ok(())
}