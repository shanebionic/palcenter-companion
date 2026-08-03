# Location capability research

Research date: August 3, 2026

## Selected game signals

The Companion uses two reflected, server-owned `APalPlayerState` values:

- `CachedPlayerLocation` for the player's current X, Y, and Z position.
- `RecordData.EnteringStageInstanceId` to determine whether Palworld considers
  the player to be entering an active stage instance.

These fields were compiled against the Palworld Dedicated Server Steam build
24466863 used by the existing Companion UAT environment and cross-checked against the generated
Palworld SDK declarations in
[`localcc/PalworldModdingKit`](https://github.com/localcc/PalworldModdingKit).
[UE4SS documents `GetValuePtrByPropertyNameInChain`](https://docs.ue4ss.com/dev/guides/accessing-ue-properties-c%2B%2B.html)
as its supported C++ property access mechanism.

## Why this boundary is conservative

Palworld exposes a stage instance ID and a separate `EPalStageType`, but the
currently proven player-state path does not expose a reliable stage type in the
same snapshot. The Companion therefore reports only:

- `palpagos` when no active stage instance is present;
- `special_area` when Palworld reports an active stage instance.

It does not infer dungeon, tower, arena, or World Tree membership from coordinate
ranges, data-layer names, or transitions. Those labels require a separately
verified authoritative game signal in a future milestone.

## Alternatives rejected

- Coordinate bounding boxes: fragile and equivalent to the heuristic approach
  this project intentionally retired.
- Data-layer name matching: useful during research, but names are internal and
  can change between Palworld builds.
- The official REST `/players` endpoint alone: it provides X and Y but no
  coordinate-space identity.

## Runtime behavior

The production adapter samples active players on the game thread every two
seconds. The HTTP layer reads a thread-safe in-memory snapshot. Records are
removed when a player leaves and are never persisted or sent externally.

The production DLL loaded successfully beside Offline Raid Protection and the
authenticated health, capabilities, and empty-location contracts were verified.
A joined-player and special-stage transition remain manual UAT steps.
