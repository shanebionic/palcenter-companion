# Companion API

## Status

This document defines an initial interface only. No HTTP service is implemented in v0.1.0.

The API version is independent from the Companion application version:

- Application version: `0.1.0`
- API version: `v1`
- Base path: `/palcenter/v1/`

The canonical machine-readable contract is [api/openapi.yaml](../api/openapi.yaml).

## `GET /palcenter/v1/health`

Reports whether the Companion process can serve requests. It should be inexpensive and must not depend on gameplay capabilities being active.

Example response:

```json
{
  "status": "ok"
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

Authentication, network binding, rate limits, and error-envelope details will be specified before implementation. They are intentionally not guessed in this foundation milestone.
