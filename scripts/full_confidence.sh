#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work_dir=${1:-"${repo_root}/build-confidence"}
debug_dir="${work_dir}/debug"
san_dir="${work_dir}/sanitized"
fuzz_dir="${work_dir}/fuzz"
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/orda-confidence.XXXXXX")
trap 'rmdir "$tmp_dir" 2>/dev/null || true' EXIT

cmake -S "$repo_root" -B "$debug_dir" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$debug_dir" --parallel
ctest --test-dir "$debug_dir" --output-on-failure

cmake -S "$repo_root" -B "$san_dir" -DCMAKE_BUILD_TYPE=Debug \
  -DLOB_ENABLE_ASAN=ON -DLOB_ENABLE_UBSAN=ON
cmake --build "$san_dir" --parallel
ctest --test-dir "$san_dir" --output-on-failure

"$debug_dir/orda_benchmark" --events 64 --workload cross-heavy --rounds 1 \
  --no-latency --write-binary-file "$tmp_dir/events.bin" >/dev/null
"$debug_dir/orda_replay" "$tmp_dir/events.bin" --binary --top >/dev/null

fuzzer_runtime=$(clang++ -print-file-name="libclang_rt.fuzzer_${CMAKE_SYSTEM_PROCESSOR:-x86_64}.a" 2>/dev/null || true)
if [[ -n "$fuzzer_runtime" && -f "$fuzzer_runtime" ]]; then
  cmake -S "$repo_root" -B "$fuzz_dir" -DLOB_BUILD_FUZZER=ON \
    -DCMAKE_CXX_COMPILER=clang++
  cmake --build "$fuzz_dir" --target orda_fuzz --parallel
  "$fuzz_dir/orda_fuzz" -runs=10000 -seed=305419896
else
  echo "fuzzer smoke: unavailable in local Clang runtime; CI remains required" >&2
fi

(cd "$repo_root" && ./scripts/check_evidence.sh)
