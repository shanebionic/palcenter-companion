# Live PalServer UAT

Complete this checklist against a disposable or backed-up server before publishing v0.3.0. Record sanitized evidence with timestamps. Do not mark a row passed without live evidence.

## Test record

| Item | Required value | Result |
| --- | --- | --- |
| Palworld Dedicated Server | Steam build `24466863`, game `v1.0.2.101103` | Passed initial UAT |
| UE4SS | `c838a8acaade1a0f860bdf249f039e58f4e10088` | Passed initial UAT |
| Companion DLL SHA-256 | Record from the final PR artifact | Pending final artifact |
| Package SHA-256 | Record from the final PR artifact | Pending final artifact |
| PalDefender | v1.8.3 | Loaded alongside Companion |
| Offline Raid Protection | v1.36.0, CurseForge file `7925687` | Initialized alongside Companion |
| Server host/network | Disposable Windows host plus separate Docker network namespace | Passed |

## Installation and lifecycle

- [x] Build `main.dll` with the authorized pinned toolchain.
- [x] Validate the staged ZIP and record its SHA-256.
- [x] Install the complete `PalCenterCompanion` folder.
- [x] Confirm UE4SS discovers it.
- [x] Confirm `start_mod` executes.
- [x] Confirm `on_unreal_init` executes once in each recorded server run.
- [x] Confirm initialization returns and PalServer reaches its UDP and REST listening state.
- [x] Connect a real Palworld client and confirm normal playable state.
- [x] Confirm PalDefender v1.8.3 loads alongside Companion.
- [x] Confirm Offline Raid Protection v1.36.0 initializes alongside Companion.
- [ ] Capture `Companion stopped` during normal PalServer shutdown. The pinned UE4SS process-exit path currently does not invoke C++ `uninstall_mod` handlers.
- [x] Confirm port 8213 is released after graceful PalServer shutdown.
- [x] Restart and confirm exactly one listener starts.
- [x] Complete at least three start/stop cycles without a crash or leaked port.

## Endpoint checks

Run all three from the PalServer host, then run health from the network location used by PalCenter:

```powershell
Invoke-RestMethod http://127.0.0.1:8213/palcenter/v1/health
Invoke-RestMethod http://<private-palserver-address>:8213/palcenter/v1/health
```

Use the generated bearer token for authenticated endpoints; never paste a real
token into committed test evidence:

```powershell
$headers = @{ Authorization = "Bearer <companion-token>" }
Invoke-RestMethod http://127.0.0.1:8213/palcenter/v1/version -Headers $headers
Invoke-RestMethod http://127.0.0.1:8213/palcenter/v1/capabilities -Headers $headers
Invoke-RestMethod http://127.0.0.1:8213/palcenter/v1/activity -Headers $headers
```

- [x] Responses match the documented v1 contract.
- [x] Health contains no credentials, player data, or gameplay information.
- [x] A separate Docker network namespace reaches the explicitly configured private listener through `host.docker.internal`; loopback remains the restored default.

## v0.3 player Activity

- [x] A real client join produces exactly one `player_joined` and one
  `session_started` record with a shared session ID.
- [x] A real client disconnect produces exactly one `player_left` and one
  `session_ended` record with the same session ID.
- [x] The server-side `PalPlayerState` EndPlay callback supplies the departure
  boundary on Steam build `24466863`.
- [x] Repeated hook delivery is deduplicated by the session tracker.
- [x] Restarting PalServer clears the documented memory-only Activity buffer.
- [x] PalServer remains healthy after join, disconnect, and restart cycles.
- [x] No IP address, token, or password appears in Activity responses or logs.
- [ ] Repeat final live-client UAT with the release-candidate artifact and
  record its hashes before publishing.

## Configuration matrix

For each case, restart PalServer, capture the relevant log, verify expected listener state, and confirm PalServer remains playable.

| Case | Expected result | Status |
| --- | --- | --- |
| `Enabled=true` | One listener starts | Passed |
| `Enabled=false` | No listener; informational log | Passed |
| Default port `8213` | Listener starts | Passed |
| Alternate unused port | Listener starts on that port | Passed (`18213`) |
| Loopback bind | Host-only access | Passed |
| Private/LAN bind | Listener starts plus security warning | Passed (`0.0.0.0`, private UAT only) |
| Invalid bind address | Clear error; PalServer continues | Passed |
| Occupied port | Clear error; PalServer continues | Passed |
| Malformed INI | Clear error; PalServer continues | Passed |
| Missing INI | Clear error; PalServer continues | Passed |
| `Debug` | Listener healthy; configured level accepted | Passed |
| `Information` | Listener healthy; configured level accepted | Passed |
| `Warning` | Listener healthy; configured level accepted | Passed |
| `Error` | Listener healthy; configured level accepted | Passed |

## Log evidence template

Startup should include:

```text
[PalCenterCompanion] PalCenter Companion v0.3.0
[PalCenterCompanion] Companion initialized
[PalCenterCompanion] Listening on 127.0.0.1:8213
[PalCenterCompanion] API Version v1
```

Normal shutdown should include:

```text
[PalCenterCompanion] Companion stopped
```

Attach sanitized startup, playable-state, compatibility, shutdown, and restart excerpts to PR #2. Never include server passwords, public addresses, tokens, or unrelated player information.
