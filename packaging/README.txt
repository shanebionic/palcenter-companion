PalCenter Companion 0.1.0
==========================

This package contains the production UE4SS extension. Copy the entire
PalCenterCompanion directory into:

PalServer/Pal/Binaries/Win64/ue4ss/Mods/

Restart PalServer, then verify the startup messages in UE4SS.log and request:

http://127.0.0.1:8213/palcenter/v1/health

The API is unauthenticated in this milestone. Keep it on loopback unless
performing controlled private-network testing. Never expose port 8213 directly
to the public Internet.

Compatibility and configuration instructions:
https://github.com/shanebionic/palcenter-companion/blob/main/docs/INSTALLATION.md

PalCenter Companion is unofficial and is not affiliated with Pocketpair, Inc.
