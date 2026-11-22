# Dagdocdiff

Dagdocdiff compares the functions, methods, and records in two C or C++ snapshots. It reports changes to a definition and changes that reach it through dependencies.

## Build and test

The Nix flake pins the Clang and LLVM versions. Run these commands from the repository root.

```sh
nix develop path:.
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Analyze a source snapshot

Supply the source root and a build directory that contains its `compile_commands.json`. For a CMake project, configure the build with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`. If the source needs generated headers, generate them before running the analyzer.

```sh
build/dagdocdiff scan /path/to/source /path/to/build
build/dagdocdiff diff /path/to/base-source /path/to/base-build /path/to/head-source /path/to/head-build
```

Both commands write JSON. A scan includes symbol IDs, definition locations, dependency edges, local and recursive hashes, and coverage findings. A diff reports additions, removals, direct changes, changes within a recursion cycle, and changes reached through outgoing dependencies. An empty change list represents equal hashes for the tracked symbols. Compilation errors and conflicting definitions return a nonzero exit status.

## Current scope

Analyze one build configuration per snapshot. The graph covers ordinary functions, methods, records, direct calls, referenced records, and inheritance. Internal-linkage symbols include the main translation unit path in their IDs. Headers provide definition locations and contribute to hashes through their expanded tokens.

The report identifies some common gaps, such as indirect calls and unresolved function definitions. Template instantiations, runtime dispatch targets, and documentation approvals are future work. See [ROADMAP.md](ROADMAP.md).
