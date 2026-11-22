#include "Docs.h"

#include "Analyzer.h"
#include "clang/Basic/Version.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/LockFileManager.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <cmark.h>

#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace dagdocdiff {
namespace {

namespace fs = std::filesystem;
namespace json = llvm::json;
using Hashes = std::map<std::string, std::string>;
using Links = std::map<std::string, std::set<std::string>>;
using Key = std::pair<std::string, std::string>;

constexpr auto approvalName = ".dagdocdiff-approvals.json";

struct Section {
    unsigned line;
    std::string textHash;
    Links links;
    std::vector<std::pair<unsigned, std::string>> issues;
};

struct Review {
    std::string textHash;
    std::string clangVersion;
    std::string reason;
    Links links;
    Hashes symbols;
    Hashes inputs;
};

using Sections = std::map<Key, Section>;
using Reviews = std::map<Key, Review>;

llvm::Error error(const llvm::Twine &message) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(), message);
}

llvm::Error fileError(const fs::path &path, std::error_code code) {
    return llvm::createStringError(code, "%s: %s", path.c_str(), code.message().c_str());
}

bool inside(const fs::path &path, const fs::path &root) {
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

std::optional<std::string> decodePath(llvm::StringRef path) {
    auto hex = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string decoded;
    while (!path.empty()) {
        char c = path.front();
        path = path.drop_front();
        if (c == '%') {
            if (path.size() < 2 || hex(path[0]) < 0 || hex(path[1]) < 0)
                return std::nullopt;
            c = static_cast<char>(16 * hex(path[0]) + hex(path[1]));
            path = path.drop_front(2);
        }
        if (c == '\0')
            return std::nullopt;
        decoded.push_back(c);
    }
    return decoded;
}

bool sourceFile(const fs::path &path) {
    static const std::set<fs::path> extensions{
        ".c", ".cc", ".cpp", ".cxx", ".C", ".h", ".hh", ".hpp", ".hxx", ".H", ".inc", ".ipp", ".tpp"
    };
    return extensions.contains(path.extension());
}

llvm::Expected<std::set<std::string>> resolve(llvm::StringRef url, const fs::path &page,
                                              const fs::path &source, const Snapshot &snapshot) {
    const auto [target, anchor] = url.split('#');
    if (!anchor.starts_with("L") || target.starts_with("/") ||
        target.split('/').first.contains(':'))
        return std::set<std::string>{};
    const auto path = target.split('?').first;
    auto decoded = decodePath(path);
    if (!sourceFile(path.str()) && (!decoded || !sourceFile(*decoded)))
        return std::set<std::string>{};
    if (!decoded)
        return error("invalid encoding in source link: " + url);
    const auto number = anchor.drop_front();
    unsigned line;
    if (target.contains('?') || number.empty() || number.find_first_not_of("0123456789") != llvm::StringRef::npos ||
        number.getAsInteger(10, line) || line == 0)
        return error("expected a relative source link with one #L<number> anchor: " + url);

    std::error_code ec;
    const auto file = fs::weakly_canonical(page.parent_path() / *decoded, ec);
    if (ec || fs::path(*decoded).is_absolute() || !inside(file, source))
        return error("source link leaves the source root or cannot be read: " + url);
    const auto relative = file.lexically_relative(source).generic_string();
    auto match = snapshot.declarations.lower_bound({relative, line, 0});
    if (match == snapshot.declarations.end() || match->first.file != relative || match->first.line != line)
        return error("no indexed declaration on " + url);
    const auto next = std::next(match);
    if (next != snapshot.declarations.end() && next->first.file == relative && next->first.line == line)
        return error("multiple declarations on " + url + "; choose a line with one declaration");
    for (const auto &id : match->second) {
        if (!snapshot.symbols.contains(id))
            return error("no indexed definition for " + id + " linked by " + url);
    }
    return match->second;
}

std::string headingText(cmark_node *node) {
    std::string result;
    if (auto *text = cmark_node_get_literal(node))
        result = text;
    for (auto *child = cmark_node_first_child(node); child; child = cmark_node_next(child))
        result += headingText(child);
    return result;
}

void collectLinks(cmark_node *node, unsigned blockLine, const fs::path &page,
                  const fs::path &source, const Snapshot &snapshot, Section &section) {
    if (cmark_node_get_type(node) == CMARK_NODE_LINK) {
        const auto url = llvm::StringRef(cmark_node_get_url(node));
        auto ids = resolve(url, page, source, snapshot);
        if (!ids) {
            const auto line = cmark_node_get_start_line(node);
            section.issues.emplace_back(line > 0 ? static_cast<unsigned>(line) : blockLine,
                                        llvm::toString(ids.takeError()));
        } else if (!ids->empty()) {
            section.links.try_emplace(url.str(), std::move(*ids));
        }
    }
    for (auto *child = cmark_node_first_child(node); child; child = cmark_node_next(child))
        collectLinks(child, blockLine, page, source, snapshot, section);
}

llvm::Error readPage(const fs::path &page, const fs::path &docs, const fs::path &source,
                     const Snapshot &snapshot, Sections &sections) {
    auto file = llvm::MemoryBuffer::getFile(page.string());
    if (!file)
        return fileError(page, file.getError());
    const auto text = (*file)->getBuffer();
    std::unique_ptr<cmark_node, decltype(&cmark_node_free)> tree(
        cmark_parse_document(text.data(), text.size(), CMARK_OPT_DEFAULT), cmark_node_free);
    const auto relative = page.lexically_relative(docs).generic_string();
    std::vector<size_t> starts{0};
    for (size_t i = 0; i < text.size(); ++i)
        if (text[i] == '\n')
            starts.push_back(i + 1);
    starts.push_back(text.size());

    std::vector<std::pair<int, std::string>> headings;
    std::string name = "(preamble)";
    Section section{1, {}, {}, {}};
    auto finish = [&](size_t end) -> llvm::Error {
        if (section.links.empty() && section.issues.empty())
            return llvm::Error::success();
        Fingerprint hash("dagdocdiff.doc-section.v1");
        hash.add(text.slice(starts.at(section.line - 1), end));
        section.textHash = hash.finish();
        if (!sections.try_emplace({relative, name}, std::move(section)).second)
            return error(relative + " has duplicate linked section " + name);
        return llvm::Error::success();
    };

    for (auto *block = cmark_node_first_child(tree.get()); block; block = cmark_node_next(block)) {
        const auto line = static_cast<unsigned>(cmark_node_get_start_line(block));
        if (cmark_node_get_type(block) == CMARK_NODE_HEADING) {
            if (auto result = finish(starts.at(line - 1)))
                return result;
            const int level = cmark_node_get_heading_level(block);
            while (!headings.empty() && headings.back().first >= level)
                headings.pop_back();
            headings.emplace_back(level, headingText(block));
            name.clear();
            for (const auto &[rank, heading] : headings) {
                if (!name.empty())
                    name += " / ";
                name += heading;
            }
            section = Section{line, {}, {}, {}};
        }
        collectLinks(block, line, page, source, snapshot, section);
    }
    return finish(text.size());
}

llvm::Expected<Sections> readDocs(const fs::path &docs, const fs::path &source,
                                 const Snapshot &snapshot) {
    std::error_code ec;
    Sections sections;
    for (fs::recursive_directory_iterator it(docs, ec), end; it != end && !ec; it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().extension() == ".md") {
            if (auto result = readPage(it->path(), docs, source, snapshot, sections))
                return std::move(result);
        }
        if (ec)
            return fileError(it->path(), ec);
    }
    if (ec)
        return fileError(docs, ec);
    return sections;
}

