# Admin teleport UAT

Use a private test server and keep a backup. All players used in a test must be
online in Palpagos, not inside a dungeon, tower, arena, World Tree, or another
special area.

## Enable

Stop PalServer. In `PalCenterCompanion/config/PalCenterCompanion.ini`, set:

```ini
AdminActionsEnabled=true
TeleportAdminToPlayerEnabled=true
TeleportPlayerToAdminEnabled=true
TeleportPlayerToLocationEnabled=true
```

Restart PalServer. Keep the Companion listener on loopback. Read the current
player IDs from authenticated `GET /palcenter/v1/locations`; use the 32-digit
`playerId` for the administrator character and target character.

## Exercise

Send authenticated JSON requests to these endpoints. Generate a new unique
`requestId` for each intended action; reuse it only to verify a safe retry.

- `POST /palcenter/v1/admin-actions/teleport-admin-to-player`
- `POST /palcenter/v1/admin-actions/teleport-player-to-admin`
- `POST /palcenter/v1/admin-actions/teleport-player-to-location`

The first two bodies contain `requestId`, `administratorPlayerId`, and
`targetPlayerId`. The location body also contains:

```json
{
  "destination": {
    "coordinateSpace": "palpagos",
    "verification": "palpagos_map",
    "x": 100,
    "y": 200,
    "z": 300
  }
}
```

Use coordinates confirmed from the Palpagos map and supply the observed Z; the
Companion never invents one. Confirm each character arrives on solid land with
orientation preserved. Repeat one successful request unchanged and confirm the
response has `"replayed": true` without a second move.

Change `coordinateSpace` to `world_tree` and then `special_area`; both must be
rejected. Move either character into a dungeon or other special area and confirm
player-to-player teleport is rejected.

## Audit and disable

Check `PalCenterCompanion/config/PalCenterCompanion.teleport-audit.jsonl`. It
must contain requested, successful, replayed, and rejected attempts without the
API token. A bounded prior file may exist with the `.1` suffix.

After testing, stop PalServer, set `AdminActionsEnabled=false`, and restart.
Confirm every teleport action is `false` in the capabilities response.
