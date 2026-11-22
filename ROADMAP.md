# Dagdocdiff C and C++ Implementation Roadmap

Dagdocdiff compares two C or C++ source snapshots. It reports changes to functions, methods, and records, including changes that reach them through dependencies. Build a standalone C++ tool first. Then use its symbol graph and hashes to verify documentation that depends on the source package.

## Extract a graph for each revision

Run Clang LibTooling with each snapshot’s `compile_commands.json`. Start with one build configuration per snapshot. An AST visitor collects definitions, resolved direct calls, referenced types, and base classes. Include project headers reached through compilation. Record diagnostics and unresolved dependencies so a partial graph is visible in the results. [Clang LibTooling](https://clang.llvm.org/docs/LibTooling.html) · [AST visitor tutorial](https://clang.llvm.org/docs/RAVFrontendAction.html)

Define project-owned symbol IDs from qualified names and canonical signatures. Include parameter types and method qualifiers where they distinguish C++ overloads. Scope internal-linkage symbols by the repository-relative path of the main translation unit. A `static` helper in `helpers.h` gets distinct IDs under `reader.cpp` and `writer.cpp`, and both IDs point to `helpers.h` as the definition location. Externally linked symbols use path-free IDs within a build context. [C++ linkage rules](https://eel.is/c++draft/basic.link) · [C++ parameter type rules](https://eel.is/c++draft/dcl.fct)

Use normalized, macro-expanded tokens as the starting point for local definition hashes. This lets a macro edit affect a function whose written body stays the same. Version the symbol and hash formats and record the Clang version and build configuration. [Clang token API](https://clang.llvm.org/doxygen/Tokens_8h_source.html)

## Merge graphs and hash dependencies

Merge translation-unit records by symbol ID and report conflicting payloads in the same build context. Draw each dependency edge from the symbol that uses it to the symbol it depends on. Collapse cycles into strongly connected components (SCCs). LLVM’s `scc_iterator` visits dependencies before their dependents. Give the iterator a synthetic entry node that reaches every symbol so disconnected groups are included. [LLVM SCC iterator](https://llvm.org/doxygen/SCCIterator_8h.html)

Hash each SCC from its sorted member IDs, local hashes, and dependency edges. Include the computed hash of each outgoing dependency SCC. Keep local hashes and edges available so the comparison can explain which definition changed and how that change propagated.

## Compare snapshots and verify the result

Compare snapshots by symbol ID. Report added and removed symbols, direct changes, and changes inherited through a dependency or a shared SCC. The initial command accepts two build directories with compilation databases.

Use an end-to-end fixture with two mutually recursive functions in different source files and a caller in a third. Change only a header macro used by one of the two functions. The report should identify that function as directly changed, its partner as affected through the shared SCC, and the caller as affected through a dependency. A comment-only edit should leave the hashes unchanged.

## Derive dependencies from Markdown links

The source package publishes a symbol index, which lists the symbols that documentation can reference. Documentation authors use ordinary Markdown links whose destinations identify code symbols. Put each link in the sentence or example that discusses the symbol. Keep page headers and footers free of dependency lists.

For example, an author could write the following sentence. The `code:` prefix illustrates a possible destination format.

```markdown
Create a [`net::Client`](code:net::Client) before opening a connection.
```

The link directs readers to the code. The verifier derives an explicit dependency from that same link. The source graph supplies the implicit dependencies reachable from the linked symbol.

An API reference, tutorial, and architecture guide can each have its own versions and approvals. Build reverse navigation from the documentation links so an editor can show which sections reference a symbol. The source package must build independently of documentation. A merge policy can require documentation verification in CI.

## Record and verify documentation approvals

An approval binds a reviewed document or section revision to its resolved symbol IDs and their recursive hashes for a source build configuration. The verifier compares the current document and source hashes with that record. It reports missing symbols as unresolved links. An edited section or a changed dependency requires review. The record shows which implementation state a reviewer assessed. The reviewer judges whether the documentation is accurate.

After the standalone comparison works, resolve links from one documentation package. Adapt docdiff’s `check`, `report`, and `explain` commands to use the approval records. Extend the end-to-end fixture with a sentence that links to the caller. After approval, the header macro change must trigger review and show the dependency path from the linked caller to the changed function. [docdiff review workflow](https://github.com/StevenBock/docdiff#docdiff-ack)

## Expand dependency coverage

Add template instantiations, virtual dispatch targets, and function pointer targets as projects need them. If a project needs to collect graphs during a build, run the same extractor as a Clang plugin. [Clang tooling interfaces](https://clang.llvm.org/docs/Tooling.html)
