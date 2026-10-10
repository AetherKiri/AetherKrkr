# AetherKrkr

The KiriKiri2 (krkr2) engine runtime for [AetherKiri](https://github.com/AetherKiri/AetherKiri):
the TJS2 script VM, the core engine modules (base/environ/extension/plugin/
movie/sound/visual/utils) and the bundled native plugins, extracted from the
AetherKiri main repository with full history (AetherKiri/AetherKiri#241).

## How it is consumed

This repository is not built standalone: the AetherKiri checkout embeds it as
the `packages/AetherKrkr` submodule and wires it through `add_subdirectory()`,
providing:

- `KRKR2_ENGINE_ABI_INCLUDE_DIR` — the engine_api ABI headers of the
  consuming checkout (`engine_gpu_bridge.h`, `TextTransform.h`). The visual
  and utils core modules expose these headers to their consumers, and engine
  sources resolve `TVPTransformText`/`TVPPrefetchText` against the host
  library across the DSO boundary.
- `KRKR2_PSDFILE_PACKAGE_ROOT` — the `psdfile` adapter package (a submodule
  of the consuming checkout) that the psdfile plugin compiles against.

The integration glue (the `aether_krkr2_runtime` legacy-services host, the
engine_api dispatch layer, the Godot extension) stays in the AetherKiri
repository. Test suites under `tests/` and the CLI tools under `tools/`
(`xp3`, `xp3_select`, `plugin_gap_audit.py`) are also wired from the
consuming checkout's `tests/` and `tools/` CMake lists.

To iterate on the engine without committing gitlink bumps, point the main
checkout's `AETHERKIRI_KRKR_DIR` cache variable at a local krkr2 working
copy.

## Layout

```
core/       krkr2core umbrella target: tjs2, core_*_module, tvpgl_simd
plugins/    krkr2plugin and the bundled plugin targets
external/   libbpg (and the dormant minizip copy)
tests/      tjs2 and plugin test suites (wired by the consumer)
tools/      xp3 / xp3_select CLI, plugin_gap_audit.py
```

## Compatibility reporting

Platform-limited features must say why they are missing. A game script that
only sees `false`, or a class that silently returns `true` without doing the
work, cannot tell "this build has no implementation" from "the call failed",
and host diagnostics end up guessing.

`DebugIntf.h` therefore exposes one receipt helper:

```cpp
TVPAddCompatReceipt(feature, TJSCompatState::Unavailable, "detail");
```

with four states — `loaded`, `unimplemented` (a compatibility or mock stub
accepts the call without doing the work), `unavailable` (no implementation in
this build), `failed` (an implementation exists but loading failed). Each
`(feature, state)` pair is reported once and logged through spdlog, so it lands
in the same engine log that host diagnostics export already tails:

```
[Compat] feature=<name> state=<state> detail=<text>
```

The receipts are also returned as `compat_receipts` by
`engine_get_plugin_debug_info`, alongside the existing plugin counters. Current
call sites: the plugin load path (`core/plugin/PluginImpl.cpp`) and the
class-level compatibility stubs (`plugins/compatLegacyPlugins.cpp`).

`System.platformTag` reports the target platform for scripts that branch on it
(`windows` / `linux` / `macos` / `android` / `ios` / `unknown`). It is separate
from `System.platformName`, which reports the CPU architecture.

### Member registration and link order

`NCB_ATTACH_CLASS` members are written to the class object when the module is
loaded, and only instances created *afterwards* see them: a `Layer` that already
exists when a module is linked keeps its original member set
(`typeof Layer.mosaic` is `Object` while `typeof layer.mosaic` stays
`undefined`). Titles normally link their plugin DLLs from the first startup
script, before creating layers, but a title that links later would silently lose
the API.

To remove that dependency for the compatibility surface, the modules that
consist purely of legacy-class members are registered during startup, before any
script runs (`core/plugin/PluginImpl.cpp`): addFont, fpslimit, json,
layerExAreaAverage, layerExColor, layerExMosaic, layerExSubImage, messenger,
msdfrender, qrcode, saveStruct, shellExecute, stdio, tasktray, tftSave, toml and
windowExProgress. Modules that also register classes or storage media keep
loading on demand, and `plugin_load_mode=aether_all` already registers
everything.

Verified with a probe fixture: a `Layer` created before linking
`layerExMosaic.dll` now answers `mosaic()` and reports its `unimplemented`
receipt exactly like one created after linking.

## License

GPL-3.0-or-later, same as AetherKiri. See `LICENSE`. Third-party notices for
the full distribution are preserved in the AetherKiri repository's
`THIRD_PARTY_LICENSES.md`.
