# Change a retry limit and update its docs

A photo backup app tries again when Wi-Fi drops during an upload. Its troubleshooting guide says the app retries up to three times before giving up. You will raise that limit to five and run Dagdocdiff as you update the code and the guide.

## A function and its guide

The example has a C file, a Markdown guide, and a compilation database in one folder. The [photo backup starter](../../examples/photo-backup/) has all three files. Copy that folder or create the files below.

Create `retries.c` with this one line.

```c
int retry_limit(void) { return 3; }
```

Create `retries.md` next to `retries.c`.

```markdown
# Retries

If Wi-Fi drops during an upload, the app [retries the upload](retries.c#L1) up to 3 times before giving up.
```

The relative link points to the function on line 1 of `retries.c` in the same folder.

Create `compile_commands.json` in the same folder. It gives Dagdocdiff the source file and the compiler options for reading it as C17. The `directory` field tells Clang where the compiler would run. It needs the full path to your example folder.

```json
[
  {
    "directory": "REPLACE_WITH_ABSOLUTE_PATH",
    "file": "retries.c",
    "arguments": ["clang", "-std=c17", "-c", "retries.c"]
  }
]
```

Open a terminal in your example folder. The command `pwd` prints the folder's full path. Save that path in the `directory` field in place of `REPLACE_WITH_ABSOLUTE_PATH`. Run the remaining commands from this folder.

## Run Dagdocdiff

This walkthrough uses the published Dagdocdiff 1.1.0 package on Linux with Nix flakes enabled.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- --version
```

You should see `dagdocdiff 1.1.0`. Nix runs the package named before `--` and passes everything after it to Dagdocdiff.

## The first approval

Run Dagdocdiff against the guide. The three `.` arguments point to the source, compilation database, and Markdown folders. In this example, all three are the current folder.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs check . . .
```

You should see `retries.md:1: Retries: approval required`. The link resolves, and Dagdocdiff has no approval that records which version of the code the guide describes.

The guide says the app retries up to 3 times, and `retry_limit` returns 3. Record that reason for approving the `Retries` section in `retries.md`. The page path is relative to the Markdown folder, and the section name comes from the heading.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs approve . . . retries.md Retries 'The guide matches the upload retry limit of 3.'
```

You should see `Recorded review for retries.md / Retries`. Run `docs check` again.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs check . . .
```

You should see `Approved documentation sections: 1`.

## Five retries in the code

In `retries.c`, change `return 3;` to `return 5;`. Keep the function on line 1 so the Markdown link still points to it. Save the file and run Dagdocdiff again.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs check . . .
```

The command should exit with status `1`. You should see the Markdown section and the changed function in the report.

```text
retries.md:1: Retries: code changed: retry_limit
```

Open `retries.md`. It still tells people to expect up to 3 retries.

## Five retries in the guide

Change “up to 3 times” to “up to 5 times” in `retries.md`. Save the file and run Dagdocdiff again.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs check . . .
```

The command should still exit with status `1`. You should see a new message, `Retries: section text changed`, alongside the code change. The sentence now matches the function, and the approval still records the earlier version of both.

## A recorded review of the change

Record why the revised sentence matches the function.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs approve . . . retries.md Retries 'The guide matches the new upload retry limit of 5.'
```

You should see `Recorded review for retries.md / Retries` again. The updated record is in `.dagdocdiff-approvals.json`. Run Dagdocdiff once more to see what it reports for the updated guide.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs check . . .
```

You should see `Approved documentation sections: 1` again.

[Trace a docs warning to a helper](first-diff.md) follows a change that starts in a different file from the linked function.
