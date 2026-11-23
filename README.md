# c-docc

c-docc is a command-line documentation checker for C and C++ projects. It follows source links in Markdown and flags the sections that need review when linked code or a tracked dependency changes.

The published Nix package runs from a release tag on Linux.

## Build

Quick build from the repository root. The flake pins Clang and LLVM.

```sh
nix build path:.
./result/bin/c-docc --version
```

## Documentation

The [documentation](docs/README.md) includes tutorials, how-to guides, explanation, and reference.

The [VS Code extension](editors/vscode/README.md) brings documentation review and source backlinks into the editor.

## Roadmap

The [roadmap](ROADMAP.md) lists planned work.
