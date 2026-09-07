# Backend decision

The project keeps three implementations because they answer different design
questions during the promotion period.

## Decision

| backend | decision | reason |
| --- | --- | --- |
| baseline | retain as correctness control, not a successor candidate | general price domain, simplest control path, easiest oracle |
| pooled | current general-purpose successor candidate | stable slots and no fixed price domain, subject to Linux evidence |
| ladder | retain as a bounded-market experiment | fixed price domain and bitmap discovery, not a universal backend |

The baseline remains the public default.

The pooled backend is the only current general-purpose successor candidate.

The baseline remains available as a differential control and fallback.

The ladder backend should only be selected when the instrument's price domain is
known and the configured bounds are part of the product contract.

No backend is promoted by a single local throughput table.

## Evidence

The allocation campaign measured 0.800752 allocations per event for pooled,
compared with 1.60052 for baseline, on the 500,000-event replay.

The local sweep campaign measured the lowest elapsed time at depth 4,096 for
the ladder backend, while pooled had the lowest p50 at depth 64.

These measurements include timestamp overhead and are not portable production
performance claims.

## Promotion gate

Promote pooled only after it passes the full differential and fuzz campaigns,
then repeat the comparison on Linux with CPU affinity, fixed frequency policy,
multiple rounds, and `perf` counters.

The follow-up must include reject-heavy and modify-heavy workloads, not only
the sweep workload.

The follow-up must also compare memory footprint, correctness campaign cost,
tail distributions, and failure behavior.

Until that gate is met, removing baseline would discard the independent control
needed to interpret pooled changes.

## Deferred evidence

These gate items have no local substitute and stay open until run on the
required machine:

- Bare-metal Linux repeat with CPU affinity, fixed frequency policy,
  multiple rounds, and `perf` counters. Prerequisite: a bare-metal Linux
  host owned by whoever schedules the promotion review; hosted CI runners
  are scheduler-functional evidence only.
- Memory-footprint comparison. Prerequisite: a memory-measurement harness,
  which does not exist yet; the allocation probe counts event-path `new`
  calls only and is not a footprint.
- Failure-behavior comparison. Prerequisite: a failure-injection harness,
  which does not exist yet; allocator failure, crashes, and concurrent
  access are outside every current campaign.

No reject-heavy result may be cited until the `reject-heavy` workload and
its differential oracle coverage both exist and pass.
