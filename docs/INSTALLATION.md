# Installation and Startup

## Milestone status

The v0.1.0 implementation is an early development artifact. It exposes discovery information only and has not yet completed a published Palworld/UE4SS compatibility certification. It contains no gameplay hooks, player data, telemetry, or game events.

Public CI verifies the platform-neutral runtime and the Windows DLL lifecycle contract. Because UE4SS production builds require authorized Unreal-derived `UEPseudo` sources, a release candidate must also be compiled against the exact authorized UE4SS revision and loaded in a disposable PalServer before distribution.

## Requirements

- Palworld Dedicated Server running the Windows server binary
- Windows, or Linux hosting the Windows server through Wine/Proton
- A Palworld-compatible UE4SS build matching the Companion release compatibility notes

Native Linux PalServer is not supported by UE4SS today. Do not install multiple UE4SS loaders: PalCenter Companion, PalDefender's UE4SS variant, Offline Raid Protection, and other UE4SS extensions should share one compatible loader installation.

## Planned release installation

1. Stop the Palworld server.
2. Install or update the compatible UE4SS release specified by the Companion release notes.
3. Copy the release's `PalCenterCompanion` directory to:

   ```text
   PalServer/Pal/Binaries/Win64/ue4ss/Mods/PalCenterCompanion/
   ```

4. Confirm this layout:

   ```text
   PalCenterCompanion/
   ├── config/
   │   └── PalCenterCompanion.ini
   ├── dlls/
   │   └── PalCenterCompanion.dll
   └── enabled.txt
   ```

5. Restart the Palworld server.
6. Review `UE4SS.log` for the startup messages below.
7. From the same machine, request `http://127.0.0.1:8213/palcenter/v1/health`.

`enabled.txt` provides the intended copy-and-restart behavior. Administrators who manage explicit UE4SS load ordering may remove it and enable `PalCenterCompanion` in UE4SS's `mods.txt` instead.

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
| `Enabled` | `true` | Starts the embedded API when the server initializes. |
| `BindAddress` | `127.0.0.1` | Network interface used by the embedded listener. |
| `Port` | `8213` | Dedicated Companion API port, separate from Palworld's REST API. |
| `LogLevel` | `Information` | Minimum level: `Debug`, `Information`, `Warning`, or `Error`. |

Keep the default loopback address when PalCenter runs on the same host. If PalCenter is remote, bind to an appropriate private interface and restrict port `8213` to the PalCenter host with the operating-system firewall. This milestone does not provide API authentication or TLS and must not be exposed publicly.

Invalid configuration or a port-bind failure disables the Companion listener and records an error. It does not intentionally terminate the Palworld server.

## Startup verification

Expected UE4SS log entries at the default information level:

```text
[PalCenterCompanion] PalCenter Companion v0.1.0
[PalCenterCompanion] Companion initialized
[PalCenterCompanion] Listening on 127.0.0.1:8213
[PalCenterCompanion] API Version v1
```

Health response:

```json
{
  "status": "healthy",
  "applicationVersion": "0.1.0",
  "apiVersion": "v1",
  "startedAt": "2026-08-02T20:00:00Z",
  "uptimeSeconds": 12
}
```

## PalCenter detection

A future PalCenter integration will probe:

```text
http://<palworld-server>:8213/palcenter/v1/health
```

A compatible successful response means **Companion Connected**. A timeout, connection refusal, unsupported API version, or invalid response means **Companion Not Installed**. PalCenter continues all normal official REST API behavior in either case.

## Clean shutdown

When UE4SS unloads the extension, `uninstall_mod` destroys the Companion instance. Destruction stops the embedded listener, joins its worker thread, releases the port, and then returns control to UE4SS.
