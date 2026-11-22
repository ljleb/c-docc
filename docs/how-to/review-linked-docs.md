# Code changes and linked docs

Dagdocdiff reports Markdown sections affected by a change to linked code. This example runs from the repository root with an executable and a compilation database in `build`. The [Dagdocdiff symbol index guide](scan-repository.md) shows that build setup.

```sh
build/dagdocdiff check --build-dir build --docs-dir docs
```

The report identifies the page and heading. A broken or retargeted link identifies a declaration location to check. A dependency path leads to the code change. A change in behavior can require an edit to the affected claim.

Run `approve` to go through the sections that need review. Dagdocdiff shows the file, heading, and what changed. Open the file at the reported line to read the section.

```sh
build/dagdocdiff approve --build-dir build --docs-dir docs
```

Enter `y` to approve the current section, `r` to leave it for revision and move on, or `q` to stop. The prompt defaults to leaving the section for revision. Dagdocdiff reports broken links so you can fix them before approving those sections.

To approve a whole Markdown file, pass its path relative to `docs`. Dagdocdiff records each linked section in the file.

```sh
build/dagdocdiff approve --build-dir build --docs-dir docs explanation/symbol-graphs.md
```

To approve a single section, add the heading path from the report. Use `--reason` if you want to add a note.

```sh
build/dagdocdiff approve --build-dir build --docs-dir docs explanation/symbol-graphs.md \
  'How Dagdocdiff tracks code changes / Recursion changes how hashes propagate'
build/dagdocdiff check --build-dir build --docs-dir docs
```

The same command records removal of an approved section or all its tracked links. One commit can contain the code change, any documentation edits, and the updated approval file. The reference describes the file format and link rules. [Source links and approvals](../reference/cli.md#source-links-and-approvals)
