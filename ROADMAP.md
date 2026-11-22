# Dagdocdiff implementation roadmap

- Extend dependency coverage to template instantiations, virtual dispatch targets, and function pointer targets.
- Decide how links to non-method declarations inside class bodies and macro-generated names resolve to symbols.
- Support independently versioned documentation packages that live outside the source checkout. Derive reverse links for editors so readers can discover which sections cite a symbol.
- Add a Clang plugin if projects need to collect graphs during compilation. [Clang tooling interfaces](https://clang.llvm.org/docs/Tooling.html)
