# Code changes and linked docs

Dagdocdiff reports Markdown sections affected by a change to linked code. This example runs from the repository root with an executable and a compilation database in `build`. The [Dagdocdiff symbol index guide](scan-repository.md) shows that build setup.

```sh
build/dagdocdiff check --build-dir build --docs-root docs
```

The report identifies the page and heading. A broken or retargeted link identifies a declaration location to check. A dependency path leads to the code change. A change in behavior can require an edit to the affected claim.

A section approval records a review of the current text and its linked code. Pass the page path relative to `docs` and the heading path from the report. Use `--reason` if you want to add a note.

```sh
build/dagdocdiff approve --build-dir build --docs-root docs explanation/symbol-graphs.md \
  'How Dagdocdiff tracks code changes / Recursion changes how hashes propagate'
build/dagdocdiff check --build-dir build --docs-root docs
```

The same command records removal of an approved section or all its tracked links. One commit can contain the code change, any documentation edits, and the updated approval file. The reference describes the file format and link rules. [Source links and approvals](../reference/cli.md#source-links-and-approvals)
