# UE4SS Extension Adapter

This directory contains the only UE4SS-specific implementation boundary.

`dllmain.cpp`:

- exports the standard `start_mod` and `uninstall_mod` lifecycle functions;
- waits for `on_unreal_init` before starting the Companion application;
- resolves configuration relative to the installed extension directory;
- routes Companion logs through UE4SS;
- stops the embedded listener during unload.

It intentionally contains no gameplay hooks. The platform-neutral configuration and HTTP implementation live under `include/` and `src/` so they can be tested without Palworld.
