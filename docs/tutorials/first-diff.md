# Trace a docs warning to a helper

A request guide says the retry limit is three. It links to the function that returns the request limit, which gets that value from a helper in another file. This example changes the helper to allow five retries and follows the reported path to the changed code.

## Two C files and a request guide

These commands use Nix with flakes enabled on Linux. They create a temporary project and record its starting approval. The guide links to line 2 of `request_limit.c`, where `request_limit` is defined.

```sh
dagdocdiff() {
  nix run github:ljleb/dagdocdiff/1.1.0 -- "$@"
}

example=$(mktemp -d)
cd "$example"
mkdir docs

cat > request_limit.c <<'EOF'
int retry_limit(void);
int request_limit(void) { return retry_limit(); }
EOF

cat > retry_limit.c <<'EOF'
int retry_limit(void) { return 3; }
EOF

cat > docs/requests.md <<'EOF'
# Requests

[`request_limit`](../request_limit.c#L2) returns 3.
EOF

cat > compile_commands.json <<EOF
[
  {
    "directory": "$PWD",
    "file": "$PWD/request_limit.c",
    "arguments": ["clang", "-std=c17", "-c", "request_limit.c"]
  },
  {
    "directory": "$PWD",
    "file": "$PWD/retry_limit.c",
    "arguments": ["clang", "-std=c17", "-c", "retry_limit.c"]
  }
]
EOF

dagdocdiff docs approve . . docs requests.md Requests 'The request limit comes from retry_limit, which returns 3.'
```

You should see `Recorded review for requests.md / Requests`. The check shows that the guide is approved before the edit.

```sh
dagdocdiff docs check . . docs
```

You should see `Approved documentation sections: 1`.

## Five retries in the helper

The edit goes into `retry_limit.c`. The file that the guide links to stays the same.

```sh
printf '%s\n' 'int retry_limit(void) { return 5; }' > retry_limit.c
dagdocdiff docs check . . docs
```

The check should exit with status 1 and show the path from the linked function to the changed helper.

```text
requests.md:1: Requests: code changed: request_limit -> retry_limit
```

These commands show the two ends of that path.

```sh
cat request_limit.c
cat retry_limit.c
```

In the first file, `request_limit` should still return the result of calling `retry_limit`. In the second file, `retry_limit` should now return 5. The guide describes the result of the call, so the helper's edit affects its claim.

## Five retries in the guide

The link still points to `request_limit`. This edit changes the value the guide describes and records why it matches the code.

```sh
cat > docs/requests.md <<'EOF'
# Requests

[`request_limit`](../request_limit.c#L2) returns 5.
EOF

dagdocdiff docs approve . . docs requests.md Requests 'request_limit calls retry_limit, which now returns 5.'
dagdocdiff docs check . . docs
```

You should see `Approved documentation sections: 1` again.
