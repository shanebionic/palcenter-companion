# Server Extension Framework Evaluation

Research completed on 2026-08-02. Repository activity dates and versions below reflect that retrieval date and should be revalidated before each compatibility release.

## Recommendation

Build PalCenter Companion as a **UE4SS C++ extension**, compiled against and tested with a pinned Palworld-compatible RE-UE4SS revision.

For the first implementation baseline, the build accepts an external `RE-UE4SS` source checkout through `PALCENTER_UE4SS_ROOT`. This deliberately avoids silently downloading a moving loader revision. The current Palworld ecosystem should be validated against the actively maintained [Okaetsu Palworld fork](https://github.com/Okaetsu/RE-UE4SS) at an exact commit for release builds, while preserving compatibility with upstream UE4SS when Palworld no longer requires fork-specific changes.

UE4SS is recommended because it provides the required in-process C++ lifecycle, automatic loading, logging integration, a familiar `ue4ss/Mods/<Name>/dlls` deployment layout, and a path to future authoritative server hooks. It is established community infrastructure rather than a PalCenter-specific injector.

The recommendation is qualified:

- Upstream stable UE4SS `v3.0.1` was published on 2024-02-14 and requires C++ extensions to be built for its ABI.
- Upstream development remains active (the main repository was pushed on 2026-07-30), but current Palworld projects commonly require a Palworld-specific experimental build.
- PalSchema's current releases explicitly require Okaetsu's `experimental-palworld` UE4SS build; the fork's main branch was pushed on 2026-07-16.
- Native Linux dedicated servers are not officially supported by UE4SS. Linux administrators need a Windows PalServer under Wine or Proton. This limitation must be prominent in every release.
- Every Companion binary therefore needs a published and tested Palworld/UE4SS compatibility matrix.
- UE4SS's C++ build depends on Unreal-derived `UEPseudo` sources that require an authorized Epic-linked GitHub account. Anonymous public CI cannot compile the production ABI. Public CI compiles the Windows DLL against test-only lifecycle declarations and verifies its exports; release builds must additionally compile against the authorized pinned UE4SS source and pass live PalServer validation.

## Options considered

| Approach | Advantages | Disadvantages | Activity and compatibility | Deployment experience | Decision |
| --- | --- | --- | --- | --- | --- |
| UE4SS C++ extension | Documented load/unload lifecycle; native code; UE reflection and future hooks; established logging; widely recognized folder layout | ABI-sensitive; loader installation required; Palworld updates can require a fork; no native Linux server support | Upstream pushed 2026-07-30; official C++ template updated 2025-10-23; Palworld fork pushed 2026-07-16 | Install one compatible UE4SS loader, copy the Companion folder, restart | **Selected** |
| UE4SS Lua extension | Very fast iteration; familiar to many Palworld administrators; used by projects such as Offline Raid Protection | Embedded HTTP and future concurrency require native modules; weaker compile-time safety; Lua/native boundary adds operational complexity | UE4SS Lua APIs are active, but feature capability depends on current Palworld mappings | Simple folder copy after UE4SS | Rejected for the core runtime; useful only for future prototypes |
| PalSchema / Blueprint content | Active Palworld-specific project; useful for schema, data-table, Blueprint, and packaged-content changes | Not designed as an embedded native administration service; does not replace the need for a native listener/lifecycle; currently depends on a specific UE4SS fork | PalSchema pushed 2026-07-26 and released updates in July 2026 | Familiar content package workflow, but inappropriate for this service boundary | Rejected for the Companion runtime |
| Unreal Engine plugin or packaged server plugin | First-class Unreal concepts when source, target rules, and packaging pipeline are available | Palworld is closed-source; exact server target/toolchain is unavailable to community builders; packaged content may create client compatibility concerns; poor drop-in administrator experience | Palworld Modding Kit is active but focuses on Blueprint/content development, not a supported dedicated-server native plugin ABI | More complex cooking/deployment; potentially client-facing | Rejected until Pocketpair provides a supported server-plugin SDK |
| Custom proxy DLL / injector | Complete control; PalDefender demonstrates a direct Windows proxy-loader distribution can work | Reinvents loading, update compatibility, crash isolation, and Wine behavior; PalDefender implementation is closed source, so its safety cannot be independently reused or verified | PalDefender documentation is active and offers Windows and UE4SS variants | Very simple copy for Windows, but PalCenter would own a private loader forever | Rejected; duplicates proven loader infrastructure |

## Established project patterns

### PalDefender

[PalDefender](https://github.com/Ultimeit/PalDefender) is actively documented but closed source. Its Windows package uses a proxy DLL and an in-process DLL, while its Wine/Proton distribution uses UE4SS. It demonstrates that automatic in-process loading and copy/restart installation are accepted administrator patterns, but its private implementation cannot serve as reusable infrastructure.

### Offline Raid Protection

The current [Offline Raid Protection distribution](https://www.curseforge.com/palworld/lua-code-mods/offline-raid-protection) installs under `Pal/Binaries/Win64/ue4ss/Mods` and requires UE4SS. It demonstrates the familiar server-side UE4SS folder workflow, while also illustrating that Lua is adequate for game logic but not the best foundation for an embedded native API service.

### PalSchema

[PalSchema](https://github.com/Okaetsu/PalSchema) is actively maintained and important to the Palworld extension ecosystem. Its purpose is schema, Blueprint, and content extensibility. Current releases require the Palworld-specific UE4SS fork, which is strong compatibility evidence for the loader choice but not a reason to make PalSchema a Companion dependency.

## Embedded HTTP library

The implementation uses [cpp-httplib](https://github.com/yhirose/cpp-httplib), pinned to `v0.52.0` commit `095a5c1caf9e467ff840ee2ffd19be1a7852203b`.

Advantages:

- MIT licensed and header-only;
- actively maintained (release and repository activity on 2026-08-02);
- small integration surface for three local JSON endpoints;
- synchronous bind result, explicit stop, and a listener that can run on a dedicated worker thread;
- a documented WebSocket API is available for a later milestone without implementing it now.

Tradeoffs:

- its default concurrency model must be bounded before exposing high-volume APIs;
- TLS is intentionally not compiled in during this local discovery milestone;
- the API is unauthenticated, so loopback binding is the safe default;
- future WebSocket resource and backpressure behavior still needs a dedicated design review.

Crow and Boost.Beast/Asio were considered. They provide richer routing or lower-level asynchronous control, but add substantially more framework and build complexity than three local discovery endpoints require. If future event-stream load exceeds cpp-httplib's model, the transport can be replaced behind `CompanionHttpServer` without changing the UE4SS lifecycle or API contract.

## Primary sources

- [UE4SS repository](https://github.com/UE4SS-RE/RE-UE4SS)
- [UE4SS C++ extension guide](https://docs.ue4ss.com/dev/guides/creating-a-c%2B%2B-mod.html)
- [UE4SS C++ installation guide](https://docs.ue4ss.com/dev/guides/installing-a-c%2B%2B-mod.html)
- [UE4SS releases](https://github.com/UE4SS-RE/RE-UE4SS/releases)
- [UE4SS C++ template](https://github.com/UE4SS-RE/UE4SSCPPTemplate)
- [Palworld Modding Kit](https://github.com/localcc/PalworldModdingKit)
- [PalSchema](https://github.com/Okaetsu/PalSchema)
- [Palworld-specific RE-UE4SS fork](https://github.com/Okaetsu/RE-UE4SS)
- [PalDefender repository](https://github.com/Ultimeit/PalDefender)
- [PalDefender installation](https://ultimeit.github.io/PalDefender/Installation/)
- [Palworld dedicated-server UE4SS installation](https://pwmodding.wiki/docs/users/ue4ss/installation-server)
- [cpp-httplib](https://github.com/yhirose/cpp-httplib)
