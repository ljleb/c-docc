# c-docc documentation

c-docc is a command-line documentation checker for C and C++ projects. It checks Markdown links to source code and reports sections that need review.

## Motivation

A guide says that a parser accepts empty input and links to its declaration. An edit to a helper can change how empty input is handled while the declaration stays in place. The guide can become inaccurate even though the linked code looks the same.

c-docc follows the parser's tracked dependencies to the helper and identifies the affected section. The person changing the code can check that section, edit it, and record why the current text applies. c-docc can run locally or in CI. The docs are ordinary Markdown.

## Published package

On Linux with Nix flakes enabled, replace `RELEASE_TAG` with the GitHub tag you want to run.

```sh
nix run github:ljleb/c-docc/RELEASE_TAG -- --help
```

Nix fetches the tagged source and builds the package or reuses a cached build. A c-docc checkout is optional. An analysis uses the `compile_commands.json` for the C or C++ project being checked.

The [repository README](../README.md#build) has the build commands for a checkout.

## [Tutorials](tutorials/)

- [Change a retry limit and update its docs](tutorials/first-doc-check.md) follows one linked section from an approved starting point through a code change and a new approval.
- [Trace a change in a delivery estimate](tutorials/first-diff.md) follows a change in depot sorting time through a caller to a guide.

## [How-to guides](how-to/)

- [Git revision comparison](how-to/compare-revisions.md) covers checkout setup, compilation databases, and dependency paths in the diff.
- [Code changes and linked docs](how-to/review-linked-docs.md) covers broken links, changed dependencies, and section approvals.
- [c-docc symbol index](how-to/scan-repository.md) covers symbol IDs, declarations, and definition locations.
- [Published 1.0.0 build](how-to/build-release.md) covers building the analyzer from the release tag.

## [Explanation](explanation/)

- [How c-docc tracks code changes](explanation/symbol-graphs.md) explains symbol identity, dependency cycles, and how a change reaches a linked section.

## [Reference](reference/)

- [c-docc command and output reference](reference/cli.md) covers command syntax, exit statuses, JSON fields, and source link rules.
