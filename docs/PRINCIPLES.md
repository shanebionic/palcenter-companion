# Guiding Principles

## Preserve the official API

The Companion never modifies, replaces, intercepts, or impersonates the official Palworld REST API. Its interfaces use a separate `/palcenter/` namespace and remain optional.

## Prefer authoritative evidence

The Companion is an authoritative event source, not a collection of helper endpoints. It should expose state as the Palworld server understands it. PalCenter should prefer that evidence over heuristic inference and gracefully return to the official REST API when the evidence is unavailable.

## Remain independent

PalCenter works without the Companion, and the Companion does not require PalCenter. Public contracts must not depend on PalCenter's database, frontend, or internal service layout.

## Stay local and private

The Companion never collects analytics, transmits telemetry externally, or requires a cloud service. Data remains under the server administrator's control. Future external integrations, if ever considered, require explicit opt-in design and security review.

## Negotiate every enhancement

Availability is discovered through version and capability negotiation. Consumers enable only explicitly supported features and fail conservatively.

## Minimize impact

Future server integration must prioritize stability, bounded resource use, minimal data exposure, and clean failure isolation. An enhancement must not compromise normal Palworld operation.

## Keep identity clear

The public project is **PalCenter Companion**, an optional server-side extension for administrators. It may eventually use UE4SS or another supported extension framework internally, but compatibility mechanics do not define its identity or its public contracts.
