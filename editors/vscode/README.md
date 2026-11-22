# Dagdocdiff for VS Code

Review documentation in VS Code’s Source Control view. Dagdocdiff groups files with pending reviews and broken links. Open a file in either group to see its reported sections. Use the check mark next to a file to record an approval.

In C and C++ files, an end-of-line arrow jumps to the Markdown that links to that line. If several links reach the same line, choose one from the list.

## Setup

Use a Dagdocdiff build with `check --json`. From the Dagdocdiff checkout, build the CLI in the Nix development shell.

```sh
nix develop path:. -c cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
nix develop path:. -c cmake --build build
```

To try the extension from source, run `code --extensionDevelopmentPath="$PWD/editors/vscode" "$PWD"` from the checkout. You can also install a packaged `.vsix` from the Extensions view.

Open a trusted local workspace containing the C or C++ project. The extension looks for `compile_commands.json` in `build` or the source directory. It uses the binary in the build directory when available, or finds `dagdocdiff` on VS Code's `PATH`. The project’s `docs` directory is the default Markdown directory when present.

The `dagdocdiff.executable`, `dagdocdiff.sourceDir`, `dagdocdiff.buildDir`, and `dagdocdiff.docsDir` settings override these paths. Relative settings start at the workspace folder.

The extension refreshes after source and Markdown files change on disk. To refresh on demand, use **Dagdocdiff: Check Documentation**. Keep `editor.inlayHints.enabled` on to see the source arrows.
