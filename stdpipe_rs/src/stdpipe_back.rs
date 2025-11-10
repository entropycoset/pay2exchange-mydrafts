use anyhow::{Context, Result};
use nix::sys::wait::{waitpid, WaitStatus};
use nix::unistd::{close, execv, fork, pipe, write, read, ForkResult, Pid};
use std::env;
use std::ffi::CString;
use std::os::unix::io::RawFd;
use std::thread;
use std::time::Duration;

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
    server_pid: Option<Pid>,
}

impl StdPipeController {
    fn new(server_path: &str) -> Result<Self> {
        eprintln!("StdPipeController: Starting server process: {}", server_path);

        let mut controller = StdPipeController {
            cmd_pipe: MyPipe::new(),
            resp_pipe: MyPipe::new(),
            server_pid: None,
        };

        // Create pipes
        controller.cmd_pipe.spawn().context("Failed to create command pipe")?;
        controller.resp_pipe.spawn().context("Failed to create response pipe")?;

        eprintln!("StdPipeController: Created pipes - cmd_pipe[{},{}], resp_pipe[{},{}]",
                  controller.cmd_pipe.side_read().get_fd()?,
                  controller.cmd_pipe.side_write().get_fd()?,
                  controller.resp_pipe.side_read().get_fd()?,
                  controller.resp_pipe.side_write().get_fd()?);

        eprintln!("StdPipeController: Child will use cmd_pipe.read_fd={} for reading commands",
                  controller.cmd_pipe.side_read().get_fd()?);
        eprintln!("StdPipeController: Child will use resp_pipe.write_fd={} for writing responses",
                  controller.resp_pipe.side_write().get_fd()?);

        // Get FD values before forking (for logging purposes)
        let cmd_read_fd = controller.cmd_pipe.side_read().get_fd()?;
        let resp_write_fd = controller.resp_pipe.side_write().get_fd()?;

        // Fork process
        match unsafe { fork() }.context("Failed to fork server process")? {
            ForkResult::Parent { child } => {
                // Parent process
                controller.server_pid = Some(child);
                
                // Close child's ends in parent
                controller.cmd_pipe.side_read_mut().close();
                controller.resp_pipe.side_write_mut().close();

                eprintln!("StdPipeController: Created anonymous pipes - child uses cmd_input_fd={}, resp_output_fd={}",
                         cmd_read_fd, resp_write_fd);

                // Give the child a moment to start
                thread::sleep(Duration::from_millis(100));

                eprintln!("StdPipeController: Server process started successfully");
            }
            ForkResult::Child => {
                // Child process - keep child's pipe ends, close parent's ends
                eprintln!("Child: Using cmd_read_fd={} for reading commands", cmd_read_fd);
                eprintln!("Child: Using resp_write_fd={} for writing responses", resp_write_fd);

                // Close parent's ends in child
                let cmd_write_fd = controller.cmd_pipe.side_write().get_fd()?;
                let resp_read_fd = controller.resp_pipe.side_read().get_fd()?;
                let _ = close(cmd_write_fd);  // Parent writes to this
                let _ = close(resp_read_fd);  // Parent reads from this

                eprintln!("Child: About to exec server with FDs {},{}", cmd_read_fd, resp_write_fd);

                // Execute the server with actual FD numbers as arguments
                let server_path_c = CString::new(server_path)?;
                let arg1 = CString::new(cmd_read_fd.to_string())?;
                let arg2 = CString::new(resp_write_fd.to_string())?;
                
                execv(&server_path_c, &[server_path_c.as_ref(), &arg1, &arg2])
                    .context("Failed to exec server")?;

                unreachable!("execv should not return");
            }
        }

        Ok(controller)
    }

    fn send_command(&mut self, command: &str) -> Result<()> {
        eprintln!("StdPipeController: Sending command: {}", command);
        let cmd_with_newline = format!("{}\n", command);
        
        write(self.cmd_pipe.side_write().get_fd()?, cmd_with_newline.as_bytes())
            .context("Failed to send command to server")?;
            
        Ok(())
    }

    fn read_response(&mut self) -> Result<String> {
        let mut buffer = [0u8; 1024];
        let bytes_read = read(self.resp_pipe.side_read().get_fd()?, &mut buffer)
            .context("Failed to read response from server")?;

        if bytes_read == 0 {
            return Err(anyhow::anyhow!("Server closed response pipe"));
        }

        let mut response = String::from_utf8_lossy(&buffer[..bytes_read]).to_string();
        
        // Remove trailing newline if present
        if response.ends_with('\n') {
            response.pop();
        }

        eprintln!("StdPipeController: Received response: {}", response);
        Ok(response)
    }

    fn run_test(&mut self) -> Result<()> {
        eprintln!("StdPipeController: Starting communication test");

        // Test 1: Send ping, expect pong
        self.send_command("ping")?;
        let response1 = self.read_response()?;
        if response1 != "pong" {
            return Err(anyhow::anyhow!("Expected 'pong' but got: '{}'", response1));
        }
        eprintln!("✓ Ping test passed");

        // Small delay
        thread::sleep(Duration::from_millis(100));

        // Test 2: Send quit
        self.send_command("quit")?;
        let response2 = self.read_response()?;
        if response2 != "goodbye" {
            return Err(anyhow::anyhow!("Expected 'goodbye' but got: '{}'", response2));
        }
        eprintln!("✓ Quit test passed");

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
}

impl Drop for StdPipeController {
    fn drop(&mut self) {
        if let Some(pid) = self.server_pid {
            eprintln!("StdPipeController: Terminating server process");
            let _ = nix::sys::signal::kill(pid, nix::sys::signal::Signal::SIGTERM);
            let _ = waitpid(pid, None);
        }
    }
}

fn main() -> Result<()> {
    eprintln!("StdPipe Backend Controller starting...");

    let args: Vec<String> = env::args().collect();
    let server_path = if args.len() > 1 {
        &args[1]
    } else {
        "./target/debug/stdpipe_serv"
    };

    let mut controller = StdPipeController::new(server_path)?;
    controller.run_test()?;

    eprintln!("StdPipe Backend Controller completed successfully");
    Ok(())
}