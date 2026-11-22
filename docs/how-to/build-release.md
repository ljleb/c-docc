# Published 1.0.0 build

These commands build the analyzer at tag `1.0.0` in a detached worktree. The example assumes that `origin` points to the published Dagdocdiff repository.

```sh
git fetch origin tag 1.0.0
git worktree add --detach ../dagdocdiff-1.0.0 1.0.0
cd ../dagdocdiff-1.0.0
nix develop path:. -c cmake -S . -B build -G Ninja
nix develop path:. -c cmake --build build
build/dagdocdiff --help
```

The executable is `build/dagdocdiff` in the new worktree. The source identity for this build is Git tag `1.0.0`.
