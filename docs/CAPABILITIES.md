# Capability Negotiation

Capability negotiation lets a consumer enable enhanced features without assuming that a Companion installation supports every planned integration.

## Initial model

`GET /palcenter/v1/capabilities` returns grouped, versioned capability metadata:

```json
{
  "schemaVersion": "1",
  "categories": {
    "events": { "supported": false, "capabilityVersion": "1" },
    "health": { "supported": true, "capabilityVersion": "1" }
  }
}
```

Only the `health` and `version` discovery capabilities are supported in this milestone.

| Capability | Future meaning |
| --- | --- |
| `events` | Authoritative event query or streaming interfaces are available. |
| `coordinateSpaces` | Authoritative coordinate-space identification is available. |
| `playerLocations` | Current server-owned player locations are available. |
| `guilds` | Authoritative guild information is available. |
| `bases` | Authoritative base information is available. |
| `performance` | Companion-sourced server performance data is available. |
| `moderation` | Companion moderation operations are available. |
| `administration` | Reserved broad administration category; currently false. |
| `adminActions` | Independently negotiated privileged action interfaces are available. |
| `health` | Structured health information is available. |
| `version` | Build and compatibility information is available. |

## Consumer behavior

1. Continue normal operation through the official REST API.
2. Probe the Companion health and version endpoints.
3. Reject unsupported API versions without affecting normal operation.
4. Read capabilities.
5. Enable only features whose `supported` value is explicitly `true`.
6. Prefer authoritative Companion data for that capability.
7. Return to the official REST API or inference path if the Companion becomes unavailable.

Capability identifiers are permanent: they are never renamed or removed, and new identifiers are added only. Consumers must treat missing or malformed entries as unsupported and ignore unknown categories and metadata for forward compatibility.

Capabilities describe currently usable interfaces, not roadmap promises. Capability negotiation takes precedence over informational application-version compatibility.

The `adminActions` entry contains an `actions` object with permanent Boolean
identifiers: `teleportAdminToPlayer`, `teleportPlayerToAdmin`, and
`teleportPlayerToLocation`. Admin actions capability version `2` identifies the
location request that accepts verified Palpagos X/Y and resolves Z inside the
Companion. An identifier is true only when configuration
enables it and the installed runtime has probed the required game integration.
Consumers must check the individual action rather than the family-level summary.
