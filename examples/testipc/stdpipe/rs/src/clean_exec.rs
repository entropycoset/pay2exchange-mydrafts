use anyhow::{Context, Result};
use nix::sys::wait::{waitpid, WaitStatus};
use nix::unistd::{fork, execv, ForkResult};
use std::env;
use std::ffi::CString;
use std::fs::OpenOptions;
use clap::{Parser, Subcommand};
use stdpipe_rs::envcleaner;

#[derive(Parser)]
#[command(name = "clean_exec")]
#[command(about = "Clean Exec - Environment and File Descriptor Cleanup Tool")]
struct Args {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Run FD management tests
    Tests,
    /// Execute program with cleanup options
    Run {
        /// Comma-separated list of FDs to keep (e.g., '0,1,2')
        #[arg(long)]
        clean_fd_except: Option<String>,
        
        /// Comma-separated list of env vars to keep (e.g., 'HOME,USER')
        #[arg(long)]
        clean_env_except: Option<String>,
        
        /// Comma-separated list of env vars to set (e.g., 'HOME=/tmp,VAR=value')
        #[arg(long)]
        set_env: Option<String>,
        
        /// Program to execute
        program: String,
        
        /// Arguments for the program
        program_args: Vec<String>,
    },
}

struct CleanupOptions {
    allowed_fds: Option<Vec<i32>>,
    keep_env_vars: Option<Vec<String>>,
    set_env_vars: Option<Vec<(String, String)>>,
}

fn parse_fd_list(fd_str: &str) -> Result<Vec<i32>> {
    if fd_str.is_empty() {
        return Ok(Vec::new());
    }
    
    // Check for whitespace
    if fd_str.chars().any(|c| c.is_whitespace()) {
        return Err(anyhow::anyhow!("FD list contains whitespace: '{}'", fd_str));
    }
    
    let mut fds = Vec::new();
    for fd_part in fd_str.split(',') {
        if fd_part.is_empty() {
            return Err(anyhow::anyhow!("Empty FD in list: '{}'", fd_str));
        }
        
        let fd: i32 = fd_part.parse()
            .with_context(|| format!("Failed to parse FD: '{}'", fd_part))?;
        
        if fd < 0 {
            return Err(anyhow::anyhow!("Negative FD not allowed: {}", fd));
        }
        
        fds.push(fd);
    }
    
    Ok(fds)
}

fn parse_string_list(str_list: &str) -> Result<Vec<String>> {
    if str_list.is_empty() {
        return Ok(Vec::new());
    }
    
    // Check for whitespace
    if str_list.chars().any(|c| c.is_whitespace()) {
        return Err(anyhow::anyhow!("String list contains whitespace: '{}'", str_list));
    }
    
    let mut strings = Vec::new();
    for part in str_list.split(',') {
        if part.is_empty() {
            return Err(anyhow::anyhow!("Empty string in list: '{}'", str_list));
        }
        strings.push(part.to_string());
    }
    
    Ok(strings)
}

fn cleanup_child_environment(opts: &CleanupOptions) -> Result<()> {
    // Clean FDs if requested
    if let Some(ref allowed_fds) = opts.allowed_fds {
        eprintln!("Child: Cleaning FDs, keeping: {:?}", allowed_fds);
        let closed = envcleaner::close_all_except_allowed(allowed_fds.clone())
            .context("Failed to close unwanted file descriptors")?;
        eprintln!("Child: Closed {} FDs", closed);
    }
    
    // Clean environment if requested
    if let Some(ref keep_vars) = opts.keep_env_vars {
        eprintln!("Child: Cleaning environment, keeping: {:?}", keep_vars);
        clean_environment(keep_vars)?;
    }
    
    // Set additional environment variables
    if let Some(ref set_vars) = opts.set_env_vars {
        eprintln!("Child: Setting environment variables: {:?}", set_vars);
        set_environment(set_vars);
    }
    
    Ok(())
}

fn clean_environment(keep_vars: &[String]) -> Result<()> {
    // Get all current environment variables
    let all_vars: Vec<String> = env::vars().map(|(k, _)| k).collect();
    
    // Remove all variables not in the keep list
    for var in all_vars {
        if !keep_vars.contains(&var) {
            env::remove_var(&var);
            eprintln!("Child: Removed environment variable: {}", var);
        }
    }
    
    Ok(())
}

fn set_environment(set_vars: &[(String, String)]) {
    for (name, value) in set_vars {
        env::set_var(name, value);
        eprintln!("Child: Set environment variable: {}={}", name, value);
    }
}

