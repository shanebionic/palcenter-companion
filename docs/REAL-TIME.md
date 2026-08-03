# Real-Time Event Stream

## Status

`/palcenter/v1/events/live` is a future WebSocket interface design. No WebSocket server or event stream is implemented in v0.1.0.

## Connection outline

1. A consumer verifies `/health`, `/version`, and `/capabilities` over HTTP.
2. The consumer connects only when the `events` capability is `true`.
3. The WebSocket handshake negotiates API and event-contract versions.
4. The server sends a connection acknowledgement before events.
5. The consumer tracks the last accepted event identity or stream cursor.

The exact subprotocol, authentication mechanism, and cursor format will be specified before implementation.

## Reconnect behavior

Consumers should reconnect with exponential backoff and bounded jitter. A normal disconnect must not disrupt standard PalCenter features. After reconnect, the consumer should provide its last acknowledged cursor when replay is supported. Deterministic event IDs allow duplicate deliveries to be discarded safely.

The future protocol must state its replay window and signal when a requested cursor is too old. It must never imply that every missed event was recovered when the server cannot prove that.

## Heartbeats

The future stream will define application-level heartbeat messages independent of gameplay events. A client that misses the negotiated heartbeat threshold should close the connection and begin reconnect behavior. Heartbeats must not be stored or presented as world events.

## Version negotiation

Clients and servers must agree on a supported Companion API version and stream contract during connection setup. Unsupported versions fail clearly without falling back to an ambiguously compatible stream. PalCenter then continues through the official REST API and heuristic inference.

## Ordering and delivery

The intended contract is ordered delivery within a single connection with at-least-once replay where supported. Consumers must deduplicate by deterministic `eventId`. Stronger delivery guarantees must not be claimed until persistence and restart behavior are implemented and tested.
