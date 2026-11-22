# Code changes and linked docs

The docs check reports Markdown sections affected by a change to linked code. This example runs from the repository root with an executable and a compilation database in `build`. The [Dagdocdiff symbol index guide](scan-repository.md) shows that build setup.

```sh
build/dagdocdiff docs check . build docs
```

The report identifies the page and heading. A broken or retargeted link identifies a declaration location to check. A dependency path leads to the code change. A change in behavior can require an edit to the affected claim.

A section approval records what the code author checked and why the current text matches. The arguments are the page path relative to `docs`, the heading path from the report, and the reason.

```sh
build/dagdocdiff docs approve . build docs explanation/symbol-graphs.md \
  'How Dagdocdiff tracks code changes / Recursion changes how hashes propagate' \
  'The explanation matches how this revision hashes dependency cycles.'
build/dagdocdiff docs check . build docs
```

The same command records removal of an approved section or all its tracked links. One commit can contain the code change, any documentation edits, and the updated approval file. The [documentation check reference](../reference/cli.md#documentation-check) describes the file format and link rules.
