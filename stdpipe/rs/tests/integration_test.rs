use std::process::Command;
use std::env;

#[test]
fn test_pipe_communication() {
    println!("Testing Rust stdpipe_back controlling stdpipe_serv...");
    
    // Get the workspace directory
    let manifest_dir = env::var("CARGO_MANIFEST_DIR").unwrap();
    
    // Build the binaries first
    let build_output = Command::new("cargo")
        .arg("build")
        .current_dir(&manifest_dir)
        .output()
        .expect("Failed to build binaries");
    
    if !build_output.status.success() {
        panic!(
            "Build failed:\nstdout: {}\nstderr: {}", 
            String::from_utf8_lossy(&build_output.stdout),
            String::from_utf8_lossy(&build_output.stderr)
        );
    }
    
    // Run the pipe communication test
    let test_output = Command::new("./target/debug/stdpipe_back")
        .arg("./target/debug/stdpipe_serv")
        .current_dir(&manifest_dir)
        .output()
        .expect("Failed to run pipe communication test");
    
    // Print the output for debugging
    println!("stdout: {}", String::from_utf8_lossy(&test_output.stdout));
    if !test_output.stderr.is_empty() {
        println!("stderr: {}", String::from_utf8_lossy(&test_output.stderr));
    }
    
    // Check that the test completed successfully
    assert!(
        test_output.status.success(), 
        "Pipe communication test failed with exit code: {:?}",
        test_output.status.code()
    );
    
    println!("Test completed!");
}