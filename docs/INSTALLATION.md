# Installation and Startup

## Compatibility status

The v0.1.0 implementation exposes discovery information only. Its production release candidate targets Palworld Dedicated Server Steam build `24466863` (game version `v1.0.2.101103`) and Okaetsu RE-UE4SS commit `c838a8acaade1a0f860bdf249f039e58f4e10088`. Initial live validation is recorded in [PalServer build 24466863 validation](validation/PALSERVER-24466863.md); the remaining gates are tracked in [Live PalServer UAT](LIVE-PALSERVER-UAT.md).

Public CI builds `PalCenterCompanion-contract-test.dll` against test declarations. That DLL is not a production artifact and must not be installed. Authorized contributors build the real `main.dll` using [Production Toolchain](TOOLCHAIN.md).

## Requirements

- Palworld Dedicated Server using the Windows server binary
- Windows, or Linux hosting the Windows server through Wine/Proton
- The exact Palworld-compatible UE4SS revision recorded above
- Microsoft Visual C++ 2015–2022 x64 Redistributable because `main.dll` uses `/MD`

Native Linux PalServer is not supported by UE4SS. Do not install multiple UE4SS loaders. PalCenter Companion, PalDefender's UE4SS variant, Offline Raid Protection, and other UE4SS extensions must share one compatible loader installation.

## Build from authorized sources

The production build accepts the authorized UE4SS checkout as a local CMake input; it never downloads or commits protected dependencies:

```powershell
.\scripts\build-production.ps1 -Ue4ssRoot C:\src\RE-UE4SS
```

See [Production Toolchain](TOOLCHAIN.md) for source preparation and exact compiler pins. Missing paths, uninitialized UEPseudo sources, a different UE4SS commit, compiler mismatch, SDK mismatch, or wrong generator causes an explicit configure failure.

Successful packaging creates:

```text
artifacts/
├── PalCenterCompanion-0.1.0-win64.zip
├── PalCenterCompanion-0.1.0-win64.zip.sha256
└── stage/
    └── PalCenterCompanion/
        ├── dlls/
        │   └── main.dll
        ├── config/
        │   └── PalCenterCompanion.ini
        ├── enabled.txt
        ├── README.txt
        └── LICENSES/
            └── THIRD-PARTY-NOTICES.txt
```

The package excludes UE4SS, UEPseudo, source trees, and contract-test output.

## Install

1. Stop PalServer and back up its current UE4SS configuration.
2. Confirm the pinned compatible UE4SS loader is installed.
3. Extract the complete `PalCenterCompanion` directory to:

   ```text
   PalServer/Pal/Binaries/Win64/ue4ss/Mods/PalCenterCompanion/
   ```

4. Confirm `dlls/main.dll`, the configuration, `enabled.txt`, README, and notices are present.
5. Restart PalServer.
6. Review `UE4SS.log` for the startup messages below.
7. Verify all three endpoints from the server host.

`enabled.txt` supplies copy-and-restart loading. Administrators using explicit UE4SS load ordering can remove it and enable `PalCenterCompanion` in `mods.txt` instead.

## Configuration

`config/PalCenterCompanion.ini`:

```ini
[Companion]
Enabled=true
BindAddress=127.0.0.1
Port=8213
LogLevel=Information
```

| Setting | Default | Meaning |
| --- | --- | --- |
| `Enabled` | `true` | Starts the embedded API after Unreal initialization. |
| `BindAddress` | `127.0.0.1` | Interface used by the listener. |
| `Port` | `8213` | Companion API port, separate from the official REST API. |
| `LogLevel` | `Information` | Minimum level: `Debug`, `Information`, `Warning`, or `Error`. |

Invalid or missing configuration and listener startup failures produce an error and leave the Companion unavailable. They do not intentionally terminate PalServer. Automated tests cover defaults, disabled mode, alternate ports, malformed and missing files, invalid addresses, occupied ports, every log-level value, non-loopback warnings, and repeated listener cycles. Live PalServer results remain part of the UAT gate.

## Networking and Unraid

`127.0.0.1` is reachable only inside the same process host or network namespace. It does not automatically cross container boundaries.

### Shared host network

When PalCenter and the Windows PalServer process share the same host network namespace, loopback may be usable. Confirm from inside the PalCenter runtime rather than assuming that host networking removes all process or Wine isolation.

### Separate containers

When PalCenter and PalServer use separate containers:

