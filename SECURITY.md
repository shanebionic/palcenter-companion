# Security Policy

## Supported versions

PalCenter Companion is currently a pre-release project. Its discovery-only runtime is implemented, but no version is supported for production use until the pinned production DLL passes live PalServer validation. Security support policy will be defined before the first certified release.

## Reporting a vulnerability

Do not open a public issue for a suspected vulnerability. Use GitHub's **Report a vulnerability** option on the repository Security tab to submit a private advisory.

Include the affected contract or component, impact, reproduction details, and any suggested mitigation. Do not include real Palworld administrator credentials, player information, or production telemetry.

Maintainers will acknowledge a complete report when practical, assess its scope, and coordinate disclosure after a remediation is available.

## Security principles

Future implementation work must preserve these baseline expectations:

- local operation without required cloud services;
- no analytics or external telemetry collection;
- authenticated and least-privilege administration interfaces;
- no modification or interception of the official Palworld REST API;
- explicit capability negotiation;
- safe handling of administrator credentials and event data;
- conservative behavior when identity or evidence is uncertain.

The v0.1 discovery API has no authentication or TLS. It exposes health, version, and disabled capabilities only and must remain on loopback or a tightly controlled private test network. Gameplay or player data must not be introduced until API authentication is implemented.
