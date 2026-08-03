# Architecture

## System relationship

PalCenter Companion is an optional, independent server-side extension. It augments PalCenter with authoritative information while leaving the official Palworld REST API unchanged.

```text
Palworld Dedicated Server
├── Official REST API ───────────────┐
└── PalCenter Companion (optional) ──┼──> PalCenter
                                     └──> Other authorized local consumers
```

PalCenter should discover the Companion, negotiate capabilities, and use supported authoritative data. If discovery fails or a capability is unavailable, PalCenter continues operating through the official REST API and existing inference paths.

## Intended internal layers

```text
Game Hooks
    ↓
Internal Event Bus
    ↓
Companion API
    ↓
HTTP / WebSocket
    ↓
PalCenter
```

These are architectural boundaries, not implemented components in v0.1.0.

### Game Hooks

Future adapters observe authoritative server state through an appropriate supported extension framework. Framework-specific details remain isolated from public API contracts.

### Internal Event Bus

Normalizes authoritative observations into immutable internal events. Producers should not depend on HTTP, WebSocket, or PalCenter-specific behavior.

### Companion API

Defines versioned resources, capability discovery, authentication boundaries, and stable external data contracts. The API does not proxy, intercept, or rewrite the official REST API.

### Transports

HTTP supports discovery and bounded queries. A future WebSocket stream can deliver ordered real-time events with reconnect support. Transport implementations consume the same internal event model.

### Consumers

PalCenter is the primary intended consumer, but the Companion remains independently deployable. Consumer-specific presentation and heuristic fallback logic belong outside this repository.

## Failure and compatibility model

- Companion availability is never a prerequisite for PalCenter.
- Missing or `false` capabilities disable only their enhanced behavior.
- Unknown fields should be ignored by compatible consumers.
- Breaking API changes require a new base-path version.
- Application and API versions evolve independently.
- Uncertain observations must not be represented as authoritative facts.

## Trust boundary

The Companion will run alongside a game server and may observe sensitive administrator and player data. Future designs must authenticate callers, bind conservatively by default, minimize exposed data, avoid logging secrets, and never transmit information to external services without an explicit future administrator-controlled design.
