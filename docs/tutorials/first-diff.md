# Trace a docs warning to a helper

A parcel tracking app asks a carrier for delivery updates. If the carrier takes too long to respond, the app tries again. The guide says it retries up to three times.

The guide links to `request_limit`, which gets the limit from a helper in another file. You will raise the limit to five in the helper and follow Dagdocdiff's report from the linked function to the edit.

## Run Dagdocdiff

This walkthrough uses the published Dagdocdiff 1.1.0 package on Linux with Nix flakes enabled.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- --version
```

You should see `dagdocdiff 1.1.0`. Nix runs the package named before `--` and passes everything after it to Dagdocdiff.

## Two C files and a request guide

Create an empty folder for this example and open a terminal there. Run the rest of the commands from this folder.

Create `retry_limit.c` with this one line.

```c
int retry_limit(void) { return 3; }
```

Next to it, create `request_limit.c` with these two lines. The first declares the helper and the second defines the function that calls it.

```c
int retry_limit(void);
int request_limit(void) { return retry_limit(); }
```

Create a `docs` folder in the example folder. In it, create `requests.md` with these contents.

```markdown
# Requests

If the carrier takes too long to respond, the app [retries the tracking request](../request_limit.c#L2) up to 3 times.
```

The link opens `request_limit.c` at line 2. Read that line and follow its call to `retry_limit` to see where the guide's value comes from.

Create `compile_commands.json` next to the C files. It lists both files and their compiler options so Dagdocdiff can follow the call between them. Replace both instances of `/absolute/path/to/request-example` with the full path to your example folder. If your terminal is open there, `pwd` prints that path.

```json
[
  {
    "directory": "/absolute/path/to/request-example",
    "file": "request_limit.c",
    "arguments": ["clang", "-std=c17", "-c", "request_limit.c"]
  },
  {
    "directory": "/absolute/path/to/request-example",
    "file": "retry_limit.c",
    "arguments": ["clang", "-std=c17", "-c", "retry_limit.c"]
  }
]
```

## An approved starting point

The guide says the app retries the request up to 3 times, and the helper returns 3. Record that reason for approving the `Requests` section in `requests.md`. Both `.` arguments point to the current folder because it contains the source and the compilation database. The `docs` argument names the Markdown folder, and `requests.md` is relative to it.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs approve . . docs requests.md Requests 'The guide says 3 retries, and request_limit gets 3 from retry_limit.'
```

You should see `Recorded review for requests.md / Requests`. Run Dagdocdiff against the guide to see the starting result.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs check . . docs
```

You should see `Approved documentation sections: 1`.

## Five retries in the helper

In `retry_limit.c`, change `return 3;` to `return 5;`. Save that file and run Dagdocdiff again.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs check . . docs
```

The command should exit with status `1`. Look for this line in the report.

```text
requests.md:1: Requests: code changed: request_limit -> retry_limit
```

The arrow shows that `request_limit` calls `retry_limit`. Open `request_limit.c` and look for that call on line 2. In `retry_limit.c`, you should see `return 5;`. The guide's link still opens the caller, and the new value comes from the helper.

## Five retries in the guide

In `docs/requests.md`, change “up to 3 times” to “up to 5 times” and keep the link to `request_limit`. Save the guide and record why the new value applies.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs approve . . docs requests.md Requests 'The guide says 5 retries, and request_limit gets 5 from retry_limit.'
```

You should see `Recorded review for requests.md / Requests` again. Run Dagdocdiff once more against the updated guide.

```sh
nix run github:ljleb/dagdocdiff/1.1.0 -- docs check . . docs
```

You should see `Approved documentation sections: 1` again.
