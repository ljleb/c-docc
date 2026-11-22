# Git revision comparison

Dagdocdiff compares two source checkouts with their compilation databases. Matching build options keep the comparison focused on source changes.

## Checkouts and compilation databases

The [diff command](#diff-command-and-output) accepts existing checkouts. This example creates detached worktrees for `HEAD~1` and `HEAD` from the target repository. `base_ref` and `head_ref` hold the refs to compare.

```sh
base_ref=HEAD~1
head_ref=HEAD
work=$(mktemp -d)

git worktree add --detach "$work/base" "$base_ref"
git worktree add --detach "$work/head" "$head_ref"
```

These CMake commands export compilation databases for Ninja builds. The comparison uses matching project-specific toolchain and configuration options for both revisions.

```sh
cmake -S "$work/base" -B "$work/base-build" -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake -S "$work/head" -B "$work/head-build" -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Projects with generated headers need those headers in each checkout before analysis.

## Diff command and output

`tool` holds the absolute path to the Dagdocdiff executable. The four arguments are the base source and build directories, followed by the head source and build directories.

```sh
tool=/absolute/path/to/dagdocdiff
"$tool" diff "$work/base" "$work/base-build" "$work/head" "$work/head-build" > changes.json
```

`base_findings` and `head_findings` report gaps in the analysis. For a `dependency` or `cycle` entry, `dependency_path` leads from the affected symbol to the reported cause. The [diff output reference](../reference/cli.md#diff-output) lists the remaining fields.

Clang diagnostics on standard error identify analysis failures. Moved checkouts need compilation databases with current paths. Each snapshot requires one compile command per source input.
