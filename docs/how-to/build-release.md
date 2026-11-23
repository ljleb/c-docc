# Published 1.0.0 build

These commands build the analyzer at tag `1.0.0` in a detached worktree. The example assumes that `origin` points to the published c-docc repository.

```sh
git fetch origin tag 1.0.0
git worktree add --detach ../c-docc-1.0.0 1.0.0
cd ../c-docc-1.0.0
nix develop path:. -c cmake -S . -B build -G Ninja
nix develop path:. -c cmake --build build
build/dagdocdiff --help
```

Tag `1.0.0` predates the rename, so the executable in that worktree is `build/dagdocdiff`. The source identity for this build is Git tag `1.0.0`.
