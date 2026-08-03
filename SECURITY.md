# Security Policy

## Supported versions

PalCenter Companion is currently an architecture-only pre-release project. No deployable runtime is available and no version is supported for production use yet. Security support policy will be defined before the first executable release.

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
