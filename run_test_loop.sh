#!/bin/bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
LOG_FILE="${LOG_FILE:-test.log}"

debug_build=1
max_tries=20
vmem_build=0
non_linux_dbg_build=0
if [[ "$(uname -s)" == "Linux" ]]; then
    reuseport_default=1
else
    reuseport_default=0
fi
reuseport_build="$reuseport_default"

usage() {
    echo "Usage: $0 [-max-tries <n>] [-dbg <0|1>] [-rp <0|1>] [-nld <0|1>] [-vmem <0|1>] <test-source-path>"
    echo "  -rp   Build with GOC_HTTP_REUSEPORT=1 when 1, otherwise 0. Defaults to $reuseport_default on this platform."
    echo "  -nld  Build with GOC_NON_LINUX_DBG=1 when 1, OFF when 0. Default: 0."
    echo "  -vmem Build with LIBGOC_VMEM=ON when 1, OFF when 0. Default: 0."
    echo "Example: $0 -max-tries 20 -dbg 1 -rp 1 -nld 0 -vmem 0 tests/test_p06_thread_pool.c"
    exit 1
}

if [[ $# -lt 1 ]]; then
    usage
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
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
        -rp)
            shift
            if [[ $# -eq 0 || ! "$1" =~ ^[01]$ ]]; then
                echo "Error: -rp requires 0 or 1."
                usage
            fi
            reuseport_build="$1"
            shift
            ;;
        -nld)
            shift
            if [[ $# -eq 0 || ! "$1" =~ ^[01]$ ]]; then
                echo "Error: -nld requires 0 or 1."
                usage
            fi
            non_linux_dbg_build="$1"
            shift
            ;;
        -vmem)
            shift
            if [[ $# -eq 0 || ! "$1" =~ ^[01]$ ]]; then
                echo "Error: -vmem requires 0 or 1."
                usage
            fi
            vmem_build="$1"
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
rp_arg="-DGOC_HTTP_REUSEPORT=$reuseport_build"
vmem_arg="-DLIBGOC_VMEM=$vmem_build"
nld_arg="-DGOC_NON_LINUX_DBG=$non_linux_dbg_build"

test_name="$(basename "$test_source" .c)"
binary_path="$BUILD_DIR/$test_name"

echo "Rebuilding from scratch in '$BUILD_DIR'..."
rm -rf "$BUILD_DIR"
cmake -S . -B "$BUILD_DIR" -DGOC_ENABLE_STATS=ON $build_type $rp_arg $vmem_arg $nld_arg
cmake --build "$BUILD_DIR" --target "$test_name"

if [[ ! -x "$binary_path" ]]; then
    echo "Built test binary not found: $binary_path"
    exit 1
fi

echo "----------------------------------------------------------------"
echo "| Debug build: $debug_build | reuseport: $reuseport_build | vmem: $vmem_build | non-Linux debug: $non_linux_dbg_build |"
echo "----------------------------------------------------------------"
echo "Running '$test_name' in a loop..."

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
    echo "[$run_end_ts] Run $tests_run/$max_tries: PASSED | ${duration_ms}ms"

    if [[ "$tests_run" -ge "$max_tries" ]]; then
        echo "Completed $tests_run successful tries. Exiting."
        exit 0
    fi
done
