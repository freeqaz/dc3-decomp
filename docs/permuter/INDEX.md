# C++ Permuter → now `decomp-synth`

The C++ source permuter (formerly `scripts/permuter/` in this repo) has been
extracted into its own standalone, open-source tool:

> **[`../../../decomp-synth`](../../../decomp-synth)** — package `decomp_synth`.

All tool documentation — usage, configuration, pattern reference, architecture,
search strategy, the BSF engine, the AI advisor, and the evolution history —
now lives there:

- Tool README & full usage / option reference: [`../../../decomp-synth/README.md`](../../../decomp-synth/README.md)
- Docs index (architecture, search, guidance, patterns): [`../../../decomp-synth/docs/README.md`](../../../decomp-synth/docs/README.md)

## Running it against DC3

`decomp_synth` is installed editable in the shared venv, and DC3 ships its
`decomp-synth.json` at the repo root (`msvc` / build `373307D9` / mirrored obj
layout). Run it from the repo root:

```bash
# Easiest: the /permute skill
/permute AsyncFile::Seek

# Or directly
venv/bin/python -m decomp_synth.scan_and_permute --symbol 'AsyncFile::Seek' \
    --max-rounds 10 --max-variants 100 --plateau-limit 3 --chain-depth 5

# Scan a whole unit (dry run)
venv/bin/python -m decomp_synth.scan_and_permute --unit 'system/obj/*' --dry-run
```

It also drives the orchestrator's `permute` subcommand
(`scripts/decomp_orchestrate.py permute <symbol>`), which calls into
`decomp_synth` directly.

## What stays in this repo

- **[ghidra-stress-test/](ghidra-stress-test/)** — DC3-specific findings from the
  Ghidra-guided permuter stress-test campaign (per-function analyses of DC3
  classes, the testing protocol, and session summaries). These are tied to DC3's
  binary and stay project-side.
- The **BSF compiler-trace tooling** itself (`tools/compiler_trace/`) and its
  integration tests stay project-side — they need DC3's wibo + MSVC + GDB. The
  *design* of the BSF engine is documented in
  [`../../../decomp-synth/docs/bsf-engine.md`](../../../decomp-synth/docs/bsf-engine.md).
