# Venue parity

The engine implements price-time priority as its standard default algorithm:
FIFO within each price level, cancel-replace losing queue position.

That statement describes the engine's own matching rule.

It is not a claim that any production venue behaves the same way in every
edge case.

## Market and IOC remainders

The engine invariant is that a market or IOC order never leaves a resting
remainder, as a design contract of this codebase.

That is an inference from the engine's observed behavior, not a statement
about venue rules.

One known divergence: CBOE Rule 5.34(a)(1)(A)(i) converts a sell market
order to a resting limit order at the minimum increment when the NBB is
zero and the NBO is at or below $0.50 with GTC or GTD persistence.

The engine does not replicate that conversion.

A market order that cannot fully execute here simply leaves no remainder;
it is never rebooked as a resting limit.

## Scope

Do not cite the engine's matching rules as evidence for how a specific
venue handles market remainders, locked or crossed markets, halts, or
self-trade prevention.

Those behaviors require per-venue specifications and are outside the
current contract.
