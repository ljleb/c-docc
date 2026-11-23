# c-docc for VS Code

Review documentation in VS Code’s Source Control view. c-docc groups files with pending reviews and broken links. Click a file to open its first flagged line. The scrollbar marks the other lines that need review. Hover over a diagnostic or open the Problems panel to see why the line was flagged. The diagnostic links to the changed definition when c-docc finds it. Use the check mark next to a file to approve all its linked sections. Files with broken links need those links fixed before approval.

Save an edit to a linked section to clear its review. To put a file or folder back in Needs Review, right-click it in Explorer and choose **Invalidate Documentation**.

In C and C++ files, the CodeLens above a linked line gives the Markdown file and line number. Click the label to open the Markdown, or hover to read the referring line. If several links reach the same line, the label opens a list of references. CodeLens works with inlay hints turned off.

## Install the extension

Download the `.vsix` from the [GitHub releases](https://github.com/ljleb/c-docc/releases). Open the Extensions view in VS Code and select **Install from VSIX…** from the **…** menu. You can also pass the downloaded filename to `code --install-extension` or `codium --install-extension`.

## Package from a checkout

From the repository root, the Nix development shell provides Node.js and `vsce`.

```sh
nix develop path:. --command npm --prefix editors/vscode run package
```

The command reads `VERSION` and writes `build/docc-<version>.vsix`. The packaged extension uses the matching c-docc GitHub release for automatic CLI installation.

## Connect a project

Open a trusted workspace containing a C or C++ project. The extension looks for `compile_commands.json` in `build` or the source directory. It uses the project’s `docs` directory when present.

The extension offers to install the c-docc CLI when it cannot find it. On x64 Windows, the released VSIX offers **Install c-docc**. It downloads the matching Windows ZIP and verifies its checksum. On x64 or ARM64 Linux, the released VSIX offers **Install with Nix**, which uses Nix to fetch or build the selected release. Both options keep the CLI in the extension’s storage.

**Later** dismisses the prompt. **c-docc: Install CLI** in the Command Palette starts the installation on demand.

The extension can also use a c-docc CLI that supports `check --json`. It looks in the project’s build directory, the extension’s storage, and then on `PATH`. The `c-docc.executable` setting can select a specific binary. The `c-docc.sourceDir`, `c-docc.buildDir`, and `c-docc.docsDir` settings can select other project directories. Relative paths start at the workspace folder.

The extension refreshes after source and Markdown files change on disk. To refresh on demand, use **c-docc: Check Documentation**.