bool readHashes(const json::Object *object, Hashes &hashes) {
    if (!object)
        return false;
    for (const auto &[key, value] : *object) {
        const auto hash = value.getAsString();
        if (!hash)
            return false;
        hashes.emplace(key.str(), hash->str());
    }
    return true;
}

bool readLinks(const json::Object *object, Links &links) {
    if (!object)
        return false;
    for (const auto &[key, value] : *object) {
        const auto *array = value.getAsArray();
        if (!array || array->empty())
            return false;
        auto &ids = links[key.str()];
        for (const auto &entry : *array) {
            const auto id = entry.getAsString();
            if (!id)
                return false;
            ids.insert(id->str());
        }
    }
    return true;
}

llvm::Expected<Reviews> loadReviews(const fs::path &path) {
    auto buffer = llvm::MemoryBuffer::getFile(path.string());
    if (!buffer) {
        if (buffer.getError() == std::errc::no_such_file_or_directory)
            return Reviews{};
        return fileError(path, buffer.getError());
    }
    auto value = json::parse((*buffer)->getBuffer());
    if (!value)
        return value.takeError();
    const auto *object = value->getAsObject();
    const auto *sections = object ? object->getArray("sections") : nullptr;
    if (!sections || object->getInteger("schema_version") != 1)
        return error("invalid approvals format in " + path.string());

    Reviews result;
    for (const auto &item : *sections) {
        const auto *entry = item.getAsObject();
        if (!entry)
            return error("invalid approval in " + path.string());
        const auto page = entry->getString("page");
        const auto section = entry->getString("section");
        const auto text = entry->getString("text_hash");
        const auto clang = entry->getString("clang_version");
        const auto reason = entry->getString("reason");
        if (!page || !section || !text || !clang || !reason)
            return error("incomplete approval in " + path.string());
        Review review{text->str(), clang->str(), reason->str(), {}, {}, {}};
        if (!readLinks(entry->getObject("links"), review.links) ||
            !readHashes(entry->getObject("symbols"), review.symbols) ||
            !readHashes(entry->getObject("inputs"), review.inputs) ||
            !result.try_emplace({page->str(), section->str()}, std::move(review)).second)
            return error("invalid or duplicate approval for " + *page + " / " + *section);
    }
    return result;
}

