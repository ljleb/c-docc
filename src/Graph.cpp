#include "Graph.h"

#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/StringExtras.h"

#include <algorithm>
#include <deque>
#include <utility>

namespace dagdocdiff {

Fingerprint::Fingerprint(llvm::StringRef domain) { add(domain); }

void Fingerprint::add(llvm::StringRef value) {
    hash_.update(std::to_string(value.size()));
    hash_.update(":");
    hash_.update(value);
}

std::string Fingerprint::finish() {
    return llvm::toHex(hash_.final(), /*LowerCase=*/true);
}

namespace {

struct Node {
    std::string id;
    Symbol *symbol = nullptr;
    std::vector<Node *> dependencies;
};

struct NodeTraits {
    using NodeRef = Node *;
    using ChildIteratorType = std::vector<Node *>::const_iterator;
    static NodeRef getEntryNode(NodeRef root) { return root; }
    static ChildIteratorType child_begin(NodeRef node) { return node->dependencies.begin(); }
    static ChildIteratorType child_end(NodeRef node) { return node->dependencies.end(); }
};

using Causes = std::map<std::string, std::string>;

// Store one hop toward the nearest direct change in this snapshot.
Causes findCauses(const Snapshot &snapshot, const std::set<std::string> &seeds) {
    std::map<std::string, std::vector<std::string>> dependents;
    std::deque<std::string> pending;
    Causes causes;
    for (const auto &[id, symbol] : snapshot.symbols) {
        for (const auto &edge : symbol.dependencies)
            dependents[edge.target].push_back(id);
    }
    // A seed can be absent from a snapshot with an unresolved edge to it.
    for (const auto &seed : seeds) {
        causes.emplace(seed, "");
        pending.push_back(seed);
    }
    while (!pending.empty()) {
        auto id = std::move(pending.front());
        pending.pop_front();
        const auto incoming = dependents.find(id);
        if (incoming == dependents.end())
            continue;
        for (const auto &dependent : incoming->second) {
            if (causes.try_emplace(dependent, id).second)
                pending.push_back(dependent);
        }
    }
    return causes;
}

std::vector<std::string> pathToCause(const Causes &causes, const std::string &id) {
    auto cause = causes.find(id);
    if (cause == causes.end())
        return {};
    std::vector<std::string> path{id};
    while (!cause->second.empty()) {
        path.push_back(cause->second);
        cause = causes.find(path.back());
    }
    return path;
}

bool sharesComponent(const Snapshot &snapshot, const std::vector<std::string> &path) {
    const auto cause = snapshot.symbols.find(path.back());
    return cause != snapshot.symbols.end() &&
           snapshot.symbols.at(path.front()).component == cause->second.component;
}

} // namespace

void hashComponents(Snapshot &snapshot) {
    std::map<std::string, Node> nodes;
    Node root;
    for (auto &[id, symbol] : snapshot.symbols) {
        auto &node = nodes.emplace(id, Node{id, &symbol, {}}).first->second;
        root.dependencies.push_back(&node);
    }
    for (auto &[id, node] : nodes) {
        for (const auto &edge : node.symbol->dependencies) {
            const auto target = nodes.find(edge.target);
            if (target != nodes.end()) {
                node.dependencies.push_back(&target->second);
            } else {
                snapshot.findings.insert({node.symbol->definition,
                    id + " has no indexed definition for " + edge.target});
            }
        }
    }

    using Iterator = llvm::scc_iterator<Node *, NodeTraits>;
    for (auto group = Iterator::begin(&root); group != Iterator::end(&root); ++group) {
        auto members = *group;
        if (members.front() == &root)
            continue;
        std::sort(members.begin(), members.end(),
                  [](const Node *a, const Node *b) { return a->id < b->id; });
        auto component = std::make_shared<Component>();
        for (auto *node : members) {
            node->symbol->component = component;
            component->members.push_back(node->id);
        }

        Fingerprint hash("dagdocdiff.scc.v1");
        for (const auto *node : members) {
            hash.add("member");
            hash.add(node->id);
            hash.add(node->symbol->kind);
            hash.add(node->symbol->localHash);
            for (const auto &edge : node->symbol->dependencies) {
                hash.add("edge");
                hash.add(edge.kind);
                hash.add(edge.target);
                const auto target = snapshot.symbols.find(edge.target);
                if (target == snapshot.symbols.end()) {
                    hash.add("unresolved");
                } else if (target->second.component == component) {
                    hash.add("internal");
                } else {
                    hash.add(target->second.component->hash);
                }
            }
        }
        component->hash = hash.finish();
    }
}

std::vector<Change> compare(const Snapshot &base, const Snapshot &head) {
    std::map<std::string, std::string> kinds;
    std::set<std::string> direct;
    for (const auto &[id, before] : base.symbols) {
        const auto after = head.symbols.find(id);
        if (after == head.symbols.end()) {
            kinds[id] = "removed";
        } else if (before.localHash != after->second.localHash ||
                   before.dependencies != after->second.dependencies ||
                   before.kind != after->second.kind) {
            kinds[id] = "direct";
        } else if (before.component->hash != after->second.component->hash) {
            kinds[id] = "dependency";
            continue;
        } else {
            continue;
        }
        direct.insert(id);
    }
    for (const auto &[id, symbol] : head.symbols) {
        if (!base.symbols.contains(id)) {
            kinds[id] = "added";
            direct.insert(id);
        }
    }

    const auto baseCauses = findCauses(base, direct);
    const auto headCauses = findCauses(head, direct);
    std::vector<Change> changes;
    for (auto &[id, kind] : kinds) {
        std::vector<std::string> path;
        if (kind == "dependency") {
            auto oldPath = pathToCause(baseCauses, id);
            auto newPath = pathToCause(headCauses, id);
            if (!newPath.empty() && (oldPath.empty() || newPath.size() <= oldPath.size())) {
                path = std::move(newPath);
                if (sharesComponent(head, path))
                    kind = "cycle";
            } else if (!oldPath.empty()) {
                path = std::move(oldPath);
                if (sharesComponent(base, path))
                    kind = "cycle";
            }
        }
        changes.push_back({id, std::move(kind), std::move(path)});
    }
    return changes;
}

} // namespace dagdocdiff
