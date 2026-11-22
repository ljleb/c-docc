# Dagdocdiff documentation

Dagdocdiff is a command-line documentation checker for C and C++ projects. It checks Markdown links to source code and reports sections that need review.

## Motivation

A guide says that a parser accepts empty input and links to its declaration. An edit to a helper can change how empty input is handled while the declaration stays in place. The guide can become inaccurate even though the linked code looks the same.

Dagdocdiff follows the parser's tracked dependencies to the helper and identifies the affected section. The person changing the code can check that section, edit it, and record why the current text applies. The check can run locally or in CI. The docs are ordinary Markdown.

## Published package

On Linux with Nix flakes enabled, this command runs Dagdocdiff 1.1.0 directly from its GitHub tag.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- --help
```

Nix fetches the tagged source and builds the package or reuses a cached build. A Dagdocdiff checkout is optional. An analysis uses the `compile_commands.json` for the C or C++ project being checked.

## Checkout build and worked example

The [repository README](../README.md#build-v1) has the build commands for a checkout. The [dependency diff walkthrough](tutorials/first-diff.md) builds a local executable and compares three copies of a small example project.

## Documentation

| Form | Contents |
| --- | --- |
| [Tutorial](tutorials/first-diff.md) | A sample C and C++ project, two comparisons, and the expected output. |
| [How-to guides](#task-guides) | Commands and examples for comparing revisions, scanning a checkout, updating linked docs, and building a release. |
| [Explanation](explanation/symbol-graphs.md) | Symbol identity, dependency cycles, and the reasons a linked section can become stale. |
| [Reference](reference/cli.md) | Command syntax, exit statuses, JSON fields, and source link rules. |

## Task guides

- [Git revision comparison](how-to/compare-revisions.md) covers checkout setup, compilation databases, and dependency paths in the diff.
- [Code changes and linked docs](how-to/review-linked-docs.md) covers broken links, changed dependencies, and section approvals.
- [Dagdocdiff symbol index](how-to/scan-repository.md) covers symbol IDs, declarations, and definition locations.
- [Published 1.0.0 build](how-to/build-release.md) covers building the analyzer from the release tag.
