# PalServer Build 24466863 Validation

Validation performed on 2026-08-03 using a clean disposable SteamCMD installation. No production saves, credentials, player information, or public addresses were used.

## Build record

| Component | Validated value |
| --- | --- |
| Palworld Dedicated Server | Steam build `24466863` |
| Reported game version | `v1.0.2.101103` |
| RE-UE4SS | Okaetsu commit `c838a8acaade1a0f860bdf249f039e58f4e10088` |
| Companion | v0.1.0 production `main.dll` |
| Companion DLL SHA-256 | `368dc543e71e0e49a9d2d5160f216cbd41aa74dc18f6d183d32e1a0b98a88bee` |
| Package SHA-256 | `79b43ca893a36575ca107bf8e24350a1ede4e570e5134a85f393d85f91b80a8c` |
| PalDefender | v1.8.3 |
| Offline Raid Protection | v1.36.0, CurseForge file `7925687` |

The build used Visual Studio 2022 17.14, MSVC 19.44, Windows SDK 10.0.26100.0, CMake `3.31.6-msvc6`, Rust 1.88.0, the `Game__Shipping__Win64` UE4SS configuration, and `/MD` runtime linkage.

## Package validation

The packaging workflow verified `start_mod` and `uninstall_mod`, rejected contract-test output, and produced only:

```text
PalCenterCompanion/
├── dlls/main.dll
├── config/PalCenterCompanion.ini
├── enabled.txt
├── README.txt
└── LICENSES/THIRD-PARTY-NOTICES.txt
```

No UE4SS, UEPseudo, source tree, or access-controlled dependency was included.

## Live results

- SteamCMD installed and validated the clean server.
- UE4SS discovered `PalCenterCompanion` through `enabled.txt`.
- Each recorded server run contained one Companion initialization and one listener.
- PalServer reached its UDP game/query listening state and authenticated REST API state.
- Health, version, and capabilities returned the documented v1 contracts.
- Graceful shutdown through Palworld's official REST API exited PalServer and released port 8213.
- More than three restart cycles completed without a crash or leaked Companion port.
- Twelve configuration cases passed: enabled, disabled, default port, alternate port, loopback, private bind, invalid bind, occupied port, malformed INI, missing INI, and all four log levels.
- A separate Docker container reached the listener through `host.docker.internal` only after the Companion was explicitly bound to `0.0.0.0`. Loopback was restored afterward.
- PalDefender v1.8.3 reported itself loaded while the Companion remained healthy.
- Offline Raid Protection v1.36.0 reported successful initialization while the Companion remained healthy.

Sanitized startup excerpt:

```text
Mod 'OfflineRaidProtection' has enabled.txt, starting mod.
Mod 'PalCenterCompanion' has enabled.txt, starting mod.
[OfflineRaidProtection] v1.36.0 by Mathayuss
[OfflineRaidProtection] Initialized Successfully!
[PalCenterCompanion] PalCenter Companion v0.1.0
[PalCenterCompanion] Companion initialized
[PalCenterCompanion] Listening on 127.0.0.1:8213
[PalCenterCompanion] API Version v1
```

PalDefender excerpt:

```text
Starting PalDefender Anti Cheat v1.8.3 (console)
Game version is v1.0.2.101103
Running Palworld dedicated server on :18211
PalDefender Anti Cheat v1.8.3 loaded!
```

## Endpoint evidence

Health:

```json
{
  "status": "healthy",
  "applicationVersion": "0.1.0",
  "apiVersion": "v1",
  "startedAt": "2026-08-03T05:15:24Z",
  "uptimeSeconds": 54
}
```

Version:

```json
{
  "application": "palcenter-companion",
  "applicationVersion": "0.1.0",
  "apiVersion": "v1"
}
```

Capabilities:

```json
{
  "events": false,
  "guilds": false,
  "bases": false,
  "performance": false,
  "moderation": false
}
```

## Remaining gates

An actual Palworld game client was not connected, so client-level playable-state validation remains pending.

The pinned UE4SS revision's `DLL_PROCESS_DETACH` path calls `UE4SSProgram::static_cleanup()`, whose destructor stops its event loop and closes logging but does not call `uninstall_mods()`. Consequently, normal PalServer shutdown released port 8213 at process exit but did not call Companion's `uninstall_mod` or emit `Companion stopped`. The contract-test DLL still proves explicit `uninstall_mod` cleanly stops and joins the listener. Blocking teardown must not be added to `DllMain` because it runs under the Windows loader lock.

## Authenticated discovery follow-up

The capability-negotiation build was revalidated on the same disposable PalServer installation:

- Production DLL SHA-256: `b9f9ab43551ee8a030720f476a2fc327ff9385c1b56542e2380b1290d1535cf1`
- Package SHA-256: `736eb6ba1acc950e93e87abd72d37c0d28498360b2a9ccec72836ca8e5560545`
- First startup generated one persistent 64-character token from 32 OS-CSPRNG bytes.
- Unauthenticated health returned only `status`.
- Missing and invalid credentials returned `401`; the persisted token returned `200` for version and capabilities.
- Restart preserved the token and started one listener.
- A separate Docker network namespace authenticated successfully over an explicitly configured private listener.
- A production PalCenter API instance discovered the Companion using explicit host `127.0.0.1` and port `18213`, reported invalid-token failure distinctly, kept the token out of public responses, and continued reporting the official REST server online.
- PalServer shut down cleanly after the checks, and the Companion configuration was restored to `127.0.0.1:8213`.

No real Palworld game client was connected during this follow-up. The earlier UE4SS process-exit limitation remains unchanged.
