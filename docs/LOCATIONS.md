# Player locations

PalCenter Companion can report where connected players are according to the
Palworld server. PalCenter uses this information to keep the Living World Map
accurate when a player enters a special area.

`GET /palcenter/v1/locations` returns the latest in-memory location for each
connected player. The endpoint requires the Companion bearer token.

The first supported spaces are:

- `palpagos` — the player is in the main world and can be shown on the Palpagos map.
- `special_area` — Palworld reports an active stage instance. PalCenter shows a
  friendly off-map state rather than plotting those coordinates on Palpagos.

The Companion reads Palworld's replicated `CachedPlayerLocation` and
`EnteringStageInstanceId` state. It does not classify a stage as a dungeon,
tower, arena, or World Tree unless the game provides a separate reliable signal.
No coordinate ranges or transition heuristics are used.

Locations are sampled every two seconds, kept in memory, and removed when the
player leaves. They are not sent anywhere except to an authenticated local
PalCenter client.