json::Object hashesJson(const Hashes &hashes) {
    json::Object result;
    for (const auto &[key, hash] : hashes)
        result[key] = hash;
    return result;
}

llvm::Error saveReviews(const fs::path &path, const Reviews &reviews) {
    json::Array sections;
    for (const auto &[key, review] : reviews) {
        json::Object links;
        for (const auto &[url, ids] : review.links)
            links[url] = json::Array(std::vector<std::string>(ids.begin(), ids.end()));
        sections.push_back(json::Object{
            {"page", key.first}, {"section", key.second}, {"text_hash", review.textHash},
            {"clang_version", review.clangVersion}, {"reason", review.reason},
            {"links", std::move(links)}, {"symbols", hashesJson(review.symbols)},
            {"inputs", hashesJson(review.inputs)}
        });
    }
    auto temp = llvm::sys::fs::TempFile::create(path.string() + ".%%%%%%");
    if (!temp)
        return temp.takeError();
    llvm::raw_fd_ostream stream(temp->FD, /*shouldClose=*/false);
    stream << llvm::formatv("{0:2}\n", json::Value(json::Object{
        {"schema_version", 1}, {"sections", std::move(sections)}
    }));
    stream.flush();
    if (stream.has_error()) {
        const auto ec = stream.error();
        stream.clear_error();
        return llvm::joinErrors(fileError(path, ec), temp->discard());
    }
    return temp->keep(path.string());
}

Review currentReview(const Section &section, const Snapshot &snapshot, llvm::StringRef reason) {
    Review result{section.textHash, clang::getClangFullVersion(), reason.str(), section.links, {}, {}};
    std::vector<std::string> pending;
    for (const auto &[url, ids] : section.links) {
        for (const auto &id : ids) {
            if (result.symbols.try_emplace(id, snapshot.symbols.at(id).component->hash).second)
                pending.push_back(id);
        }
    }
    while (!pending.empty()) {
        auto id = std::move(pending.back());
        pending.pop_back();
        auto [entry, inserted] = result.inputs.try_emplace(id, "unresolved");
        if (!inserted)
            continue;
        const auto symbol = snapshot.symbols.find(id);
        if (symbol == snapshot.symbols.end())
            continue;
        Fingerprint hash("dagdocdiff.review-input.v1");
        hash.add(symbol->second.localHash);
        for (const auto &edge : symbol->second.dependencies) {
            hash.add(edge.kind);
            hash.add(edge.target);
            pending.push_back(edge.target);
        }
        entry->second = hash.finish();
    }
    return result;
}

std::string causePath(const std::string &root, const Review &before, const Review &after,
                      const Snapshot &snapshot) {
    std::map<std::string, std::string> parents{{root, ""}};
    std::deque<std::string> pending{root};
    while (!pending.empty()) {
        auto id = std::move(pending.front());
        pending.pop_front();
        const auto previous = before.inputs.find(id);
        if (previous == before.inputs.end() || previous->second != after.inputs.at(id)) {
            std::string path = id;
            for (auto parent = parents.at(id); !parent.empty(); parent = parents.at(parent))
                path = parent + " -> " + path;
            return path;
        }
        const auto symbol = snapshot.symbols.find(id);
        if (symbol != snapshot.symbols.end()) {
            for (const auto &edge : symbol->second.dependencies) {
                if (parents.try_emplace(edge.target, id).second)
                    pending.push_back(edge.target);
            }
        }
    }
    return root;
}

void report(const Key &key, unsigned line, llvm::StringRef message) {
    llvm::errs() << key.first << ':' << line << ": " << key.second << ": " << message << '\n';
}

