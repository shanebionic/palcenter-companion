# UE4SS Extension Adapter

This directory contains the only UE4SS-specific implementation boundary.

`dllmain.cpp`:

- exports the standard `start_mod` and `uninstall_mod` lifecycle functions;
- waits for `on_unreal_init` before starting the Companion application;
- resolves configuration relative to the installed extension directory;
- routes Companion logs through UE4SS;
- stops the embedded listener during unload.

It intentionally contains no gameplay hooks. The platform-neutral configuration and HTTP implementation live under `include/` and `src/` so they can be tested without Palworld.

Public CI uses the minimal declarations under `tests/ue4ss_stubs` to compile a clearly named contract-test DLL, verify `start_mod` and `uninstall_mod`, load it, exercise initialization and the health endpoint, and unload it cleanly. Those declarations and that DLL are not shipped and do not claim UE4SS ABI compatibility. Production builds require an authorized, pinned RE-UE4SS checkout through `PALCENTER_UE4SS_ROOT` plus live PalServer validation.
