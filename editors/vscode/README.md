# Dagdocdiff for VS Code

Review documentation in VS Code’s Source Control view. Dagdocdiff groups files with pending reviews and broken links. Open a file in either group to see its reported sections. Use the check mark next to a file to record an approval.

In C and C++ files, an end-of-line arrow jumps to the Markdown that links to that line. If several links reach the same line, choose one from the list.

## Install the extension

Download the `.vsix` from the [GitHub releases](https://github.com/ljleb/dagdocdiff/releases). Open the Extensions view in VS Code and select **Install from VSIX…** from the **…** menu. You can also pass the downloaded filename to `code --install-extension` or `codium --install-extension`.

## Connect a project

Open a trusted workspace containing a C or C++ project. The extension looks for `compile_commands.json` in `build` or the source directory. It uses the project’s `docs` directory when present.

The extension offers to install the Dagdocdiff CLI when it cannot find it. On x64 Windows, a VSIX from a GitHub release offers **Install Dagdocdiff**. It downloads the matching Windows ZIP and verifies its checksum. On x64 or ARM64 Linux, **Install with Nix** uses Nix to fetch or build the selected release. Both options keep the CLI in the extension’s storage.

**Later** dismisses the prompt. **Dagdocdiff: Install CLI** in the Command Palette starts the installation on demand.

The extension can also use a CLI that supports `check --json` (Dagdocdiff 1.12.0 or later). It looks in the project’s build directory, the extension’s storage, and then on `PATH`. The `dagdocdiff.executable` setting can select a specific binary. The `dagdocdiff.sourceDir`, `dagdocdiff.buildDir`, and `dagdocdiff.docsDir` settings can select other project directories. Relative paths start at the workspace folder.

The extension refreshes after source and Markdown files change on disk. To refresh on demand, use **Dagdocdiff: Check Documentation**. The source arrows use the editor’s `editor.inlayHints.enabled` setting.
