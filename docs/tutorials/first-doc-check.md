# Change a retry limit and update its docs

A small project limits retries to three. In this walkthrough, the code and its guide change to five retries. A docs check shows how each edit affects the section's approval.

## The example project

These commands use Nix with flakes enabled on Linux. They create a temporary directory with one C function and one Markdown page. The page links to the declaration on line 1. The compilation database lists the source file and compiler options for this example.

```sh
dagdocdiff() {
  nix run github:ljleb/dagdocdiff/1.1.0 -- "$@"
}

example=$(mktemp -d)
cd "$example"
mkdir docs

cat > retries.c <<'EOF'
int retry_limit(void) { return 3; }
EOF

cat > docs/retries.md <<'EOF'
# Retries

[`retry_limit`](../retries.c#L1) returns 3.
EOF

cat > compile_commands.json <<EOF
[
  {
    "directory": "$PWD",
    "file": "$PWD/retries.c",
    "arguments": ["clang", "-std=c17", "-c", "retries.c"]
  }
]
EOF

dagdocdiff docs approve . . docs retries.md Retries 'The function returns 3, as documented.'
```

The last command records the example's starting approval. You should see `Recorded review for retries.md / Retries`.

```sh
dagdocdiff docs check . . docs
```

You should see `Approved documentation sections: 1`. Both `.` arguments point to the example directory because it contains the source and the compilation database. The last argument points to the Markdown directory.

## Five retries in the code

This edit changes the return value. The function declaration stays on line 1.

```sh
printf '%s\n' 'int retry_limit(void) { return 5; }' > retries.c
dagdocdiff docs check . . docs
```

The check should exit with status 1 and identify the Markdown section and changed function.

```text
retries.md:1: Retries: code changed: retry_limit
```

## Five retries in the guide

The guide can now use the same value as the code.

```sh
cat > docs/retries.md <<'EOF'
# Retries

[`retry_limit`](../retries.c#L1) returns 5.
EOF

dagdocdiff docs check . . docs
```

The check should still exit with status 1. You should see a new message, `Retries: section text changed`, alongside the code change. The text now says 5, and the approval still records the earlier version.

## A recorded review of the change

This approval records why the updated text matches the code.

```sh
dagdocdiff docs approve . . docs retries.md Retries 'The function now returns 5 and the guide states that value.'
dagdocdiff docs check . . docs
```

You should see `Approved documentation sections: 1` again. The updated review is in `docs/.dagdocdiff-approvals.json`.

[Trace a docs warning to a helper](first-diff.md) follows a change that starts in a different file from the linked function.
