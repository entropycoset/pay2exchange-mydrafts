use anyhow::{Context, Result};
use std::env;
use std::fs::File;
use std::io::{Write};
use std::os::fd::FromRawFd;
use std::thread;
use std::time::Duration;
use stdpipe_rs::{libcmdformat, libstdpipeutil};

struct StdPipeServer {
    cmd_in_file: File,
    cmd_out_file: File,
    cmd_format: libcmdformat::CmdFormat,
}

impl StdPipeServer {
    fn new(cmd_in_fd: i32, cmd_out_fd: i32) -> Result<Self> {
        libstdpipeutil::stderr_msg(&format!(
            "Program starting - StdPipe Server initializing with FDs: {}, {}",
            cmd_in_fd, cmd_out_fd
        ));

        let cmd_in_file = unsafe { File::from_raw_fd(cmd_in_fd) };
        let cmd_out_file = unsafe { File::from_raw_fd(cmd_out_fd) };

        libstdpipeutil::stderr_msg("Program starting - StdPipe Server initialized successfully");

        Ok(StdPipeServer {
            cmd_in_file,
            cmd_out_file,
            cmd_format: libcmdformat::CmdFormat::CmdformatV1lenend, // Use v1lenend format
        })
    }

    fn send_reply(&mut self, reply: &str) -> Result<()> {
        libstdpipeutil::stderr_msg(&format!("Program sending reply: {}", reply));

        // Use libcmdformat to encode the reply
        let formatted_reply = libcmdformat::encode_command(reply, self.cmd_format);

        self.cmd_out_file
            .write_all(formatted_reply.as_bytes())
            .context("Failed to write reply to command output pipe")?;

        self.cmd_out_file
            .flush()
            .context("Failed to flush command output pipe")?;

        Ok(())
    }

    fn main_loop(&mut self) -> Result<()> {
        loop {
            libstdpipeutil::stderr_msg("Program waiting for command...");

            let command = match libcmdformat::decode_command(&mut self.cmd_in_file, self.cmd_format) {
                Ok(cmd) => cmd,
                Err(e) => {
                    libstdpipeutil::stderr_msg(&format!("Error reading/decoding command: {}", e));
                    libstdpipeutil::stderr_msg("Program detected end of input - exiting loop");
                    break;
                }
            };

            libstdpipeutil::stderr_msg(&format!("Program getting a command: '{}'", command));

            if command == "ping" {
                self.send_reply("pong")?;
            } else if command == "quit" {
                libstdpipeutil::stderr_msg("Program received quit command - exiting loop");
                self.send_reply("goodbye")?;
                break;
            } else if command.starts_with("sleep ") {
                // Handle "sleep N" command where N is milliseconds
                let ms_str = &command[6..];
                if ms_str.is_empty() {
                    self.send_reply("sleep command requires milliseconds parameter")?;
                    continue;
                }

                match libstdpipeutil::parse_strict_integer::<u32>(ms_str) {
                    Ok(milliseconds) => {
                        // Limit maximum sleep to prevent abuse (10 seconds = 10000ms)
                        if milliseconds > 10000 {
                            self.send_reply("sleep duration limited to 10000 milliseconds maximum")?;
                            continue;
                        }

                        libstdpipeutil::stderr_msg(&format!(
                            "Program sleeping for {} milliseconds",
                            milliseconds
                        ));
                        thread::sleep(Duration::from_millis(milliseconds as u64));
                        libstdpipeutil::stderr_msg("Program finished sleeping");

                        self.send_reply(&format!("slept {} ms", milliseconds))?;
                    }
                    Err(e) => {
                        libstdpipeutil::stderr_msg(&format!("Error parsing sleep parameter: {}", e));
                        self.send_reply(&format!("invalid sleep parameter: {}", e))?;
                    }
                }
            } else {
                libstdpipeutil::stderr_msg(&format!("Unknown command received: '{}'", command));
                self.send_reply("command unknown")?;
            }
        }

        libstdpipeutil::stderr_msg("Program exiting main loop");
        Ok(())
    }
}

fn print_usage(program_name: &str) {
    println!("StdPipe Server\n");
    println!("Usage: {} <cmd_in_fd> <cmd_out_fd>\n", program_name);
    println!("Arguments:");
    println!("  cmd_in_fd    File descriptor number for command input pipe (required)");
    println!("  cmd_out_fd   File descriptor number for command output pipe (required)\n");
    println!("Description:");
    println!("  Server process that communicates via anonymous pipes using the provided");
    println!("  file descriptors. Typically spawned by stdpipe_back controller.\n");
    println!("Commands:");
    println!("  ping         Responds with 'pong'");
    println!("  sleep N      Sleeps for N milliseconds (max 10000) then responds");
    println!("  quit         Responds with 'goodbye' and exits");
    println!("  <unknown>    Responds with 'command unknown'\n");
    println!("Note:");
    println!("  This program is usually not run directly but launched by stdpipe_back");
    println!("  which creates the pipes and passes the file descriptor numbers.\n");
}

fn main() -> Result<()> {
    let args: Vec<String> = env::args().collect();

    // Check for --help
    if args.len() > 1 && args[1] == "--help" {
        print_usage(&args[0]);
        return Ok(());
    }

    // Expect command line arguments for the two pipe file descriptors
    if args.len() != 3 {
        print_usage(&args[0]);
        eprintln!("Error: Expected exactly 2 file descriptors for anonymous pipes");
        return Err(anyhow::anyhow!("Invalid command line arguments"));
    }

    let cmd_in_fd = libstdpipeutil::parse_strict_integer::<i32>(&args[1])
        .context("Failed to parse cmd_in_fd")?;
    let cmd_out_fd = libstdpipeutil::parse_strict_integer::<i32>(&args[2])
        .context("Failed to parse cmd_out_fd")?;

    libstdpipeutil::stderr_msg(&format!(
        "Will talk CMD on: cmd-in fd {}, cmd-out fd {}",
        cmd_in_fd, cmd_out_fd
    ));

    if cmd_in_fd < 0 || cmd_out_fd < 0 {
        libstdpipeutil::stderr_msg("Error: Invalid file descriptor numbers");
        return Err(anyhow::anyhow!("Invalid file descriptor numbers"));
    }

    let mut server = StdPipeServer::new(cmd_in_fd, cmd_out_fd)?;
    server.main_loop()?;

    println!("{}", libstdpipeutil::make_reset_msg("Program exiting normally"));
    Ok(())
}