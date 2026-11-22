# Dagdocdiff

Dagdocdiff is a command-line documentation checker for C and C++ projects. It follows source links in Markdown and flags the sections that need review when linked code or a tracked dependency changes.

The [published Nix package](docs/README.md#published-package) runs directly from the 1.1.0 tag on Linux.

## Build v1

Quick build from the repository root. The flake pins Clang and LLVM.

```sh
nix build path:.
./result/bin/dagdocdiff --version
```

The version command prints `dagdocdiff 1.1.0`. The [dependency diff walkthrough](docs/tutorials/first-diff.md) includes a development build. The [published tag build](docs/how-to/build-release.md) uses the 1.0.0 source.

## Documentation

The [documentation index](docs/README.md) links to the tutorial, how-to guides, explanation, and reference. The [roadmap](ROADMAP.md) covers current scope and planned work.
