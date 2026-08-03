# Contributing to PalCenter Companion

Thank you for helping build a reliable, local-first companion for Palworld server administrators.

## Before contributing

- Read the [architecture](docs/ARCHITECTURE.md) and [guiding principles](docs/PRINCIPLES.md).
- Search existing issues and pull requests before proposing new work.
- Discuss architectural or game-integration changes in an issue before implementation.
- Never include server credentials, private telemetry, copyrighted game assets, or data captured from a real administrator environment.

## Development workflow

1. Fork the repository and create a focused branch.
2. Make one coherent change.
3. Run `npm run validate`.
4. Open a pull request using the repository template.

Branch names use a short category and description:

- `feature/health-api`
- `fix/capability-contract`
- `docs/event-model`
- `chore/validation-tooling`

Commit messages follow Conventional Commits where practical:

- `feat: define capability negotiation contract`
- `fix: clarify reconnect sequence`
- `docs: document coordinate spaces`
- `chore: update validation workflow`

## Coding and documentation standards

- Prefer small modules with one responsibility.
- Use strong types and avoid untyped escape hatches.
- Keep the Companion independent from PalCenter internals.
- Version externally consumed contracts and preserve backward compatibility.
- Treat event data as authoritative only when supported by direct server evidence.
- Use deterministic identifiers where contracts require deduplication.
- Document security, privacy, failure, and fallback behavior with each interface.
- Use `PalCenter Companion` or `Companion` for the public project identity.

Compatibility with UE4SS or another supported extension framework may be discussed as an implementation concern, but such frameworks do not define the project's public identity.

## Pull requests

Pull requests should explain the problem, scope, compatibility impact, validation, and any security or privacy implications. Keep unrelated refactors separate. New capabilities require corresponding contract and documentation updates.

All contributors must follow the [Code of Conduct](CODE_OF_CONDUCT.md). Security concerns should follow [SECURITY.md](SECURITY.md), not public issue reporting.
