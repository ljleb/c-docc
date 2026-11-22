# Dependency diff walkthrough

This example shows how a change in a header can affect a caller in another file. The code change is one line in `shared.hpp`.

```diff
-#define INITIAL 1
+#define INITIAL 2
```

The diff reports these three functions. Their definitions stay the same across the example projects.

```cpp
// a.cpp
int app::a(int n) { return n ? b(n - 1) : INITIAL; }

// b.cpp
int app::b(int n) { return n ? a(n - 1) : helper(); }

// caller.cpp
using Count = int;
int inspect(Count n) { return app::a(n) + c_value(); }
```

The complete projects include the headers and other definitions needed to run the example.

- [`base`](../../examples/dependency-diff/base/) is the original project.
- [`head`](../../examples/dependency-diff/head/) changes the value of `INITIAL` in a header.
- [`comment-only`](../../examples/dependency-diff/comment-only/) adds a comment and keeps the original value.

## Local tool and example files

The [published Nix package](../README.md#published-package) can run from any directory. This walkthrough uses a Dagdocdiff checkout to build a local executable and run the test that creates the example files.

The checkout commands below use the `1.1.0` tag. Its test generates the same example sources as the linked directories.

```sh
git clone --branch 1.1.0 https://github.com/ljleb/dagdocdiff.git
cd dagdocdiff
```

From the checkout, Nix opens a shell with the compiler, CMake, and Ninja used to build Dagdocdiff.

```sh
nix develop path:.
```

In that shell, the first CMake command configures the build in `build`. The second compiles Dagdocdiff.

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

After the build succeeds, you should have a `build/dagdocdiff` executable.

When you run the `snapshot-diff` test, it prepares the three examples under `build/test-snapshots`. Each directory contains source files and a `compile_commands.json` file. That file lists the compiler options Dagdocdiff needs to read the example code. The test also runs the two comparisons and checks their results.

```sh
ctest --test-dir build -R '^snapshot-diff$' --output-on-failure
```

You should see `snapshot-diff` marked `Passed` in the CTest output. If the test fails, `--output-on-failure` prints its diagnostics.

## Header macro change

The diff compares the original project with the copy that changed the macro. Each directory appears twice because it contains both the source files and `compile_commands.json`.

```sh
build/dagdocdiff diff \
  build/test-snapshots/base build/test-snapshots/base \
  build/test-snapshots/head build/test-snapshots/head
```

You should see three entries in the JSON `changes` array.

| Symbol | Result | What happened |
| --- | --- | --- |
| `app::a(int)` | `direct` | Its definition uses the macro. |
| `app::b(int)` | `cycle` | It calls `app::a(int)`, which calls it back. |
| `inspect(int)` | `dependency` | It calls `app::a(int)` from a third file. |

In the output for `inspect(int)`, expect `dependency_path` to start at that caller and end at `app::a(int)`. Their `after.file` fields should show `caller.cpp` and `a.cpp`.

## Comment-only edit

The `comment-only` copy adds `// This change only shifts source locations.` to `shared.hpp`. `INITIAL` stays at `1`.

```sh
build/dagdocdiff diff \
  build/test-snapshots/base build/test-snapshots/base \
  build/test-snapshots/comment-only build/test-snapshots/comment-only
```

You should see `"changes": []`. The comment leaves the tracked code graph unchanged in this example.

[Git revision comparison](../how-to/compare-revisions.md) shows the same command with separate source and build directories.