1. Set `BindAddress` to the Palworld container interface or `0.0.0.0`.
2. Expose/map TCP port `8213` on the Palworld container.
3. Configure future PalCenter detection with the Palworld container hostname or private IP, not `127.0.0.1`.
4. Restrict access to the PalCenter container/network.

For Unraid, add TCP port `8213` to the Palworld container template—not the PalCenter template—and use the Palworld container name when both containers share a custom Docker network.

### Separate machine

Bind to an appropriate private/LAN interface, permit TCP 8213 only from the PalCenter host, and never forward the port from the public Internet.

Example Windows Firewall rule for a private PalCenter host at `192.0.2.10` (replace the documentation address with the real private address):

```powershell
New-NetFirewallRule -DisplayName "PalCenter Companion from PalCenter" `
  -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8213 `
  -RemoteAddress 192.0.2.10 -Profile Private
```

The API has no authentication or TLS in v0.1.0. Non-loopback access is only for controlled private-network testing. No gameplay or player data may be added before API authentication is implemented.

## Verify endpoints

From the server host:

```powershell
Invoke-RestMethod http://127.0.0.1:8213/palcenter/v1/health
Invoke-RestMethod http://127.0.0.1:8213/palcenter/v1/version
Invoke-RestMethod http://127.0.0.1:8213/palcenter/v1/capabilities
```

Health contains only status and build/lifecycle metadata—never credentials, players, or gameplay data:

```json
{
  "status": "healthy",
  "applicationVersion": "0.1.0",
  "apiVersion": "v1",
  "startedAt": "2026-08-03T20:00:00Z",
  "uptimeSeconds": 12
}
```

## Startup and shutdown logs

Expected startup:

```text
[PalCenterCompanion] PalCenter Companion v0.1.0
[PalCenterCompanion] Companion initialized
[PalCenterCompanion] Listening on 127.0.0.1:8213
[PalCenterCompanion] API Version v1
```

Expected normal unload:

```text
[PalCenterCompanion] Companion stopped
```

The listener binds before a dedicated worker thread enters its request loop. It does not run the HTTP loop on Unreal's game thread. Application lifecycle operations are mutex-protected; running state and bound port are atomic; handlers use server-owned immutable route state; explicit `uninstall_mod` calls stop and join the listener before destroying state. A duplicate Unreal initialization callback is ignored. Bind failure degrades safely without starting a worker.

The pinned UE4SS revision does not call `uninstall_mods()` from its process-detach cleanup path. A normal PalServer shutdown therefore releases the listener when the process exits, but does not produce the Companion's `Companion stopped` message. Do not add blocking cleanup to Windows `DllMain`; that would run under the loader lock. This limitation remains a release gate until an upstream-supported pre-exit/unload lifecycle is available or the project approves a safe alternative.

## PalCenter detection

The generated API credential is stored at `PalCenterCompanion/config/PalCenterCompanion.token`. To rotate it, stop PalServer, replace the file with a new 64-character hexadecimal token generated from 32 secure random bytes, update PalCenter, and restart. Never send it in a URL or share the Palworld administrator password.

A future PalCenter integration will request:

```text
http://<private-palworld-address>:8213/palcenter/v1/health
```

A compatible response means **Companion Connected**. A timeout, refusal, unsupported API version, or invalid response means **Companion Not Installed**. PalCenter continues all official REST API behavior either way.

## Troubleshooting

### UE4SS does not list the Companion

- Confirm the installed file is `PalCenterCompanion/dlls/main.dll`, not the contract-test DLL.
- Confirm `enabled.txt` exists or the mod is enabled in `mods.txt`.
- Confirm the package is nested exactly once under `ue4ss/Mods`.

### Wrong UE4SS ABI or immediate load failure

- Compare the server loader commit with [Production Toolchain](TOOLCHAIN.md).
- Rebuild; never rename the contract-test DLL to `main.dll`.
- Confirm the Microsoft Visual C++ 2015–2022 x64 Redistributable is installed.
- Restore the backed-up UE4SS installation if other extensions also fail.

### Port already in use

```powershell
Get-NetTCPConnection -LocalPort 8213 -ErrorAction SilentlyContinue
```

Stop the conflicting listener or select an unused private port in the INI and matching container/firewall configuration.

### Listener fails or remote health is refused

- Read the PalCenter Companion entries in `UE4SS.log`.
- Verify `Enabled`, `BindAddress`, `Port`, and INI syntax.
- Test loopback on the PalServer host first.
- For containers, test from inside the PalCenter network namespace.
- Confirm the private firewall rule and container port mapping.
- Do not solve connectivity by exposing the unauthenticated API publicly.
