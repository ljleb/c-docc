#include "Analyzer.h"

#include "clang/Basic/Version.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <utility>

namespace {

using namespace dagdocdiff;
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
    return json::Object{{"symbols", std::move(symbols)}, {"findings", findings(snapshot)}};
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
    result["clang_version"] = clang::getClangFullVersion();
    llvm::outs() << llvm::formatv("{0:2}\n", json::Value(std::move(result)));
}

int fail(llvm::Error error) {
    llvm::logAllUnhandledErrors(std::move(error), llvm::errs(), "dagdocdiff: ");
    return 1;
}

void usage(llvm::raw_ostream &out) {
    out << "Usage:\n"
        << "  dagdocdiff scan <source-root> <build-dir>\n"
        << "  dagdocdiff diff <base-root> <base-build-dir> <head-root> <head-build-dir>\n";
}

} // namespace

int main(int argc, char **argv) {
    llvm::InitLLVM init(argc, argv);
    const llvm::StringRef command = argc > 1 ? argv[1] : "";
    if (command == "--help" || command == "-h") {
        usage(llvm::outs());
        return 0;
    }
    if (command == "scan" && argc == 4) {
        auto snapshot = dagdocdiff::analyze(argv[2], argv[3]);
        if (!snapshot)
            return fail(snapshot.takeError());
        print(scanJson(*snapshot));
        return 0;
    }
    if (command == "diff" && argc == 6) {
        auto base = dagdocdiff::analyze(argv[2], argv[3]);
        if (!base)
            return fail(base.takeError());
        auto head = dagdocdiff::analyze(argv[4], argv[5]);
        if (!head)
            return fail(head.takeError());
        print(diffJson(*base, *head));
        return 0;
    }
    usage(llvm::errs());
    return 2;
}
