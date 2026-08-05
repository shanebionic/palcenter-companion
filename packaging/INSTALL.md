# PalCenter Companion UAT

1. Stop PalServer.
2. Copy the `PalCenterCompanion` folder from this package into:
   `PalServer/Pal/Binaries/Win64/ue4ss/Mods/`
   Keep the existing `PalDefender` and `Offline Raid Protection` folders.
3. To change the listener, edit `PalCenterCompanion/config/PalCenterCompanion.ini`:
   set `BindAddress` and `Port`.
4. Start PalServer. The API token is stored in
   `PalCenterCompanion/config/PalCenterCompanion.token` after first startup.
5. Verify it is running with `http://<bind-address>:<port>/palcenter/v1/health`.
6. To remove it after testing, stop PalServer and delete the `PalCenterCompanion`
   folder. Do not delete `PalDefender` or `Offline Raid Protection`.
