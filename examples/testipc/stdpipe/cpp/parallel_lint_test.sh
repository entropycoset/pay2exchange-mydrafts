#!/bin/bash
# Parallel Linting Performance Test Script

set -e

echo "🔍 Parallel Linting Performance Test"
echo "===================================="

# Check if we're in a build directory
if [[ ! -f "CMakeCache.txt" ]]; then
    echo "❌ Error: Run this script from your CMake build directory"
    echo "   Example: cd build && ../parallel_lint_test.sh"
    exit 1
fi

# Detect number of cores
if command -v nproc >/dev/null 2>&1; then
    CORES=$(nproc)
elif [[ -f /proc/cpuinfo ]]; then
    CORES=$(grep -c ^processor /proc/cpuinfo)
else
    CORES=4  # fallback
fi

echo "🚀 Detected $CORES CPU cores"
echo ""

# Function to run linting and measure time
run_lint_test() {
    local description="$1"
    local command="$2"
    
    echo "⏱️  Testing: $description"
    echo "   Command: $command"
    
    # Clear any existing .lint files for clean test
    find . -name "*.lint" -delete 2>/dev/null || true
    
    # Run and time the command
    start_time=$(date +%s.%N)
    if eval "$command"; then
        end_time=$(date +%s.%N)
        elapsed=$(echo "$end_time - $start_time" | bc -l 2>/dev/null || python3 -c "print($end_time - $start_time)")
        printf "   ✅ Completed in %.2f seconds\n\n" "$elapsed"
        return 0
    else
        echo "   ❌ Failed"
        echo ""
        return 1
    fi
}

echo "🧪 PERFORMANCE COMPARISON TESTS"
echo "================================"

# Test 1: Single-threaded make
echo "Test 1: Single-threaded execution"
run_lint_test "Single-threaded make" "make -j1 lint"

# Test 2: Multi-threaded make  
echo "Test 2: Multi-threaded execution (all cores)"
run_lint_test "Multi-threaded make -j$CORES" "make -j$CORES lint"

# Test 3: Ninja (if available)
if command -v ninja >/dev/null 2>&1; then
    echo "Test 3: Ninja build system (auto-parallel)"
    run_lint_test "Ninja (auto-detects cores)" "ninja lint"
fi

echo "📊 SUMMARY"
echo "=========="
echo "• Individual .lint files created per source file"
echo "• Each file lints independently = maximum parallelism"
echo "• Use 'make -j\$(nproc) lint' for optimal performance"
echo "• Use 'ninja lint' for automatic core detection"
echo ""

# Show some lint files created
lint_files=$(find . -name "*.lint" | head -5)
if [[ -n "$lint_files" ]]; then
    echo "📄 Sample .lint files created:"
    echo "$lint_files" | while read -r file; do
        echo "   $file"
    done
    echo ""
fi

echo "✅ Parallel linting test completed!"