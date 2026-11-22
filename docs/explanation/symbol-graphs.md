# How Dagdocdiff tracks code changes

A change to a helper can affect a caller in another file. Dagdocdiff follows that relationship from symbol to symbol. This lets a diff show where the change began and how it reached the code that uses it.

## Identity follows the symbol

Dagdocdiff pairs definitions across snapshots by symbol ID. An externally linked C++ function uses a qualified name and canonical parameter types. That lets a function keep its ID when its definition moves or when a parameter uses a different alias for the same type. File and line numbers describe where to find it.

Internal linkage changes what the name identifies. If two source files include a header with a `static` helper, each gets its own helper. Each ID includes the main translation unit path. A translation unit is a source file together with the headers processed with it.

## Recursion changes how hashes propagate

A local hash summarizes a definition's macro-expanded tokens. A recursive hash also incorporates tracked dependencies. For example, `read()` calls `parse()`, and `parse()` calls `tokenize()`. If `tokenize()` changes, the recursive hash carries that change back to `read()`.

When `parse()` and `tokenize()` call each other, they form a cycle. Each hash would need the other to be ready first. [`hashComponents`](../../src/Graph.cpp#L90) groups mutually reachable symbols into a strongly connected component, or SCC. The group gets one hash from its members, their local hashes, and its dependency edges. The groups form a graph without cycles, so Dagdocdiff can hash dependencies before the groups that use them.

The shared hash tells [`compare`](../../src/Graph.cpp#L146) that the cycle changed. The local hashes and outgoing edges help it find where the change began. That is how one edit can appear as a direct change to one symbol, a cycle change to its partner, and a dependency change to the caller.

## Why coverage matters

Each graph describes the selected build configuration and the dependency edges Dagdocdiff can extract. For example, a virtual call can resolve to a method declaration during analysis and invoke a different override at runtime. The findings report gaps that the analyzer detects. Equal recursive hashes apply to the tracked inputs for that configuration.

## Why documentation tracks identity as well as hashes

A Markdown link opens a file at a line number. If two overload declarations trade places, the same link can open a different overload while both symbol hashes stay the same. The approval stores the symbol IDs that each source link resolved to, along with their recursive hashes. Dagdocdiff can then distinguish a link that points to different code from a change in the code it originally referenced.
