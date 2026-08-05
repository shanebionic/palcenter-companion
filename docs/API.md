# Companion API

## Status

The discovery endpoints and bounded player Activity endpoint are implemented by the embedded v0.3.0 development listener. No WebSocket API is implemented.

The API version is independent from the Companion application version:

- Application version: `0.3.0`
- API version: `v1`
- Base path: `/palcenter/v1/`

The canonical machine-readable contract is [api/openapi.yaml](../api/openapi.yaml).

## `GET /palcenter/v1/activity`

Returns recent player joins, leaves, and session boundaries in chronological
order. Bearer authentication is required. `limit` accepts 1–200, `after` is an
exclusive UTC timestamp cursor, and `player` matches a user ID or player ID.
Departure and session-end records include `durationSeconds`; join and
session-start records return that field as `null`.

The buffer is bounded and memory-only. Restarting Companion or PalServer clears
it, so clients must tolerate a gap and continue with new records. Responses do
not contain player IP addresses, passwords, or authentication tokens.

## `GET /palcenter/v1/health`

Reports whether the in-process Companion listener can serve requests. It is inexpensive and does not depend on gameplay capabilities being active.

Example response:

```json
{
  "status": "healthy"
}
```

## `GET /palcenter/v1/version`

Reports application and API compatibility information.

Requires `Authorization: Bearer <token>`.

Example response:

```json
{
  "application": "palcenter-companion",
  "applicationVersion": "0.3.0",
  "apiVersion": "v1",
  "buildCommit": "abc1234",
  "buildBranch": "main",
  "buildDate": "2026-08-03T00:00:00Z",
  "compiler": "C++20",
  "palworldVersion": null,
  "ue4ssVersion": null,
  "compatibility": {
    "minimumPalCenter": "1.4.0",
    "testedPalCenter": "1.4.0",
    "testedPalworld": "v1.0.2.101103"
  },
  "runtime": {
    "startedAt": "2026-08-02T20:00:00Z",
    "uptimeSeconds": 12,
    "instanceId": "61fc7f4e-7d48-4ab7-a272-88a4322ccae7",
    "checks": { "configuration": "healthy" }
  }
}
```

Unavailable runtime versions are returned as `null`, not guessed. Compatibility metadata is informational; capabilities determine feature availability.

## `GET /palcenter/v1/capabilities`

Reports which optional authoritative features this Companion instance can currently provide. A reachable Companion does not imply that every feature is installed, supported, or active.

Requires `Authorization: Bearer <token>`.

```json
{
  "schemaVersion": "1",
  "categories": {
    "events": { "supported": false, "capabilityVersion": "1" },
    "health": { "supported": true, "capabilityVersion": "1" }
  }
}
```

See [capability negotiation](CAPABILITIES.md) for consumer behavior.

## Admin teleport actions

The authenticated endpoints below synchronously return the one game-thread
dispatch result. They require a caller-generated unique `requestId`, the
administrator character's stable 32-digit `administratorPlayerId`, and the
target's stable `targetPlayerId`.

- `POST /palcenter/v1/admin-actions/teleport-admin-to-player`
- `POST /palcenter/v1/admin-actions/teleport-player-to-admin`
- `POST /palcenter/v1/admin-actions/teleport-player-to-location`

Location requests also require finite `x`, `y`, and `z`, `coordinateSpace` set
to `palpagos`, and `verification` set to `palpagos_map`. World Tree and every
special-area coordinate space are rejected. A successful retry returns the
original result with `replayed: true` and does not move the character again.

Actions are independently advertised and are unavailable until both the global
privileged gate and the matching action gate are enabled. See the
[machine-readable contract](../api/openapi.yaml) and [UAT guide](ADMIN-ACTIONS-UAT.md).

## Compatibility rules

- Additive response fields may be introduced within API v1.
- Consumers must ignore unknown fields.
- Existing fields must not change meaning within API v1.
- Removing a field or changing its type requires a new API version.
- An unavailable endpoint, unsupported API version, timeout, or invalid response causes PalCenter to fall back safely rather than impair standard server management.

The listener binds to `127.0.0.1:8213` by default. Only the minimal health probe is unauthenticated. Version, capabilities, and every future gameplay or administrative endpoint require bearer authentication by default. Missing, malformed, oversized, and invalid credentials receive the same `401` response; `403` is reserved for a future authenticated identity that lacks permission. Administrators who select a non-loopback address must restrict access with their host firewall.

## `GET /palcenter/v1/locations`

Returns the latest authoritative location for each connected player. Main-world
locations use `palpagos`. An active Palworld stage instance uses
`special_area`, so clients do not plot unrelated coordinates on the Palpagos
map. See [Player locations](LOCATIONS.md).
