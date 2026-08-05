# Changelog

All notable changes to PalCenter Companion will be documented here. This project follows [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Independently negotiated, authenticated administrator teleport actions.
- Default-off global and per-action configuration gates.
- Durable bounded teleport audit and retained idempotency records.
- Game-thread dispatch with live-player, coordinate-space, and safe-placement checks.
- Focused automated coverage and a packaged administrator UAT guide.

### Changed

- Location teleport requests now accept verified Palpagos X/Y only and resolve
  safe Z through Palworld at dispatch time; legacy caller-provided Z is rejected.
- Admin-action capability version advanced to `2`, and successful audit/replay
  records retain the runtime-resolved X/Y/Z destination.

## [0.1.0] - 2026-08-02

### Added

- Initial project governance and contributor guidance.
- Versioned Companion API contract for health, version, and capability discovery.
- Architecture designs for authoritative events, coordinate spaces, and a future live event stream.
- UE4SS C++ lifecycle adapter that loads and unloads with PalServer.
- Embedded HTTP listener for health, version, and disabled capability discovery.
- Validated INI configuration with loopback binding and information-level logging defaults.
- Automated tests for configuration, endpoint responses, port binding, startup, and shutdown.
- Lightweight CI validation for formatting, documentation, contracts, and repository structure.

[Unreleased]: https://github.com/shanebionic/palcenter-companion/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/shanebionic/palcenter-companion/releases/tag/v0.1.0
