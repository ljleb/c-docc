#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SHA256.h"

#include <compare>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace dagdocdiff {

class Fingerprint {
public:
    explicit Fingerprint(llvm::StringRef domain);
    void add(llvm::StringRef value);
    std::string finish();

private:
    llvm::SHA256 hash_;
};

struct Location {
    std::string file;
    unsigned line = 0;
    unsigned column = 0;
    auto operator<=>(const Location &) const = default;
};

struct Dependency {
    std::string kind;
    std::string target;
    auto operator<=>(const Dependency &) const = default;
};

struct Component {
    std::string hash;
    std::vector<std::string> members;
};

struct Symbol {
    std::string kind;
    Location definition;
    std::string localHash;
    std::set<Dependency> dependencies;
    std::shared_ptr<const Component> component;
};

struct Finding {
    Location location;
    std::string message;
    auto operator<=>(const Finding &) const = default;
};

struct Snapshot {
    std::map<std::string, Symbol> symbols;
    std::set<Finding> findings;
    std::map<Location, std::set<std::string>> declarations;
};

struct Change {
    std::string id;
    std::string kind;
    std::vector<std::string> path;
};

void hashComponents(Snapshot &snapshot);
std::vector<Change> compare(const Snapshot &base, const Snapshot &head);

} // namespace dagdocdiff
