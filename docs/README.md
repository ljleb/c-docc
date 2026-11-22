# Dagdocdiff documentation

Dagdocdiff is a command-line documentation checker for C and C++ projects. It checks Markdown links to source code and reports sections that need review.

## Motivation

A guide says that a parser accepts empty input and links to its declaration. An edit to a helper can change how empty input is handled while the declaration stays in place. The guide can become inaccurate even though the linked code looks the same.

Dagdocdiff follows the parser's tracked dependencies to the helper and identifies the affected section. The person changing the code can check that section, edit it, and record why the current text applies. The check can run locally or in CI. The docs are ordinary Markdown.

## Build and worked example

The [dependency diff walkthrough](tutorials/first-diff.md) uses a sample header edit to show direct changes, recursion cycles, and changes that reach callers.

These commands build the tool and print its command synopsis from the repository root. They use Nix with flakes enabled.

```sh
nix build path:.
./result/bin/dagdocdiff --help
```

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