fn main_tests() -> Result<()> {
    eprintln!("=== Testing FD Management Functions ===");
    
    // Count initial FDs
    let initial_fds = envcleaner::count_open_fd()
        .context("Failed to count initial open file descriptors")?;
    eprintln!("Initial open FDs: {}", initial_fds);
    
    // Open some extra FDs for testing
    eprintln!("Opening some test FDs...");
    let _test_fd1 = OpenOptions::new().read(true).open("/dev/null")
        .context("Failed to open /dev/null for reading")?;
    let _test_fd2 = OpenOptions::new().write(true).open("/dev/null")
        .context("Failed to open /dev/null for writing")?;
    let _test_fd3 = OpenOptions::new().read(true).open("/dev/zero")
        .context("Failed to open /dev/zero")?;
    
    let after_open_fds = envcleaner::count_open_fd()
        .context("Failed to count FDs after opening test files")?;
    eprintln!("FDs after opening test files: {}", after_open_fds);
    
    // Test FD cleanup (keep only stdin, stdout, stderr)
    let allowed_fds = vec![0, 1, 2];
    eprintln!("Closing all FDs except: {:?}", allowed_fds);
    
    let closed_count = envcleaner::close_all_except_allowed(allowed_fds)
        .context("Failed to close unwanted file descriptors")?;
    
    let final_fds = envcleaner::count_open_fd()
        .context("Failed to count final open file descriptors")?;
    eprintln!("Final open FDs: {}", final_fds);
    eprintln!("Total closed FDs: {}", closed_count);
    
    eprintln!("=== FD Management Test Completed Successfully ===");
    Ok(())
}

fn main_run(
    clean_fd_except: Option<String>,
    clean_env_except: Option<String>,
    set_env: Option<String>,
    program: String,
    program_args: Vec<String>,
) -> Result<()> {
    let mut opts = CleanupOptions {
        allowed_fds: None,
        keep_env_vars: None,
        set_env_vars: None,
    };
    
    // Parse cleanup options
    if let Some(fd_str) = clean_fd_except {
        opts.allowed_fds = Some(parse_fd_list(&fd_str)?);
    }
    
    if let Some(env_str) = clean_env_except {
        opts.keep_env_vars = Some(parse_string_list(&env_str)?);
    }
    
    if let Some(set_str) = set_env {
        opts.set_env_vars = Some(envcleaner::parse_env_pairs(&set_str)?);
    }
    
    eprintln!("CleanExecutor: Executing {}", program);
    for arg in &program_args {
        eprint!(" {}", arg);
    }
    eprintln!();
    
    // Fork and execute with cleanup
    match unsafe { fork() }.context("Failed to fork process")? {
        ForkResult::Parent { child } => {
            // Parent process - wait for child
            match waitpid(child, None).context("Failed to wait for child process")? {
                WaitStatus::Exited(_, exit_code) => {
                    eprintln!("CleanExecutor: Program exited with code {}", exit_code);
                    std::process::exit(exit_code);
                }
                WaitStatus::Signaled(_, signal, _) => {
                    eprintln!("CleanExecutor: Program terminated by signal {:?}", signal);
                    std::process::exit(-1);
                }
                status => {
                    eprintln!("CleanExecutor: Program exited with status: {:?}", status);
                    std::process::exit(-1);
                }
            }
        }
        ForkResult::Child => {
            // Child process - perform cleanup then exec
            if let Err(e) = cleanup_child_environment(&opts) {
                eprintln!("Child cleanup error: {}", e);
                std::process::exit(1);
            }
            
            // Prepare arguments for exec
            let mut argv_exec = vec![CString::new(program.clone())?];
            for arg in &program_args {
                argv_exec.push(CString::new(arg.as_str())?);
            }
            
            let program_c = CString::new(program)?;
            if let Err(e) = execv(&program_c, &argv_exec) {
                eprintln!("Child: execv failed: {}", e);
                std::process::exit(1);
            }
            
            unreachable!("execv should not return");
        }
    }
}

fn print_usage() {
    println!("Clean Exec - Environment and File Descriptor Cleanup Tool\n");
    println!("Usage: clean_exec <command> [options]\n");
    println!("Commands:");
    println!("  tests                       Run FD management tests");
    println!("  run <program> [args...]     Execute program with cleanup options\n");
    println!("Run command options:");
    println!("  --clean-fd-except <fds>     Keep only specified FDs (e.g., '0,1,2')");
    println!("  --clean-env-except <vars>   Keep only specified env vars (e.g., 'HOME,USER')");
    println!("  --set-env <pairs>           Set env vars (e.g., 'HOME=/tmp,VAR=value')\n");
    println!("Examples:");
    println!("  clean_exec tests");
    println!("  clean_exec run /bin/ls -la");
    println!("  clean_exec run --clean-fd-except 0,1,2 /bin/echo hello");
    println!("  clean_exec run --clean-env-except HOME,USER /usr/bin/env\n");
}

fn main() -> Result<()> {
    let args = Args::parse();
    
    match args.command {
        Commands::Tests => {
            main_tests()
        }
        Commands::Run { 
            clean_fd_except, 
            clean_env_except, 
            set_env, 
            program, 
            program_args 
        } => {
            main_run(clean_fd_except, clean_env_except, set_env, program, program_args)
        }
    }
}