# Companion API

## Status

The three discovery endpoints in this document are implemented by the embedded v0.1.0 HTTP listener. No gameplay or WebSocket API is implemented.

The API version is independent from the Companion application version:

- Application version: `0.1.0`
- API version: `v1`
- Base path: `/palcenter/v1/`

The canonical machine-readable contract is [api/openapi.yaml](../api/openapi.yaml).

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
  "applicationVersion": "0.1.0",
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

## Compatibility rules

- Additive response fields may be introduced within API v1.
- Consumers must ignore unknown fields.
- Existing fields must not change meaning within API v1.
- Removing a field or changing its type requires a new API version.
- An unavailable endpoint, unsupported API version, timeout, or invalid response causes PalCenter to fall back safely rather than impair standard server management.

The listener binds to `127.0.0.1:8213` by default. Only the minimal health probe is unauthenticated. Version, capabilities, and every future gameplay or administrative endpoint require bearer authentication by default. Missing, malformed, oversized, and invalid credentials receive the same `401` response; `403` is reserved for a future authenticated identity that lacks permission. Administrators who select a non-loopback address must restrict access with their host firewall.
