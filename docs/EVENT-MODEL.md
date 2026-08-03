# Event Model

## Status

This is a design contract for future work. PalCenter Companion v0.1.0 does not produce gameplay events.

The Companion is intended to be an authoritative event source. Events should describe what the Palworld server observed rather than require PalCenter to reconstruct behavior from positional telemetry.

## Envelope

Every future event should contain:

| Field | Purpose |
| --- | --- |
| `eventId` | Deterministic identifier used for deduplication. |
| `timestamp` | UTC RFC 3339 time at which the server observation occurred. |
| `eventType` | Versioned event name. |
| `player` | Minimal player identity associated with the event, or `null`. |
| `coordinateSpace` | Space in which any coordinates have meaning. |
| `confidence` | Evidence quality; authoritative hooks should normally report `authoritative`. |
| `metadata` | Event-specific, versioned values. |
| `evidence` | Safe provenance explaining which authoritative observation produced the event. |

See [examples/event-envelope.json](../examples/event-envelope.json) for a non-gameplay placeholder.

## Deterministic identifiers

Event IDs must remain stable when the same underlying observation is replayed after reconnect or restart. The intended algorithm will:

1. define a canonical event identity tuple for each event type;
2. normalize tuple values without mutable display data;
3. serialize them in a documented canonical order;
4. hash the canonical representation with a versioned algorithm and namespace.

Random UUIDs are unsuitable for replay deduplication. The final canonicalization and hash algorithm will be specified before event implementation. No guessed event identity scheme is treated as stable in v0.1.0.

## Player identity

The `player` object should use stable server-provided identifiers where available. Display names are mutable and must not be used as the sole identity key. Contracts must distinguish player, platform user, and display identities and must not expose more personal information than the event requires.

## Confidence and evidence

`confidence` communicates whether information is directly observed or derived. Initial vocabulary is expected to include `authoritative` and `unknown`; adding inferred confidence levels requires an explicit contract update.

`evidence` must describe safe provenance without including credentials, memory addresses, raw private payloads, or implementation details that weaken server security.

## Evolution

Event types and their metadata require individual schemas before implementation. Consumers must ignore unknown metadata fields and unknown event types. Breaking envelope changes require a new API version.
