# Coordinate Spaces

Coordinates have meaning only within the world or instance that produced them. A coordinate space identifies that context before any consumer projects a point onto a visual map.

Initial conceptual identifiers include:

- `palpagos`
- `world_tree`
- `dungeon_layout_x`
- `arena`
- `tower`
- `unknown`

These names are examples for contract design, not claims that detection exists. Concrete identifiers require evidence from supported server builds before production use.

## Space versus projection

A coordinate space answers: **where does this coordinate system apply?**

A map projection answers: **how is a coordinate in that space drawn on a particular image or view?**

Keeping them separate prevents several errors:

- interpreting points from two spaces as a continuous movement path;
- calculating distance or speed across a dungeon transition;
- plotting an instanced coordinate on the Palpagos surface map;
- embedding UI image dimensions in authoritative server data;
- changing event identity when a map asset or calibration changes.

The Companion should report the most specific authoritative space it can prove. When it cannot determine a space, it must report `unknown` rather than guess. PalCenter owns projection and presentation decisions for supported spaces.

## Future contract properties

A mature coordinate-space value may include a stable identifier, instance identity where safe, lifecycle information, and transition evidence. Dungeon layout identity and dungeon instance identity are separate concerns. Contracts must avoid exposing sensitive implementation addresses or unstable engine object names.
