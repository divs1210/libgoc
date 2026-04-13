#!/bin/bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
LOG_FILE="${LOG_FILE:-test.log}"

num_tests=1
debug_build=1
max_tries=20

usage() {
    echo "Usage: $0 [-test-count <num-tests>] [-max-tries <n>] [-dbg <0|1>] <test-source-path>"
    echo "Example: $0 -test-count 1 -max-tries 20 -dbg 1 tests/test_p06_thread_pool.c"
    exit 1
}

if [[ $# -lt 1 ]]; then
    usage
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        -test-count)
            shift
            if [[ $# -eq 0 || ! "$1" =~ ^[0-9]+$ ]]; then
                echo "Error: -test-count requires a numeric argument."
                usage
            fi
            num_tests="$1"
            shift
            ;;
        -max-tries)
            shift
            if [[ $# -eq 0 || ! "$1" =~ ^[0-9]+$ ]]; then
                echo "Error: -max-tries requires a numeric argument."
                usage
            fi
            max_tries="$1"
            if [[ "$max_tries" -le 0 ]]; then
                echo "Error: -max-tries must be greater than 0."
                usage
            fi
            shift
            ;;
        -dbg)
            shift
            if [[ $# -eq 0 || ! "$1" =~ ^[01]$ ]]; then
                echo "Error: -dbg requires 0 or 1."
                usage
            fi
            debug_build="$1"
            shift
            ;;
        -*)
            echo "Error: Unknown option '$1'."
            usage
            ;;
        *)
            if [[ -n "${test_source:-}" ]]; then
                echo "Error: Multiple test source paths provided."
                usage
            fi
            test_source="$1"
            shift
            ;;
    esac
done

if [[ -z "${test_source:-}" ]]; then
    usage
fi

if [[ ! -f "$test_source" ]]; then
    echo "Test source not found: $test_source"
    exit 1
fi

if [[ "$debug_build" -eq 1 ]]; then
    build_type="-DLIBGOC_DEBUG=ON"
else
    build_type="-DLIBGOC_DEBUG=OFF"
fi

test_name="$(basename "$test_source" .c)"
expected_line="${num_tests}/${num_tests} tests passed"
binary_path="$BUILD_DIR/$test_name"

echo "Rebuilding from scratch in '$BUILD_DIR'..."
rm -rf "$BUILD_DIR"
cmake -S . -B "$BUILD_DIR" -DGOC_ENABLE_STATS=ON $build_type
cmake --build "$BUILD_DIR" --target "$test_name"

if [[ ! -x "$binary_path" ]]; then
    echo "Built test binary not found: $binary_path"
    exit 1
fi

echo "Running '$test_name' in a loop, expecting '$expected_line'..."

tests_run=0
while true; do
    tests_run=$((tests_run + 1))
    run_start_ts=$(date +"%Y-%m-%d %H:%M:%S.%3N")
    run_start_ms=$(date +%s%3N)
    echo "[$run_start_ts] Run $tests_run/$max_tries: ..."

    if ! "$binary_path" 2>&1 | tee "$LOG_FILE" > /dev/null; then
        exit_code=$?
        run_end_ts=$(date +"%Y-%m-%d %H:%M:%S.%3N")
        run_end_ms=$(date +%s%3N)
        duration_ms=$((run_end_ms - run_start_ms))
        echo "[$run_end_ts] Run $tests_run/$max_tries: FAILED | ${duration_ms}ms | (exit code $exit_code)"
        echo "Test did not pass. Exiting."
        exit "$exit_code"
    fi

    run_end_ts=$(date +"%Y-%m-%d %H:%M:%S.%3N")
    run_end_ms=$(date +%s%3N)
    duration_ms=$((run_end_ms - run_start_ms))
    last_line=$(tail -n 1 "$LOG_FILE")

    if [[ "$last_line" != "$expected_line" ]]; then
        echo "[$run_end_ts] Run $tests_run/$max_tries: FAILED | ${duration_ms}ms"
        echo "Test did not pass. Exiting."
        exit 1
    fi

    echo "[$run_end_ts] Run $tests_run/$max_tries: PASSED | ${duration_ms}ms"

    if [[ "$tests_run" -ge "$max_tries" ]]; then
        echo "Completed $tests_run successful tries. Exiting."
        exit 0
    fi
done
