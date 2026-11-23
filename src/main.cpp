#include "Analyzer.h"
#include "Docs.h"

#include "clang/Basic/Version.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <utility>

namespace {

using namespace c_docc;
namespace json = llvm::json;

json::Object location(const Location &value) {
    return json::Object{{"file", value.file}, {"line", value.line}, {"column", value.column}};
}

json::Array findings(const Snapshot &snapshot) {
    json::Array result;
    for (const auto &finding : snapshot.findings)
        result.push_back(json::Object{{"location", location(finding.location)}, {"message", finding.message}});
    return result;
}

json::Object scanJson(const Snapshot &snapshot) {
    json::Array symbols;
    for (const auto &[id, symbol] : snapshot.symbols) {
        json::Array dependencies;
        for (const auto &edge : symbol.dependencies)
            dependencies.push_back(json::Object{{"kind", edge.kind}, {"target", edge.target}});
        symbols.push_back(json::Object{
            {"id", id}, {"kind", symbol.kind}, {"definition", location(symbol.definition)},
            {"local_hash", symbol.localHash}, {"recursive_hash", symbol.component->hash},
            {"component", json::Array(symbol.component->members)},
            {"dependencies", std::move(dependencies)}
        });
    }
    json::Array declarations;
    for (const auto &[site, ids] : snapshot.declarations) {
        declarations.push_back(json::Object{
            {"location", location(site)}, {"ids", json::Array(std::vector<std::string>(ids.begin(), ids.end()))}
        });
    }
    return json::Object{{"symbols", std::move(symbols)}, {"findings", findings(snapshot)},
                        {"declarations", std::move(declarations)}};
}

json::Object diffJson(const Snapshot &base, const Snapshot &head) {
    json::Array changes;
    for (const auto &change : compare(base, head)) {
        json::Object item{{"id", change.id}, {"kind", change.kind}};
        const auto before = base.symbols.find(change.id);
        const auto after = head.symbols.find(change.id);
        if (before != base.symbols.end())
            item["before"] = location(before->second.definition);
        if (after != head.symbols.end())
            item["after"] = location(after->second.definition);
        if (!change.path.empty())
            item["dependency_path"] = json::Array(change.path);
        changes.push_back(std::move(item));
    }
    return json::Object{{"changes", std::move(changes)}, {"base_findings", findings(base)},
            {"head_findings", findings(head)}};
}

void print(json::Object result) {
    result["schema_version"] = 1;
    result["tool_version"] = C_DOCC_VERSION;
    result["clang_version"] = clang::getClangFullVersion();
    llvm::outs() << llvm::formatv("{0:2}\n", json::Value(std::move(result)));
}

int fail(llvm::Error error) {
    llvm::logAllUnhandledErrors(std::move(error), llvm::errs(), "c-docc: ");
    return 1;
}

void usage(llvm::raw_ostream &out) {
    out << "Usage:\n"
        << "  c-docc --version\n"
        << "  c-docc scan <source-root> <build-dir>\n"
        << "  c-docc diff <base-root> <base-build-dir> <head-root> <head-build-dir>\n"
        << "  c-docc check [options] [--json]\n"
        << "  c-docc approve [options] [--reason <text>]\n"
        << "  c-docc approve <page> [<section>] [options] [--reason <text>]\n"
        << "  c-docc invalidate <file-or-directory> [options]\n"
        << "\nOptions for check, approve, and invalidate (each path defaults to the current directory):\n"
        << "  --source-dir <path>  Source directory\n"
        << "  --build-dir <path>    Directory containing compile_commands.json\n"
        << "  --docs-dir <path>    Markdown directory\n";
}

int usageError(const llvm::Twine &message) {
    llvm::errs() << "c-docc: " << message << '\n';
    usage(llvm::errs());
    return 2;
}

int docsCommand(llvm::StringRef command, llvm::ArrayRef<char *> args) {
    if (args.size() == 1 &&
        (llvm::StringRef(args[0]) == "--help" || llvm::StringRef(args[0]) == "-h")) {
        usage(llvm::outs());
        return 0;
    }

    const bool approve = command == "approve";
    const bool invalidate = command == "invalidate";
    llvm::StringRef source = ".", build = ".", docs = ".";
    llvm::StringRef reason;
    bool json = false;
    llvm::SmallVector<llvm::StringRef, 2> positional;
    bool options = true;
    for (size_t i = 0; i < args.size(); ++i) {
        const llvm::StringRef arg = args[i];
        if (!options || !arg.starts_with("-")) {
            positional.push_back(arg);
            continue;
        }
        if (arg == "--") {
            options = false;
            continue;
        }

        if (command == "check" && arg == "--json") {
            json = true;
            continue;
        }
        auto [name, value] = arg.split('=');
        auto *destination = llvm::StringSwitch<llvm::StringRef *>(name)
            .Case("--source-dir", &source)
            .Case("--build-dir", &build)
            .Case("--docs-dir", &docs)
            .Case("--reason", approve ? &reason : nullptr)
            .Default(nullptr);
        if (!destination)
            return usageError("unknown option " + name);
        if (name == arg) {
            if (i + 1 == args.size() || llvm::StringRef(args[i + 1]).starts_with("-"))
                return usageError("missing value for " + name);
            value = args[++i];
        }
        if (value.empty() && destination != &reason)
            return usageError("missing value for " + name);
        *destination = value;
    }
    if (approve) {
        if (positional.size() > 2)
            return usageError("approve accepts a page and an optional section");
        if (positional.empty()) {
            if (auto result = reviewDocs(source, build, docs, reason))
                return fail(std::move(result));
            return 0;
        }

        const auto page = positional[0];
        if (positional.size() == 1) {
            if (auto result = approvePage(source, build, docs, page, reason))
                return fail(std::move(result));
            return 0;
        }

        const auto section = positional[1];
        if (auto result = approveDocs(source, build, docs, page, section, reason))
            return fail(std::move(result));
        return 0;
    }

    if (invalidate) {
        if (positional.size() != 1)
            return usageError("invalidate requires a Markdown file or directory");
        const auto target = positional[0];
        if (auto result = invalidateDocs(source, build, docs, target))
            return fail(std::move(result));
        return 0;
    }

    if (!positional.empty())
        return usageError("check takes no positional arguments");
    if (auto result = checkDocs(source, build, docs, json))
        return fail(std::move(result));
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    llvm::InitLLVM init(argc, argv);
    const llvm::StringRef command = argc > 1 ? argv[1] : "";
    if (command == "--help" ||
        command == "-h") {
        usage(llvm::outs());
        return 0;
    }
    if (command == "--version" && argc == 2) {
        llvm::outs() << "c-docc " C_DOCC_VERSION "\n";
        return 0;
    }
    if (command == "scan" && argc == 4) {
        const llvm::StringRef sourceRoot = argv[2];
        const llvm::StringRef buildDir = argv[3];
        auto snapshot = c_docc::analyze(sourceRoot, buildDir);
        if (!snapshot)
            return fail(snapshot.takeError());
        print(scanJson(*snapshot));
        return 0;
    }
    if (command == "diff" && argc == 6) {
        const llvm::StringRef baseRoot = argv[2];
        const llvm::StringRef baseBuildDir = argv[3];
        auto base = c_docc::analyze(baseRoot, baseBuildDir);
        if (!base)
            return fail(base.takeError());
        const llvm::StringRef headRoot = argv[4];
        const llvm::StringRef headBuildDir = argv[5];
        auto head = c_docc::analyze(headRoot, headBuildDir);
        if (!head)
            return fail(head.takeError());
        print(diffJson(*base, *head));
        return 0;
    }
    if (command == "check" || command == "approve" || command == "invalidate")
        return docsCommand(command, llvm::ArrayRef(argv + 2, argc - 2));
    usage(llvm::errs());
    return 2;
}
