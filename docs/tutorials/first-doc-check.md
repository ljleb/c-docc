# Change a retry limit and update its docs

A photo backup app tries again when Wi-Fi drops during an upload. Its troubleshooting guide says the app retries up to three times before giving up. You will raise that limit to five and run c-docc as you update the code and the guide.

## c-docc from this checkout

This walkthrough uses a build from the c-docc repository. On Linux with Nix flakes enabled, run this from the repository root.

```sh
nix shell path:.
```

Nix builds c-docc and makes the command available in a new shell. Use that shell for the rest of the walkthrough.

## A function and its guide

The example has a C file, a Markdown guide, and a compilation database in one folder. The [photo backup starter](../../examples/photo-backup/) has all three files. Copy that folder or create the files below.

Create [`retries.c`](../../examples/photo-backup/retries.c#L1) with the contents below.

```c
int retry_limit(void)
{
    return 3;
}
```

Create [`retries.md`](../../examples/photo-backup/retries.md) next to `retries.c`.

```markdown
# Retries

If Wi-Fi drops during an upload, the app [retries the upload](retries.c#L1) up to 3 times before giving up.
```

The relative link points to the function on line 1 of `retries.c` in the same folder.

Create [`compile_commands.json`](../../examples/photo-backup/compile_commands.json) in the same folder. It gives c-docc the source file and the compiler options for reading it as C17. The `directory` field tells Clang where the compiler would run. It needs the full path to your example folder.

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

## The first check

Run c-docc from the example folder.

```sh
c-docc check
```

The first check records the current guide and its linked code in `.c-docc-approvals.json`. You should see `retries.md:1: Retries` under `Approved documentation sections: 1`.

## Five retries in the code

In `retries.c`, change `return 3;` to `return 5;`. Keep the function on line 1 so the Markdown link still points to it. Save the file and run c-docc again.

```sh
c-docc check
```

The command should exit with status `1`. You should see the text of the affected Markdown link and the location of the changed function.

```text
retries.md:3: Retries: "retries the upload": code changed: retry_limit (retries.c:1)
```

Open `retries.md`. It still tells people to expect up to 3 retries.

## Five retries in the guide

Change “up to 3 times” to “up to 5 times” in `retries.md`. Save the file and run c-docc again.

```sh
c-docc check
```

The check should pass. c-docc treats the new sentence as your response to the warning and records it with the current code. If the limit changes again while this sentence stays the same, the check can flag it again.

## Request a review

You can ask c-docc to flag the guide even when its code and Markdown are stable.

```sh
c-docc invalidate retries.md
c-docc check
```

You should see `approval manually invalidated`. The guide still matches the code, so record that review without changing the guide.

```sh
c-docc approve retries.md
c-docc check
```

You should see `retries.md:1: Retries` under `Approved documentation sections: 1` again.

[Trace a change in a delivery estimate](first-diff.md) follows a change in depot sorting time through a caller in another file.
