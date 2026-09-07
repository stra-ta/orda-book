# Fuzzing

The optional `orda_fuzz` target differentially exercises the baseline, pooled,
and ladder engines against the independent reference engine.

The ladder comparison intentionally keeps generated prices inside the ladder's
valid domain.

## Build

The target requires a Clang toolchain with libFuzzer support.

```sh
cmake -S . -B build-fuzz -DLOB_BUILD_FUZZER=ON \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-fuzz --target orda_fuzz --parallel
```

## Run

Run a bounded smoke campaign locally:

```sh
./build-fuzz/orda_fuzz -runs=10000 -seed=305419896
```

The fuzzer maps arbitrary bytes into add, cancel, and modify histories.

## Continuous integration

The Linux CI `fuzz` job (`.github/workflows/ci.yml`) configures with
`clang++`, builds `orda_fuzz`, and runs the same bounded smoke:

```sh
cmake -S . -B build-fuzz -DLOB_BUILD_FUZZER=ON \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
cmake --build build-fuzz --target orda_fuzz --parallel
./build-fuzz/orda_fuzz -runs=10000 -seed=305419896
```

The Apple Clang toolchain on macOS does not ship the libFuzzer runtime
archive, so local macOS runs are not available and Linux CI remains the
required gate for this target.

It compares each event's error, trades, order-level book state, live-order
count, and cumulative statistics.

The target uses bounded valid prices and quantities for the ladder comparison,
while dedicated abuse tests cover invalid inputs and the quantity-overflow
contract.

## Failure handling

The fuzzer traps on the first differential mismatch.

LibFuzzer prints a reproducing input and saves a minimized artifact when run
with a corpus directory.

Use an artifact prefix and a corpus directory so the failure is a structured,
replayable file rather than an untracked terminal fragment:

```sh
mkdir -p fuzz-corpus fuzz-artifacts
./build-fuzz/orda_fuzz fuzz-corpus -runs=100000 \
  -seed=305419896 -artifact_prefix=fuzz-artifacts/
```

Preserve the minimized byte file and its command line in a focused regression
fixture when it exposes a real engine defect.

Preserve a minimized input as a focused regression test when it exposes a real
engine defect.

The fuzz target is opt-in and is not part of the normal CTest suite.
