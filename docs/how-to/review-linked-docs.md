# Code changes and linked docs

c-docc reports Markdown sections affected by a change to linked code. This example runs from the repository root with an executable and a compilation database in `build`. The [c-docc symbol index guide](scan-repository.md) shows that build setup.

```sh
build/c-docc check --build-dir build --docs-dir docs
```

The report identifies the page and heading. It shows the text and line of an affected link. A dependency path leads to the changed code, and the last location points to its definition when c-docc can find it. A change in behavior can require an edit to the affected claim.

Run `approve` to go through the sections that need review. c-docc shows the file, heading, and what changed. Open the file at the reported line to read the section.

```sh
build/c-docc approve --build-dir build --docs-dir docs
```

Enter `y` to approve the current section, `r` to leave it for revision and move on, or `q` to stop. The prompt defaults to leaving the section for revision. c-docc reports broken links so you can fix them before approving those sections.

To approve a whole Markdown file, pass its path relative to `docs`. c-docc records each linked section in the file.

```sh
build/c-docc approve --build-dir build --docs-dir docs explanation/symbol-graphs.md
```

To approve a single section, add the heading path from the report. Use `--reason` if you want to add a note.

```sh
build/c-docc approve --build-dir build --docs-dir docs explanation/symbol-graphs.md \
  'How c-docc tracks code changes / Recursion changes how hashes propagate'
build/c-docc check --build-dir build --docs-dir docs
```

The same command records removal of an approved section or all its tracked links. One commit can contain the code change, any documentation edits, and the updated approval file. The reference describes the file format and link rules. [Source links and approvals](../reference/cli.md#source-links-and-approvals)
