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
`targetPlayerId`. The complete location body is:

```json
{
  "requestId": "replace-with-a-unique-request-id",
  "administratorPlayerId": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
  "targetPlayerId": "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
  "coordinateSpace": "palpagos",
  "x": 100,
  "y": 200,
  "verification": "palpagos_map"
}
```

Use X/Y confirmed from the Palpagos map. Do not supply Z; Companion resolves it
on the game thread from Palworld's floor API. Confirm the success response has
the final X, Y, and Z in `resolvedDestination`, and that the character arrives
on solid land with orientation preserved. Repeat one successful request
unchanged and confirm the response has `"replayed": true`, the same resolved
destination, and no second move.

Send the old nested `destination` object with Z, then send a top-level `z`.
Both must return `400` with `legacy_location_contract` and must not move the
character.

Change `coordinateSpace` to `world_tree` and then `special_area`; both must be
rejected. Move either character into a dungeon or other special area and confirm
player-to-player teleport is rejected.

## Audit and disable

Check `PalCenterCompanion/config/PalCenterCompanion.teleport-audit.jsonl`. It
must contain requested, successful, replayed, and rejected attempts without the
API token. A successful location record must contain requested X/Y and resolved
X/Y/Z. A bounded prior file may exist with the `.1` suffix.

After testing, stop PalServer, set `AdminActionsEnabled=false`, and restart.
Confirm every teleport action is `false` in the capabilities response.
