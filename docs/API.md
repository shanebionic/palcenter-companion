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
  "status": "healthy",
  "applicationVersion": "0.1.0",
  "apiVersion": "v1",
  "startedAt": "2026-08-02T20:00:00Z",
  "uptimeSeconds": 12
}
```

## `GET /palcenter/v1/version`

Reports application and API compatibility information.

Example response:

```json
{
  "application": "palcenter-companion",
  "applicationVersion": "0.1.0",
  "apiVersion": "v1"
}
```

Consumers must negotiate compatibility using `apiVersion`; the application version is operational metadata and does not replace API versioning.

## `GET /palcenter/v1/capabilities`

Reports which optional authoritative features this Companion instance can currently provide. A reachable Companion does not imply that every feature is installed, supported, or active.

```json
{
  "events": false,
  "guilds": false,
  "bases": false,
  "performance": false,
  "moderation": false
}
```

See [capability negotiation](CAPABILITIES.md) for consumer behavior.

## Compatibility rules

- Additive response fields may be introduced within API v1.
- Consumers must ignore unknown fields.
- Existing fields must not change meaning within API v1.
- Removing a field or changing its type requires a new API version.
- An unavailable endpoint, unsupported API version, timeout, or invalid response causes PalCenter to fall back safely rather than impair standard server management.

The discovery listener is unauthenticated in v0.1.0 and binds to `127.0.0.1:8213` by default. Administrators who select a non-loopback address must restrict access with their host firewall. Authentication, rate limits, and error-envelope details must be specified before gameplay or administrative capabilities are enabled.
