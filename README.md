# orda-book

[![CI](https://github.com/stra-ta/orda-book/actions/workflows/ci.yml/badge.svg)](https://github.com/stra-ta/orda-book/actions/workflows/ci.yml)

A single-threaded C++17 limit-order matching engine and replay lab.

[Step through one matching example](visualizer/README.md) in the browser, or open the [hosted copy](https://stra-ta.github.io/orda-book/).
The book, the fills, and every chart on it come from the C++ matcher.
It uses example prices and is not a live market chart.

![Order ingress, matching, backends, trades, and evidence](docs/ARCHITECTURE_OVERVIEW.svg)

The baseline backend is the correctness control.
The pooled backend uses stable preallocated slots.
The ladder backend is a bounded price-domain experiment.

| Historical backend | Cross-heavy throughput | Modify-heavy p99 |
| --- | ---: | ---: |
| baseline | 10.2M events/s | 375 ns |
| pooled | 18.3M events/s | 292 ns |
| ladder | 8.12M events/s | 292 ns |

The table is historical evidence from commit `3c79930` on one documented machine.
It is not a claim about the current commit, a production exchange, or a portable latency target.

## Matching boundary

- Price-time priority (FIFO within each price level) as the standard default algorithm
- Cancel-replace loses queue position
- Market and IOC remainders never rest (engine design contract, not a venue claim)
- FOK either fills completely or changes nothing
- Post-only rejects an immediate trade
- No network, persistence, or self-trade policy

Matching semantics describe this engine only.
See [`docs/VENUE_PARITY.md`](docs/VENUE_PARITY.md) for a known venue divergence.

[Build, replay, verify, benchmark, and inspect the limits](GUIDE.md).

- [Correctness campaign](docs/CORRECTNESS_CAMPAIGN.md)
- [Benchmark evidence](docs/BENCHMARK_RESULTS.md)
- [Backend decision](docs/BACKEND_DECISION.md)

## Build

See [GUIDE.md](GUIDE.md) for build presets and dependencies.

## Verification

Functional CI and performance evidence are separate. See [GUIDE.md](GUIDE.md) and `LAB_RULES.md` / `EVIDENCE.md` in `stra-ta/.github` for manifest provenance and the one-command suite (`./scripts/verify.sh` / `./scripts/confidence.sh` or `tools/verify.sh`).

## Limitations

CI is functional only. Performance evidence requires a committed manifest with machine metadata (commit, compiler, kernel, CPU, arch, build type, seed, argv) and a link from the claim to that artifact. See `stra-ta/.github` for lab-wide caveats.

