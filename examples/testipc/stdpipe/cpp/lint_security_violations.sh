#!/bin/bash

# Security linting script for stdpipe project
# Detects forbidden catch patterns that could compromise security

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
EXIT_CODE=0

echo "=========================================="
echo "🔒 Security Linter - Forbidden Catch Patterns"
echo "=========================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to check for catch(...) patterns
check_catch_all() {
    echo "Checking for forbidden catch(...) patterns..."
    
    # Find catch(...) patterns
    local temp_file=$(mktemp)
    find "$PROJECT_ROOT" -name "*.cpp" -o -name "*.hpp" -o -name "*.h" | \
        xargs grep -n -B2 "catch\s*(\s*\.\.\.\s*)" 2>/dev/null > "$temp_file" || true
    
    # Filter out allowed cases (those with UNSAFE_LINTER_IGNORE_CATCH_ALL marker)
    local violations=""
    while IFS= read -r line; do
        if [[ "$line" =~ catch.*\.\.\. ]]; then
            # Check if this catch(...) is preceded by the ignore marker
            local line_num=$(echo "$line" | cut -d: -f2)
            local file_path=$(echo "$line" | cut -d: -f1)
            
            # Check 3 lines before for the ignore marker
            local has_marker=false
            for i in {1..3}; do
                local check_line=$((line_num - i))
                if [ $check_line -gt 0 ]; then
                    local marker_check=$(sed -n "${check_line}p" "$file_path" 2>/dev/null | grep "UNSAFE_LINTER_IGNORE_CATCH_ALL" || true)
                    if [ -n "$marker_check" ]; then
                        has_marker=true
                        break
                    fi
                fi
            done
            
            if [ "$has_marker" = false ]; then
                violations="${violations}${line}\n"
            fi
        fi
    done < "$temp_file"
    
    rm -f "$temp_file"
    
    # Count total catch(...) patterns found
    local total_patterns=$(grep -rn "catch\s*(\s*\.\.\.\s*)" "$PROJECT_ROOT" --include="*.cpp" --include="*.hpp" --include="*.h" 2>/dev/null | wc -l || echo 0)
    
    if [ -n "$violations" ]; then
        echo -e "${RED}❌ SECURITY VIOLATION: Found forbidden catch(...) patterns:${NC}"
        echo -e "$violations"
        echo -e "${RED}   → catch(...) can accidentally catch security stop_exception!${NC}"
        echo -e "${YELLOW}   → Use specific exception types instead${NC}"
        echo -e "${YELLOW}   → Or add '// UNSAFE_LINTER_IGNORE_CATCH_ALL' comment above for exceptional cases${NC}"
        EXIT_CODE=1
    else
        if [ "$total_patterns" -gt 0 ]; then
            echo -e "${GREEN}✅ No forbidden catch(...) patterns found${NC}"
            echo -e "${YELLOW}   (some code might ignore checks, git grep source for texts 'UNSAFE_LINTER_IGNORE_CATCH_ALL' and verify the unsafe excluded code)${NC}"
        else
            echo -e "${GREEN}✅ No forbidden catch(...) patterns found${NC}"
        fi
    fi
}

# Function to check for stop_exception catching
check_stop_exception_catch() {
    echo "Checking for forbidden stop_exception catching..."
    
    local matches=$(find "$PROJECT_ROOT" -name "*.cpp" -o -name "*.hpp" -o -name "*.h" | \
        xargs grep -n "catch\s*.*stop_exception" 2>/dev/null || true)
    
    if [ -n "$matches" ]; then
        echo -e "${RED}❌ SECURITY VIOLATION: Found forbidden stop_exception catching:${NC}"
        echo "$matches"
        echo -e "${RED}   → stop_exception must NEVER be caught - it indicates security compromise!${NC}"
        EXIT_CODE=1
    else
        echo -e "${GREEN}✅ No forbidden stop_exception catching found${NC}"
    fi
}

# Function to check for other suspicious patterns
check_suspicious_patterns() {
    echo "Checking for other suspicious security patterns..."
    
    # Check for std::terminate being called inappropriately
    local terminate_matches=$(find "$PROJECT_ROOT" -name "*.cpp" -o -name "*.hpp" -o -name "*.h" | \
        xargs grep -n "std::terminate\s*(" 2>/dev/null || true)
    
    if [ -n "$terminate_matches" ]; then
        echo -e "${YELLOW}⚠️  Found std::terminate calls (review needed):${NC}"
        echo "$terminate_matches"
        echo -e "${YELLOW}   → Ensure terminate is called appropriately for security failures${NC}"
    fi
    
    # Check for abort() calls
    local abort_matches=$(find "$PROJECT_ROOT" -name "*.cpp" -o -name "*.hpp" -o -name "*.h" | \
        xargs grep -n "\babort\s*(" 2>/dev/null || true)
    
    if [ -n "$abort_matches" ]; then
        echo -e "${YELLOW}⚠️  Found abort() calls (review needed):${NC}"
        echo "$abort_matches"
        echo -e "${YELLOW}   → Consider using error_broken() instead for better logging${NC}"
    fi
}

# Main execution
echo "Scanning directory: $PROJECT_ROOT"
echo "------------------------------------------"

check_catch_all
echo
check_stop_exception_catch
echo
check_suspicious_patterns

echo "------------------------------------------"
if [ $EXIT_CODE -eq 0 ]; then
    # Check if we have any ignored patterns
    ignored_count=$(grep -rn "UNSAFE_LINTER_IGNORE_CATCH_ALL" "$PROJECT_ROOT" --include="*.cpp" --include="*.hpp" --include="*.h" 2>/dev/null | wc -l || echo 0)
    
    if [ "$ignored_count" -gt 0 ]; then
        echo -e "${GREEN}🔒 Security linting PASSED - No critical violations found${NC}"
        echo -e "${YELLOW}   (some code might ignore checks, git grep source for texts 'UNSAFE_LINTER_IGNORE_CATCH_ALL' and verify the unsafe excluded code)${NC}"
    else
        echo -e "${GREEN}🔒 Security linting PASSED - No critical violations found${NC}"
    fi
else
    echo -e "${RED}🚫 Security linting FAILED - Critical violations found!${NC}"
    echo -e "${RED}   Fix these security issues before proceeding${NC}"
fi
echo "=========================================="

exit $EXIT_CODE