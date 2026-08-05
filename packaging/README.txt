PalCenter Companion 0.3.2
==========================

This package contains the production UE4SS extension. Copy the entire
PalCenterCompanion directory into:

PalServer/Pal/Binaries/Win64/ue4ss/Mods/

Restart PalServer, then verify the startup messages in UE4SS.log and request:

http://127.0.0.1:8213/palcenter/v1/health

On first startup, the Companion creates config/PalCenterCompanion.token. Copy
that token into PalCenter's Advanced Companion Connection settings. Keep the
file private; it is not logged or returned by the API. Only the minimal health
probe is public. Keep port 8213 on loopback or a trusted private network.

Privileged teleport actions are disabled by default. Read the bundled
ADMIN-ACTIONS-UAT.md before enabling them on a private test server.

Compatibility and configuration instructions:
https://github.com/shanebionic/palcenter-companion/blob/main/docs/INSTALLATION.md

PalCenter Companion is unofficial and is not affiliated with Pocketpair, Inc.
