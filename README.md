# Dagdocdiff

Dagdocdiff is a command-line documentation checker for C and C++ projects. It follows source links in Markdown and flags the sections that need review when linked code or a tracked dependency changes.

The published Nix package runs from the 1.1.0 tag on Linux.

## Build v1

Quick build from the repository root. The flake pins Clang and LLVM.

```sh
nix build path:.
./result/bin/dagdocdiff --version
```

The version command prints `dagdocdiff 1.1.0`.

## Documentation

The [documentation](docs/README.md) includes tutorials, how-to guides, explanation, and reference.

## Roadmap

The [roadmap](ROADMAP.md) covers current scope and planned work.
