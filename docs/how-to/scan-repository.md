# Dagdocdiff symbol index

A scan of the Dagdocdiff checkout produces a JSON index of its symbols and source locations. These commands use the repository root and its pinned development shell, `nix develop path:.`.

```sh
cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
build/dagdocdiff scan . build > build/symbols.json
```

## Symbol IDs and locations

The `symbols.json` entry for [the snapshot analyzer](../../src/Analyzer.cpp#L476) has an `id` that starts with `dagdocdiff::analyze` and includes its parameter types. Its `definition` gives the file and line. The `declarations` array lists other indexed locations for that symbol.

The [scan output reference](../reference/cli.md#scan-output) lists the remaining fields.
