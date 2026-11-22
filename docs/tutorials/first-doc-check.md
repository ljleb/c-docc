# Change a retry limit and update its docs

A small project limits retries to three. In this walkthrough, you change the code and its guide to allow five retries. After each edit, you run Dagdocdiff to see what it reports.

## Run Dagdocdiff

This walkthrough uses the published Dagdocdiff 1.1.0 package on Linux with Nix flakes enabled.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- --version
```

You should see `dagdocdiff 1.1.0`. Nix runs the package named before `--` and passes everything after it to Dagdocdiff.

## A function and its guide

Create an empty folder for the example and open a terminal there. Run the rest of the commands from this folder.

Create `retries.c` with this one line.

```c
int retry_limit(void) { return 3; }
```

Create a `docs` folder next to `retries.c`. In it, create `retries.md` with these contents.

```markdown
# Retries

[`retry_limit`](../retries.c#L1) returns 3.
```

The link goes up from `docs` and points to the function on line 1 of `retries.c`.

Next to `retries.c`, create `compile_commands.json`. This file tells Dagdocdiff which source file to read and which compiler options to use. Replace `/absolute/path/to/retry-example` with the full path to your example folder. If your terminal is open there, `pwd` prints that path.

```json
[
  {
    "directory": "/absolute/path/to/retry-example",
    "file": "retries.c",
    "arguments": ["clang", "-std=c17", "-c", "retries.c"]
  }
]
```

## The first approval

Run Dagdocdiff against the guide. The first `.` points to the source folder and the second points to the folder with `compile_commands.json`. Both are the current folder in this example. The final argument points to the Markdown folder.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs check . . docs
```

You should see `retries.md:1: Retries: approval required`. Dagdocdiff found the section and its source link, and it needs a record that someone compared the text with the code.

The function returns 3, as the guide says. Record that reason for approving the `Retries` section in `retries.md`. The page path is relative to `docs`, and the section name comes from the heading.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs approve . . docs retries.md Retries 'The function returns 3, as documented.'
```

You should see `Recorded review for retries.md / Retries`. Run `docs check` again.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs check . . docs
```

You should see `Approved documentation sections: 1`.

## Five retries in the code

In `retries.c`, change `return 3;` to `return 5;`. Keep the function on line 1 so the Markdown link still points to it. Save the file and run Dagdocdiff again.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs check . . docs
```

The command should exit with status `1`. You should see the Markdown section and the changed function in the report.

```text
retries.md:1: Retries: code changed: retry_limit
```

Open `docs/retries.md`. The sentence still says 3.

## Five retries in the guide

Change “returns 3” to “returns 5” in `docs/retries.md`. Save the file and run Dagdocdiff again.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs check . . docs
```

The command should still exit with status `1`. You should see a new message, `Retries: section text changed`, alongside the code change. The sentence now matches the function, and the approval still records the earlier version of both.

## A recorded review of the change

Record why the revised sentence matches the function.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs approve . . docs retries.md Retries 'The function now returns 5 and the guide states that value.'
```

You should see `Recorded review for retries.md / Retries` again. The updated record is in `docs/.dagdocdiff-approvals.json`. Run Dagdocdiff once more to see what it reports for the updated guide.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs check . . docs
```

You should see `Approved documentation sections: 1` again.

[Trace a docs warning to a helper](first-diff.md) follows a change that starts in a different file from the linked function.
