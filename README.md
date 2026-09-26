# krkr2

The KiriKiri2 (krkr2) engine runtime for [AetherKiri](https://github.com/AetherKiri/AetherKiri):
the TJS2 script VM, the core engine modules (base/environ/extension/plugin/
movie/sound/visual/utils) and the bundled native plugins, extracted from the
AetherKiri main repository with full history (AetherKiri/AetherKiri#241).

## How it is consumed

This repository is not built standalone: the AetherKiri checkout embeds it as
the `packages/krkr2` submodule and wires it through `add_subdirectory()`,
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
checkout's `AETHERKIRI_KRKR2_DIR` cache variable at a local krkr2 working
copy.

## Layout

```
core/       krkr2core umbrella target: tjs2, core_*_module, tvpgl_simd
plugins/    krkr2plugin and the bundled plugin targets
external/   libbpg (and the dormant minizip copy)
tests/      tjs2 and plugin test suites (wired by the consumer)
tools/      xp3 / xp3_select CLI, plugin_gap_audit.py
```

## License

GPL-3.0-or-later, same as AetherKiri. See `LICENSE`. Third-party notices for
the full distribution are preserved in the AetherKiri repository's
`THIRD_PARTY_LICENSES.md`.