struct Inputs {
    fs::path docs;
    Snapshot snapshot;
    Sections sections;
};

llvm::Expected<Inputs> loadInputs(llvm::StringRef source, llvm::StringRef build,
                                 llvm::StringRef docs) {
    std::error_code ec;
    const auto root = fs::canonical(source.str(), ec);
    if (ec)
        return fileError(source.str(), ec);
    const auto docsRoot = fs::canonical(docs.str(), ec);
    if (ec)
        return fileError(docs.str(), ec);
    if (!inside(docsRoot, root))
        return error("docs root must be inside the source root");
    auto snapshot = analyze(source, build);
    if (!snapshot)
        return snapshot.takeError();
    auto sections = readDocs(docsRoot, root, *snapshot);
    if (!sections)
        return sections.takeError();
    return Inputs{docsRoot, std::move(*snapshot), std::move(*sections)};
}

} // namespace

llvm::Error checkDocs(llvm::StringRef source, llvm::StringRef build, llvm::StringRef docs) {
    auto inputs = loadInputs(source, build, docs);
    if (!inputs)
        return inputs.takeError();
    auto reviews = loadReviews(inputs->docs / approvalName);
    if (!reviews)
        return reviews.takeError();
    bool failed = false;
    for (const auto &[key, section] : inputs->sections) {
        if (!section.issues.empty()) {
            for (const auto &[line, message] : section.issues)
                report(key, line, message);
            failed = true;
            continue;
        }
        const auto previous = reviews->find(key);
        if (previous == reviews->end()) {
            report(key, section.line, "approval required");
            failed = true;
            continue;
        }
        const auto &before = previous->second;
        const auto after = currentReview(section, inputs->snapshot, "");
        if (before.textHash != after.textHash) {
            report(key, section.line, "section text changed");
            failed = true;
        }
        if (before.clangVersion != after.clangVersion) {
            report(key, section.line, "Clang version changed");
            failed = true;
        }
        if (before.links != after.links) {
            report(key, section.line, "source link targets changed");
            for (const auto &[url, ids] : after.links) {
                const auto oldIds = before.links.find(url);
                if (oldIds != before.links.end() && oldIds->second != ids) {
                    report(key, section.line,
                           url + " now resolves to " + llvm::join(ids, ", ") +
                           "; previously " + llvm::join(oldIds->second, ", "));
                }
            }
            failed = true;
        }
        for (const auto &[id, hash] : after.symbols) {
            const auto oldHash = before.symbols.find(id);
            if (oldHash == before.symbols.end()) {
                report(key, section.line, "approval lacks a code fingerprint for " + id);
                failed = true;
            } else if (oldHash->second != hash) {
                report(key, section.line, "code changed: " + causePath(id, before, after, inputs->snapshot));
                failed = true;
            }
        }
    }
    for (const auto &[key, review] : *reviews) {
        if (!inputs->sections.contains(key)) {
            report(key, 0, "approved section or its source links were removed; approve the removal");
            failed = true;
        }
    }
    if (failed)
        return error("documentation needs review");
    llvm::outs() << "Approved documentation sections: " << inputs->sections.size() << '\n';
    return llvm::Error::success();
}

llvm::Error approveDocs(llvm::StringRef source, llvm::StringRef build, llvm::StringRef docs,
                        llvm::StringRef page, llvm::StringRef section, llvm::StringRef reason) {
    auto inputs = loadInputs(source, build, docs);
    if (!inputs)
        return inputs.takeError();
    const auto path = inputs->docs / approvalName;
    llvm::LockFileManager lock(path.string());
    auto acquired = lock.tryLock();
    if (!acquired)
        return acquired.takeError();
    if (!*acquired)
        return error("another approval is being written; retry after it finishes");
    auto reviews = loadReviews(path);
    if (!reviews)
        return reviews.takeError();
    const Key key{page.str(), section.str()};
    const auto current = inputs->sections.find(key);
    if (current == inputs->sections.end()) {
        if (reviews->erase(key) == 0)
            return error("no linked section or existing approval for " + page + " / " + section);
    } else {
        if (!current->second.issues.empty()) {
            for (const auto &[line, message] : current->second.issues)
                report(key, line, message);
            return error("resolve source links before approving this section");
        }
        reviews->insert_or_assign(key, currentReview(current->second, inputs->snapshot, reason.trim()));
    }
    if (auto result = saveReviews(path, *reviews))
        return result;
    llvm::outs() << "Recorded review for " << page << " / " << section << '\n';
    return llvm::Error::success();
}

} // namespace dagdocdiff
