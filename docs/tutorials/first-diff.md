# Dependency diff walkthrough

This example compares C and C++ snapshots with a header macro change and a comment-only edit. It shows which callers each change affects. The commands use a Dagdocdiff checkout and Nix with flakes enabled.

## Development shell and sample snapshots

The development shell supplies the compiler and build tools. The commands use the repository root as their working directory.

```sh
nix develop path:.
```

Inside the shell, the build produces the executable. The test writes the example sources and compilation databases. A compilation database lists how to compile each source file.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build -R '^snapshot-diff$' --output-on-failure
```

One test passes. The first line of each `shared.hpp` shows the difference between the two snapshots.

```sh
head -n 1 build/test-snapshots/base/shared.hpp
head -n 1 build/test-snapshots/head/shared.hpp
```

The first command prints `#define INITIAL 1`. The second prints `#define INITIAL 2`. The change starts in the header.

## Header macro change

The diff command takes a source directory and build directory for each snapshot. Each example directory contains both.

```sh
build/dagdocdiff diff \
  build/test-snapshots/base build/test-snapshots/base \
  build/test-snapshots/head build/test-snapshots/head
```

The JSON output contains three entries in `changes`.

| Symbol | Result | What happened |
| --- | --- | --- |
| `app::a(int)` | `direct` | Its definition uses the macro. |
| `app::b(int)` | `cycle` | It calls `app::a(int)`, which calls it back. |
| `inspect(int)` | `dependency` | It calls `app::a(int)` from a third file. |

The `dependency_path` for `inspect(int)` starts at that caller and ends at `app::a(int)`. Their `after.file` fields show `caller.cpp` and `a.cpp`.

## Comment-only edit

The third snapshot adds a comment near the header definitions and leaves `INITIAL` at `1`.

```sh
build/dagdocdiff diff \
  build/test-snapshots/base build/test-snapshots/base \
  build/test-snapshots/comment-only build/test-snapshots/comment-only
```

The result contains `"changes": []`. In this fixture, the comment leaves the tracked code graph unchanged.

[Git revision comparison](../how-to/compare-revisions.md) shows the same command with separate source and build directories.
