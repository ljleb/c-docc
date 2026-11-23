# Build from a release tag

These commands build c-docc at a Git tag in a separate worktree. Run them from a checkout whose `origin` points to the c-docc repository. Replace `RELEASE_TAG` with the c-docc release tag you want to build.

```sh
tag=RELEASE_TAG
git fetch origin tag "$tag"
git worktree add --detach "../c-docc-$tag" "$tag"
cd "../c-docc-$tag"
nix develop path:. -c cmake -S . -B build -G Ninja
nix develop path:. -c cmake --build build
build/c-docc --version
```
