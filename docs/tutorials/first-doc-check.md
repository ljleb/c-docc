# Change a retry limit and update its docs

A photo backup app tries again when Wi-Fi drops during an upload. Its troubleshooting guide says the app retries up to three times before giving up. You will raise that limit to five and run Dagdocdiff as you update the code and the guide.

## Dagdocdiff from this checkout

This walkthrough uses a build from the Dagdocdiff repository. On Linux with Nix flakes enabled, run this from the repository root.

```sh
nix shell path:.
```

Nix builds Dagdocdiff and makes the command available in a new shell. Use that shell for the rest of the walkthrough.

## A function and its guide

The example has a C file, a Markdown guide, and a compilation database in one folder. The [photo backup starter](../../examples/photo-backup/) has all three files. Copy that folder or create the files below.

Create `retries.c` with this one line.

```c
int retry_limit(void)
{
    return 3;
}
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

Switch to your example folder in the shell that Nix opened. The command `pwd` prints the folder's full path. Save that path in the `directory` field in place of `REPLACE_WITH_ABSOLUTE_PATH`. Run the remaining commands from this folder.

## The first approval

Run Dagdocdiff against the guide. By default, it looks for the source, compilation database, and Markdown in your current folder.

```sh
dagdocdiff check
```

You should see `retries.md:1: Retries: approval required`. The link works. Dagdocdiff is asking you to choose a starting point for future comparisons.

Suppose you first run Dagdocdiff after someone raises the limit to 5 while the guide still says 3. If Dagdocdiff accepts those versions automatically, later runs can pass while the guide still gives the wrong number. The first approval gives you a chance to catch that mismatch.

In this example, the guide says the app retries up to 3 times, and `retry_limit` returns 3. Approve `retries.md` to record the guide and code you just compared. Future runs use that record to flag changes.

```sh
dagdocdiff approve retries.md
```

You should see `Recorded review for retries.md`. Run `check` again.

```sh
dagdocdiff check
```

You should see the number of approved sections followed by the section you just approved.

```text
Approved documentation sections: 1
  retries.md:1: Retries
```

## Five retries in the code

In `retries.c`, change `return 3;` to `return 5;`. Keep the function on line 1 so the Markdown link still points to it. Save the file and run Dagdocdiff again.

```sh
dagdocdiff check
```

The command should exit with status `1`. You should see the Markdown section and the changed function in the report.

```text
retries.md:1: Retries: code changed: retry_limit
```

Open `retries.md`. It still tells people to expect up to 3 retries.

## Five retries in the guide

Change “up to 3 times” to “up to 5 times” in `retries.md`. Save the file and run Dagdocdiff again.

```sh
dagdocdiff check
```

The command should still exit with status `1`. You should see a new message, `Retries: section text changed`, alongside the code change. The sentence now matches the function, and the approval still records the earlier version of both.

## A recorded review of the change

The guide now describes the new limit of 5. Approve the revised file.

```sh
dagdocdiff approve retries.md
```

You should see `Recorded review for retries.md` again. The updated record is in `.dagdocdiff-approvals.json`. Run Dagdocdiff once more to see what it reports for the updated guide.

```sh
dagdocdiff check
```

You should see `retries.md:1: Retries` under `Approved documentation sections: 1` again.

[Trace a docs warning to a helper](first-diff.md) follows a change that starts in a different file from the linked function.
