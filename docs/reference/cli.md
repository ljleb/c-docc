# c-docc command and output reference

This reference covers the CLI built from this checkout. Scan and diff output use JSON schema version `1`.

## Commands

The [command dispatcher](../../src/main.cpp#L178) handles global flags and selects the subcommand.

| Command | Result |
| --- | --- |
| [`c-docc --help`](../../src/main.cpp#L181) | Prints usage. |
| [`c-docc --version`](../../src/main.cpp#L185) | Prints the executable's version. |
| [`c-docc scan <source-root> <build-dir>`](../../src/main.cpp#L189) | Prints the symbols and findings for one snapshot. |
| [`c-docc diff <base-root> <base-build-dir> <head-root> <head-build-dir>`](../../src/main.cpp#L196) | Prints changes and findings for two snapshots. |
| [`c-docc check [options]`](../../src/main.cpp#L172) | Checks local links and recorded reviews in Markdown. |
| [`c-docc approve [options]`](../../src/main.cpp#L161) | Prompts for each section that needs approval. |
| [`c-docc approve <page> [options]`](../../src/main.cpp#L166) | Records a review of all linked sections in a Markdown file. |
| [`c-docc approve <page> <section> [options]`](../../src/main.cpp#L171) | Records an explicit review of one section. |
| [`c-docc invalidate <file-or-directory> [options]`](../../src/main.cpp#L152) | Requests review of linked sections in a Markdown file or directory. |

## Arguments

| Argument | Command | Meaning |
| --- | --- | --- |
| [`source-root`](../../src/main.cpp#L190) | `scan` | Directory that bounds the source files included in the snapshot. |
| [`build-dir`](../../src/main.cpp#L190) | `scan` | Directory containing `compile_commands.json`. |
| [`base-root`](../../src/main.cpp#L197) | `diff` | Source directory for the earlier snapshot. |
| [`base-build-dir`](../../src/main.cpp#L197) | `diff` | Directory containing `compile_commands.json` for the earlier snapshot. |
| [`head-root`](../../src/main.cpp#L200) | `diff` | Source directory for the later snapshot. |
| [`head-build-dir`](../../src/main.cpp#L200) | `diff` | Directory containing `compile_commands.json` for the later snapshot. |
| [`page`](../../src/main.cpp#L167) | `approve` | Markdown path relative to the docs directory. |
| [`section`](../../src/main.cpp#L171) | `approve` | Heading path, with ` / ` between nested headings. |
| [`file-or-directory`](../../src/main.cpp#L155) | `invalidate` | Markdown file or directory relative to the docs directory, or an absolute path inside it. |

A source file can have multiple compile commands. c-docc merges matching definitions and reports conflicts when commands yield different definitions or dependencies for the same symbol.

## Options

The [global flag parser](../../src/main.cpp#L178) handles help and version. The [documentation options parser](../../src/main.cpp#L105) handles flags for `check`, `approve`, and `invalidate`.

The [directory defaults](../../src/main.cpp#L113) are the current directory. Relative directory paths start there. Options that take values accept [`--name value` and `--name=value`](../../src/main.cpp#L132-L147).

| Option | Where it applies | Meaning |
| --- | --- | --- |
| [`--help`](../../src/main.cpp#L181), [`-h`](../../src/main.cpp#L181) | Global, or as the [sole argument](../../src/main.cpp#L106) to `check`, `approve`, or `invalidate` | Prints usage. |
| [`--version`](../../src/main.cpp#L185) | Global | Prints the executable's version. |
| [`--source-dir <path>`](../../src/main.cpp#L134) | `check`, `approve`, `invalidate` | Source directory. |
| [`--build-dir <path>`](../../src/main.cpp#L135) | `check`, `approve`, `invalidate` | Directory containing `compile_commands.json`. |
| [`--docs-dir <path>`](../../src/main.cpp#L136) | `check`, `approve`, `invalidate` | Directory containing the Markdown pages and approvals. It must be inside the source directory. |
| [`--json`](../../src/main.cpp#L128) | `check` | Writes a JSON report with pending reviews and references. |
| [`--reason <text>`](../../src/main.cpp#L137) | `approve` | Records an optional note with each approval. |
| [`--`](../../src/main.cpp#L123) | `check`, `approve`, `invalidate` | Ends option parsing. Any following arguments are positional, including paths that start with a dash. |

## Exit statuses

| Status | Meaning |
| --- | --- |
| `0` | The command completed. A diff that finds changes also returns `0`. |
| `1` | Analysis failed, c-docc reported a documentation issue, or it could not record the approval. |
| `2` | The invocation was invalid. |

Scan and diff write JSON to standard output. A successful `check` writes the count and lists each approved section by page, line, and heading. Clang diagnostics and issues reported by `check` go to standard error.

Link diagnostics include the link text and its line in the Markdown page. For code changes, the dependency path ends with the changed definition’s file and line when available. The source path is relative to `--source-dir`.

Use `check --json` to read pending reviews and backlinks from an editor or script. The report lists pending sections and whether they can be approved. The `references` array contains resolved source links. The `file_references` array contains tracked whole-file and Markdown heading links. When a code change has a known source location, the issue includes a `cause` with its file, line, and column. Target files and cause files are relative to the source directory. A report with pending reviews still returns status `1`.

## Local Markdown links

`check` reports local links whose destination file or directory is missing. A fragment in a Markdown link must match a heading anchor. c-docc also checks fragments in the current page and in a directory's `README.md`. It uses GitHub-style heading anchors, including the numeric suffixes for repeated headings.

Relative paths start at the referring page. A link that starts with `/` starts at the source directory. Fix a broken navigation link by updating its destination.

A whole-file link tracks the contents of the target file. A link to a heading in another page tracks that heading and its contents, including nested headings. Directory links without a heading and links within the current page check that the destination exists.

When tracked contents change, c-docc reports the link in the referring page if the referring section still matches its recorded text.

## Common JSON fields

| Field | Meaning |
| --- | --- |
| `tool_version` | Release version of c-docc. |
| `schema_version` | Version of the JSON format. |
| `clang_version` | Version string from the Clang library used by the analyzer. |

A location object has a path in `file`, relative to the source root, and one-based `line` and `column` values. A finding contains a `location` and a diagnostic `message`.

## Scan output

[`analyze`](../../src/Analyzer.cpp#L476) produces the symbols and findings for the given source root and build directory. The scan response contains `symbols`, `findings`, and `declarations`. Each declaration has a `location` and an array of `ids`.

| Symbol field | Meaning |
| --- | --- |
| `id` | Symbol identifier. Internal-linkage IDs include the main translation unit path. |
| `kind` | `function`, `method`, or `record`. |
| `definition` | Location of the definition. |
| `local_hash` | SHA-256 hash of the definition's token kinds and text, with a format marker. |
| `recursive_hash` | SHA-256 hash for the symbol's strongly connected component and its dependencies. |
| `component` | Sorted symbol IDs in the same strongly connected component. |
| `dependencies` | Edges with a `kind` and target symbol ID in `target`. Edge kinds are `call`, `type`, and `base`. |

## Diff output

Each entry in `changes` has an `id` and a `kind`. The entry has `before` or `after` locations wherever that symbol exists. It can include a `dependency_path`, ordered from the affected symbol to the cause. The response also contains `base_findings` and `head_findings`.

| Change kind | Meaning |
| --- | --- |
| `added` | The symbol appears only in the head snapshot. |
| `removed` | The symbol appears only in the base snapshot. |
| `direct` | The symbol's local hash, kind, or outgoing edge list changed. |
| `cycle` | The recursive hash changed and the reported cause shares the affected symbol's component. |
| `dependency` | The recursive hash changed through the dependency graph. |

## Source links and approvals

| Link property | Accepted value |
| --- | --- |
| Path | A C or C++ source or header path inside the source root, relative to the Markdown page. |
| Anchor | `#L<number>` for one line or `#L<first>-L<last>` for an inclusive range. |
| Context | A Markdown link in the page body. A link inside a code block is example text. |

A declaration without a body tracks its own tokens. Changing the definition elsewhere leaves that link current. Any line from a function definition's signature through its closing brace tracks the function and its dependencies. A link on a class definition's name line tracks the class definition.

A range combines the declarations and definitions it touches. Each dependency is tracked once, and uncovered lines are tracked as plain text. You can use a range with the same start and end line to track multiple declarations on one line.

The approval file is `.c-docc-approvals.json` under the docs root. It records the page path, heading path, section text hash, resolved symbol IDs, and tracked code fingerprints. It also stores fingerprints of linked declarations, source text, files, or Markdown headings and any reason supplied with the approval. Approval applies to the selected build configuration. A section outside any heading uses `(preamble)` as its name.

The first check records a baseline for existing sections with valid links. Later checks report dependency changes in sections whose Markdown still matches their baseline. A check records new or edited sections once their links resolve and drops records for sections you removed.

`invalidate` accepts a Markdown path relative to the docs directory or an absolute path inside it. A directory includes its nested Markdown files. The check reports invalidated sections until you edit or approve them. Use `approve` when you review the linked change and the Markdown already says what it should.

Passing only the page to `approve` updates all its linked sections. If any source link in the page fails to resolve, the command leaves the page's approvals as they were.

The `check` command exits with status `1` for an unresolved link or a section that needs review. Building the code and viewing the Markdown work independently of this command.
