# Capability Negotiation

Capability negotiation lets a consumer enable enhanced features without assuming that a Companion installation supports every planned integration.

## Initial model

`GET /palcenter/v1/capabilities` returns explicit booleans:

```json
{
  "events": false,
  "guilds": false,
  "bases": false,
  "performance": false,
  "moderation": false
}
```

All capabilities are `false` in the foundation example because none are implemented.

| Capability | Future meaning |
| --- | --- |
| `events` | Authoritative event query or streaming interfaces are available. |
| `guilds` | Authoritative guild information is available. |
| `bases` | Authoritative base information is available. |
| `performance` | Companion-sourced server performance data is available. |
| `moderation` | Companion moderation operations are available. |

## Consumer behavior

1. Continue normal operation through the official REST API.
2. Probe the Companion health and version endpoints.
3. Reject unsupported API versions without affecting normal operation.
4. Read capabilities.
5. Enable only features whose capability is explicitly `true`.
6. Prefer authoritative Companion data for that capability.
7. Return to the official REST API or inference path if the Companion becomes unavailable.

Consumers must treat missing, malformed, unknown, or non-boolean capability values as unsupported. Unknown capability names must be ignored for forward compatibility.

Capabilities describe currently usable interfaces, not roadmap promises. Future versions may evolve boolean values into separately named, richer negotiated features without changing the meaning of these v1 fields.
