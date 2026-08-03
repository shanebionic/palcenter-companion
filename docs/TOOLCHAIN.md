# Production Toolchain

PalCenter Companion production binaries are reproducible only when built and tested against the exact toolchain below. The pins are release-candidate inputs, not a compatibility claim. Compatibility remains **pending** until the live checklist in [Live PalServer UAT](LIVE-PALSERVER-UAT.md) passes.

| Component | Pinned value |
| --- | --- |
| Palworld Dedicated Server | Steam build `24466863`, game version `v1.0.2.101103` |
| RE-UE4SS repository | `https://github.com/Okaetsu/RE-UE4SS.git` |
| RE-UE4SS branch/revision | `main` at `c838a8acaade1a0f860bdf249f039e58f4e10088` |
| Compiler | Visual Studio 2022 17.14, MSVC 19.44 (`MSVC_VERSION=1944`), x64 |
| Windows SDK | `10.0.26100.0` |
| Rust | `rustc 1.88.0` (`x86_64-pc-windows-msvc`) |
| CMake generator | `Visual Studio 17 2022`, platform `x64` |
| UE4SS build configuration | `Game__Shipping__Win64` |
| C++ runtime | Dynamic multithreaded runtime (`/MD` for Release) |
| CMake | `3.31.6-msvc6`, using the generator above |
| cpp-httplib | v0.52.0, commit `095a5c1caf9e467ff840ee2ffd19be1a7852203b` |

Because Release uses `/MD`, the PalServer host needs the Microsoft Visual C++ 2015–2022 x64 Redistributable. A normal current Palworld/UE4SS installation commonly already has it, but the package does not redistribute it.

## Authorized source preparation

UE4SS's Unreal-derived `UEPseudo` dependency is access-controlled. Contributors must use an Epic-linked GitHub account and comply with its access terms. Never commit or republish that dependency through this repository.

```powershell
git clone https://github.com/Okaetsu/RE-UE4SS.git C:\src\RE-UE4SS
git -C C:\src\RE-UE4SS checkout c838a8acaade1a0f860bdf249f039e58f4e10088
git -C C:\src\RE-UE4SS submodule update --init --recursive
```

The SSH URL used by the authorized submodule requires appropriate GitHub credentials. The build fails before compilation when the checkout, pinned revision, generator, SDK, compiler family, or authorized submodule is missing.

## Production build and package

From a Visual Studio 2022 Developer PowerShell:

```powershell
.\scripts\build-production.ps1 -Ue4ssRoot C:\src\RE-UE4SS
```

Use a short absolute build path when the repository itself is deeply nested; Cargo and the Windows linker can otherwise exceed path limits:

```powershell
.\scripts\build-production.ps1 -Ue4ssRoot C:\src\RE-UE4SS `
  -BuildDirectory C:\pc-companion-build
```

The script configures `PALCENTER_UE4SS_BUILD_MODE=PRODUCTION`, builds the real `main.dll` with UE4SS's `Game__Shipping__Win64` configuration, verifies `start_mod` and `uninstall_mod`, stages the installable directory, validates ZIP contents, and writes SHA-256 values for the DLL and ZIP.

The contract-only CI configuration is deliberately different:

```powershell
cmake -S . -B build-contract -G "Visual Studio 17 2022" `
  -DPALCENTER_UE4SS_BUILD_MODE=CONTRACT
```

It produces `PalCenterCompanion-contract-test.dll`. That file proves PalCenter-owned lifecycle behavior only. It is not ABI-compatible evidence, not installable, and must never be distributed as the Companion.
