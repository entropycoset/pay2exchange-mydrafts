use anyhow::{Context, Result};
use std::env;
use std::fs::File;
use std::io::{BufRead, BufReader, BufWriter, Write};
use std::os::fd::FromRawFd;

struct StdPipeServer {
    cmd_in: BufReader<File>,
    cmd_out: BufWriter<File>,
}

impl StdPipeServer {
    fn new(cmd_in_fd: i32, cmd_out_fd: i32) -> Result<Self> {
        eprintln!("Program starting - StdPipe Server initializing with FDs: {}, {}", cmd_in_fd, cmd_out_fd);

        let cmd_in_file = unsafe { File::from_raw_fd(cmd_in_fd) };
        let cmd_out_file = unsafe { File::from_raw_fd(cmd_out_fd) };

        let cmd_in = BufReader::new(cmd_in_file);
        let cmd_out = BufWriter::new(cmd_out_file);

        eprintln!("Program starting - StdPipe Server initialized successfully");

        Ok(StdPipeServer { cmd_in, cmd_out })
    }

    fn send_reply(&mut self, reply: &str) -> Result<()> {
        eprintln!("Program sending reply: {}", reply);
        
        writeln!(self.cmd_out, "{}", reply)
            .context("Failed to write reply to command output pipe")?;
        
        self.cmd_out.flush()
            .context("Failed to flush command output pipe")?;

        Ok(())
    }

    fn main_loop(&mut self) -> Result<()> {
        let mut command = String::new();

        loop {
            eprintln!("Program waiting for command...");
            command.clear();

            match self.cmd_in.read_line(&mut command) {
                Ok(0) => {
                    eprintln!("Program detected end of input - exiting loop");
                    break;
                }
                Ok(_) => {
                    // Remove trailing newline
                    let command = command.trim_end();
                    eprintln!("Program getting a command: '{}'", command);

                    match command {
                        "ping" => {
                            self.send_reply("pong")?;
                        }
                        "quit" => {
                            eprintln!("Program received quit command - exiting loop");
                            self.send_reply("goodbye")?;
                            break;
                        }
                        _ => {
                            eprintln!("Unknown command received: '{}'", command);
                            self.send_reply("command unknown")?;
                        }
                    }
                }
                Err(e) => {
                    eprintln!("Error: Failed to read from command input pipe: {}", e);
                    return Err(anyhow::anyhow!("Failed to read from command input pipe: {}", e));
                }
            }
        }

        eprintln!("Program exiting main loop");
        Ok(())
    }
}

fn main() -> Result<()> {
    let args: Vec<String> = env::args().collect();
    
    if args.len() != 3 {
        eprintln!("Usage: {} <cmd_in_fd> <cmd_out_fd>", args[0]);
        eprintln!("Error: Expected exactly 2 file descriptors for anonymous pipes");
        return Err(anyhow::anyhow!("Invalid command line arguments"));
    }

    let cmd_in_fd: i32 = args[1].parse()
        .context("Failed to parse cmd_in_fd")?;
    let cmd_out_fd: i32 = args[2].parse()
        .context("Failed to parse cmd_out_fd")?;

    if cmd_in_fd < 0 || cmd_out_fd < 0 {
        eprintln!("Error: Invalid file descriptor numbers");
        return Err(anyhow::anyhow!("Invalid file descriptor numbers"));
    }

    let mut server = StdPipeServer::new(cmd_in_fd, cmd_out_fd)?;
    server.main_loop()?;
    
    println!("Program exiting normally");
    Ok(())
}