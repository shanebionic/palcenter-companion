# Event Model

## Status

PalCenter Companion v0.3.1 produces bounded player join, leave, session-start,
and session-end activity. Other event families remain future work.

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

Player Activity IDs remain stable within the current in-memory delivery window
and are derived from the server instance, stable player key, session timestamp,
and activity type. They contain no private player data.

## Player identity

The `player` object should use stable server-provided identifiers where available. Display names are mutable and must not be used as the sole identity key. Contracts must distinguish player, platform user, and display identities and must not expose more personal information than the event requires.

## Palworld lifecycle hooks

The v0.3 collector is event-driven and does not duplicate the official REST
poller. On PalServer build `24466863`, the dedicated-server authority path is:

- global actor BeginPlay for `PalPlayerController`, followed by a bounded
  identity-resolution queue because Palworld populates `PlayerUId` after
  BeginPlay;
- global actor EndPlay for `PalPlayerState`, which is the tested player-specific
  network-session departure signal.

The identity queue is capped at 256 controllers and expires entries after 30
seconds. `PlayerUId` is the stable session-history key exposed as `playerId`.
The current hook does not expose the platform `userId`, so that field is
`null`. `PlayerNamePrivate` is used when populated, with `AccountName` as the
display-name fallback.

Live testing proved that `K2_OnLogout`, controller EndPlay, controller
`NetConnection`, `OnDestroyPawn`, and the player logout Blueprint action are
not reliable normal-disconnect signals on this dedicated-server build. They
must not be used to claim a clean-leave versus connection-loss distinction.
The UI therefore uses the honest generic wording “left the server.”

## Confidence and evidence

`confidence` communicates whether information is directly observed or derived. Initial vocabulary is expected to include `authoritative` and `unknown`; adding inferred confidence levels requires an explicit contract update.

`evidence` must describe safe provenance without including credentials, memory addresses, raw private payloads, or implementation details that weaken server security.

## Evolution

Event types and their metadata require individual schemas before implementation. Consumers must ignore unknown metadata fields and unknown event types. Breaking envelope changes require a new API version.
