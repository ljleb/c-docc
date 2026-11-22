# Dagdocdiff implementation roadmap

## Working in the 1.2.0 checkout

- `scan` indexes C and C++ symbols, declaration lines, and tracked dependencies for one compilation database.
- `diff` compares two snapshots and reports direct changes, changes within a recursion cycle, and changes that reach callers or other dependents.
- `check` reads Markdown directly. It flags declaration links that fail to resolve or point to different code. It also flags sections whose text or linked code changed since approval.
- `approve` records a review of one section, including the symbol IDs resolved from each link and the recursive hashes. A reason is optional.

Projects can run `check` in CI independently of the source build. Readers can open and publish the docs as ordinary Markdown. [Update linked docs with a code change](docs/how-to/review-linked-docs.md)

### What the analyzer tracks

Clang LibTooling reads `compile_commands.json` for each snapshot. It collects project definitions, resolved direct calls, referenced record types, and base classes. The scan reports gaps it detects, such as indirect calls or missing definitions. Each snapshot uses one build configuration. [Clang LibTooling](https://clang.llvm.org/docs/LibTooling.html)

Symbol IDs use qualified names and canonical signatures where needed. Internal-linkage IDs include the repository-relative path of the main translation unit. A `static` helper in a header can therefore have multiple IDs while those IDs share a definition location. [C++ linkage rules](https://eel.is/c++draft/basic.link)

Local hashes summarize token kinds and text after macro expansion. LLVM groups dependency cycles into strongly connected components. Each component hash includes its members and the hashes of the components it depends on. The diff uses local hashes and dependency edges to explain how changes spread. [How Dagdocdiff tracks code changes](docs/explanation/symbol-graphs.md)

### Which documentation links work

A relative Markdown link with a `#L<number>` anchor points to the line that spells a declaration name. A declaration in a header and its definition in a source file can resolve to the same symbol. A link to a header declaration with separate translation-unit instances tracks all those instances. If a line contains multiple declarations, use a line with one supported declaration. [Source links and approvals](docs/reference/cli.md#source-links-and-approvals)

The tests cover change propagation across files, comment-only edits, source links that retarget to a different overload, and approvals that become stale after a dependency changes.

## Remaining work

- Extend dependency coverage to template instantiations, virtual dispatch targets, and function pointer targets.
- Decide how links to function bodies, declarations inside class bodies, macro-generated names, and line ranges resolve to symbols.
- Support independently versioned documentation packages that live outside the source checkout. Derive reverse links for editors so readers can discover which sections cite a symbol.
- Add a Clang plugin if projects need to collect graphs during compilation. [Clang tooling interfaces](https://clang.llvm.org/docs/Tooling.html)
