# Admin-action implementation

Admin actions extend the authenticated v1 listener without treating the API
caller as an in-game administrator. Every request supplies the administrator's
stable Palworld player ID and a unique request ID.

## Dispatch and safety

The HTTP worker validates and queues an action. The existing UE4SS engine-tick
callback claims it and performs all gameplay reads and mutation on the game
thread. There is no internal retry.

At dispatch time the executor resolves exactly one live controller for each
stable player ID, refreshes both locations, and rejects missing, duplicate,
stale, or special-area state. Player-to-player actions use the destination
character's authoritative position and Unreal's collision-aware
`K2_TeleportTo`, preserving the moving actor's rotation.

Location actions accept only `palpagos` plus `verification: palpagos_map`, the
documented Palpagos X/Y bounds, and a finite caller-supplied Z. On the game
thread, Palworld's reflected `CanAdjustActorToFloorAtLocation` must resolve the
floor without prioritizing water. The result is rejected below the world ocean
plane, then passed to `K2_TeleportTo`. Any missing reflection function, failed
floor result, collision rejection, or unavailable player state fails closed.
No guessed Z or raw actor-location fallback is used.

The reflected function signatures are cross-checked against the pinned
[`PalUtility` SDK declaration](https://github.com/localcc/PalworldModdingKit/blob/62fad4130238cb0aadf024b87496e7387d5f4bf5/Source/Pal/Public/PalUtility.h).
Unreal documents that
[`K2_TeleportTo`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/AActor/K2_TeleportTo)
attempts a nearby collision-free fit and reports failure when the actor cannot
be placed.

Runtime capability probing verifies the reflected functions before advertising
the location action. Each action also requires both the global privileged gate
and its own configuration gate.

## Idempotency and audit

The service canonicalizes validated request fields under `requestId`. Concurrent
or later identical requests share the original result; a different payload with
the same ID is rejected. Retained JSONL audit records rebuild this ledger after
restart. The current and one prior file are capped at 5 MiB each, so retry
guarantees cover the retained administrator-readable audit window.

An accepted record is flushed before queueing. Final and rejected results are
also recorded with player IDs, coordinate spaces, destinations, and concise
reasons. Authentication tokens and display names are never written.

## Live-UAT boundary

Automated tests cover the contract, queue, idempotency, and failure behavior.
The exact Palworld floor trace, ocean-plane rejection, replicated placement,
mounted or gliding characters, nearby structures, and behavior after game
updates require the steps in [Admin teleport UAT](ADMIN-ACTIONS-UAT.md) on the
pinned supported PalServer build.
