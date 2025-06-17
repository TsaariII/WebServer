#!/bin/bash

# Directory containing test files
TEST_DIR="./tests/"

# Optional: Set to true to show each test being run
VERBOSE=true

# Function to run a single test file
run_test_file() {
    local file="$1"
    local log_file="./test_logs/$(basename "$file").log"

    if [[ ! -x "$file" ]]; then
        echo "Skipping $file (not executable)"
        return
    fi

    if [[ $VERBOSE == true ]]; then
        echo "▶ Running $file"
    fi

    mkdir -p ./test_logs

    # Run the file directly and capture output
    {
        echo "----- Output from $file -----"
        "$file"
        echo "----- End of output for $file -----"
        echo
        echo
    } > "$log_file" 2>&1

    local status=$?

    if [[ $status -ne 0 ]]; then
        echo "❌ Test failed: $file"
        echo "📝 Output:"
        cat "$log_file"
    else
        echo "✅ Test passed: $file"
        if [[ $VERBOSE == true ]]; then
            echo "📝 Output:"
            cat "$log_file"
        fi
    fi
}


# Run all test files in the directory
for file in "$TEST_DIR"/*; do
    [[ -f "$file" ]] && run_test_file "$file"
done

# You can add custom curl calls here for manual testing
echo
echo "👉 Running custom curl checks..."

# Example curl command
# curl -s -o /dev/null -w "%{http_code}\n" http://localhost:8080/health

# Add more curl tests as needed
# curl http://localhost:8080/api/v1/test