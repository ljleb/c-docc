#include "Docs.h"

#include "Analyzer.h"
#include "clang/Basic/Version.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/LockFileManager.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <cmark.h>
#include <utf8proc.h>

#include <algorithm>
#include <deque>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace c_docc {
namespace {

namespace fs = std::filesystem;
namespace json = llvm::json;
using Hashes = std::map<std::string, std::string>;
using Links = std::map<std::string, std::set<std::string>>;
using Key = std::pair<std::string, std::string>;
struct Issue {
    unsigned line;
    std::string message;
    std::optional<Location> cause = std::nullopt;
};

using Issues = std::vector<Issue>;

constexpr auto approvalName = ".c-docc-approvals.json";

struct LinkTarget {
    std::string file;
    unsigned firstLine;
    unsigned lastLine;
};

struct LinkOccurrence {
    unsigned line;
    std::string text;
    std::string url;
    LinkTarget target;
};

struct Section {
    unsigned line = 1;
    std::string textHash;
    Links links;
    Issues issues;
    std::vector<LinkOccurrence> occurrences;
    Hashes fileLinks;
    Hashes declarationLinks;
    Hashes sourceTextLinks;
    std::vector<LinkOccurrence> fileOccurrences;
};

struct Review {
    std::string textHash;
    std::string clangVersion;
    std::string reason;
    Links links;
    Hashes symbols;
    Hashes inputs;
    Hashes fileLinks;
    Hashes declarationLinks;
    Hashes sourceTextLinks;

    bool operator==(const Review &) const = default;
};

using Sections = std::map<Key, Section>;
using Reviews = std::map<Key, Review>;

llvm::Error error(const llvm::Twine &message) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(), message);
}

llvm::Error fileError(const fs::path &path, std::error_code code) {
    return llvm::createStringError(code, "%s: %s", path.string().c_str(), code.message().c_str());
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

std::vector<size_t> lineOffsets(llvm::StringRef text) {
    std::vector<size_t> starts{0};
    for (size_t i = 0; i < text.size(); ++i)
        if (text[i] == '\n')
            starts.push_back(i + 1);
    return starts;
}

std::optional<unsigned> sourceLine(llvm::StringRef anchor) {
    unsigned line;
    if (!anchor.consume_front("L") || anchor.empty() ||
        anchor.find_first_not_of("0123456789") != llvm::StringRef::npos ||
        anchor.getAsInteger(10, line) || line == 0)
        return std::nullopt;
    return line;
}

struct SourceLink {
    LinkTarget target;
    std::set<std::string> definitions;
    std::string declarations;
    std::string text;
};

class SourceLinks {
public:
    SourceLinks(const fs::path &source, const Snapshot &snapshot)
        : source(source), snapshot(snapshot) {}

    llvm::Expected<std::optional<SourceLink>> resolve(llvm::StringRef url, const fs::path &page) {
        const auto [target, anchor] = url.split('#');
        if (!anchor.starts_with("L") || target.starts_with("/") || target.split('/').first.contains(':'))
            return std::optional<SourceLink>{};
        const auto path = target.split('?').first;
        auto decoded = decodePath(path);
        if (!sourceFile(path.str()) && (!decoded || !sourceFile(*decoded)))
            return std::optional<SourceLink>{};
        if (!decoded)
            return error("invalid encoding in source link: " + url);

        const auto [startAnchor, endAnchor] = anchor.split('-');
        const bool range = anchor.contains('-');
        const auto first = sourceLine(startAnchor);
        const auto last = range ? sourceLine(endAnchor) : first;
        if (target.contains('?') || !first || !last || *last < *first)
            return error("expected a source link with a #L<number> or #L<number>-L<number> anchor: " + url);

        std::error_code ec;
        const auto file = fs::weakly_canonical(page.parent_path() / *decoded, ec);
        if (ec || fs::path(*decoded).is_absolute() || !inside(file, source))
            return error("source link leaves the source root or cannot be read: " + url);
        if (!fs::is_regular_file(file, ec))
            return error("cannot read source link target: " + url);

        auto sourceFile = files.find(file);
        if (sourceFile == files.end()) {
            auto buffer = llvm::MemoryBuffer::getFile(file.string());
            if (!buffer)
                return fileError(file, buffer.getError());
            auto starts = lineOffsets((*buffer)->getBuffer());
            sourceFile = files.emplace(file, File{std::move(*buffer), std::move(starts)}).first;
        }
        const auto &contents = sourceFile->second;
        const auto text = contents.buffer->getBuffer();
        const auto lineCount = text.empty() ? 0 : contents.starts.size() - text.ends_with("\n");
        if (*last > lineCount)
            return error("source link goes beyond the end of the file: " + url);

        const auto relative = file.lexically_relative(source).generic_string();
        SourceLink link{{relative, *first, *last}, {}, {}, {}};
        std::set<std::pair<std::string, std::string>> declarations;
        std::vector<bool> covered(static_cast<size_t>(*last) - *first + 1);
        std::optional<std::pair<unsigned, unsigned>> matched;
        for (auto it = snapshot.sourceSpans.lower_bound({{relative, 0, 0}, 0, 0, 0});
             it != snapshot.sourceSpans.end() && it->first.start.file == relative &&
             it->first.start.line <= *last; ++it) {
            const auto &[span, targets] = *it;
            if (span.endLine < *first)
                continue;
            const auto site = std::pair(span.nameLine, span.nameColumn);
            if (!range && matched && *matched != site)
                return error("multiple declarations on " + url + "; use a line range to track them together");
            matched = site;
            for (const auto &id : targets.definitions) {
                if (!snapshot.symbols.contains(id))
                    return error("no indexed definition for " + id + " linked by " + url);
                link.definitions.insert(id);
            }
            for (const auto &[id, hashes] : targets.declarations) {
                for (const auto &hash : hashes)
                    declarations.emplace(id, hash);
            }
            const auto begin = std::max(*first, span.start.line) - *first;
            const auto end = std::min(*last, span.endLine) - *first + 1;
            std::fill(covered.begin() + begin, covered.begin() + end, true);
        }

        if (!declarations.empty()) {
            Fingerprint hash("dagdocdiff.linked-declarations.v1");
            for (const auto &[id, fingerprint] : declarations) {
                hash.add(id);
                hash.add(fingerprint);
            }
            link.declarations = hash.finish();
        }
        if (llvm::is_contained(covered, false)) {
            Fingerprint hash("dagdocdiff.linked-source-text.v1");
            hash.add(relative);
            for (size_t i = 0; i < covered.size(); ++i) {
                if (covered[i])
                    continue;
                const auto line = static_cast<size_t>(*first) + i;
                const auto end = line < contents.starts.size() ? contents.starts[line] : text.size();
                hash.add(std::to_string(i));
                hash.add(text.slice(contents.starts[line - 1], end));
            }
            link.text = hash.finish();
        }
        return std::optional<SourceLink>(std::move(link));
    }

private:
    struct File {
        std::unique_ptr<llvm::MemoryBuffer> buffer;
        std::vector<size_t> starts;
    };

    const fs::path &source;
    const Snapshot &snapshot;
    std::map<fs::path, File> files;
};

std::string inlineText(cmark_node *node, llvm::StringRef lineBreak = "", bool omitHtml = false) {
    const auto type = cmark_node_get_type(node);
    if (type == CMARK_NODE_SOFTBREAK || type == CMARK_NODE_LINEBREAK)
        return lineBreak.str();
    if (omitHtml && type == CMARK_NODE_HTML_INLINE)
        return {};
    std::string result;
    if (auto *text = cmark_node_get_literal(node))
        result = text;
    for (auto *child = cmark_node_first_child(node); child; child = cmark_node_next(child))
        result += inlineText(child, lineBreak, omitHtml);
    return result;
}

bool markdownFile(const fs::path &path) {
    const auto extension = path.extension().string();
    const auto ext = llvm::StringRef(extension);
    return ext.equals_insensitive(".md") || ext.equals_insensitive(".markdown");
}

std::string headingSlug(llvm::StringRef heading) {
    std::string slug;
    heading = heading.trim();
    while (!heading.empty()) {
        utf8proc_int32_t character;
        const auto size = utf8proc_iterate(
            reinterpret_cast<const utf8proc_uint8_t *>(heading.data()), heading.size(), &character);
        if (size < 0) {
            heading = heading.drop_front();
            continue;
        }
        heading = heading.drop_front(size);
        if (character == ' ' || character == '-' || character == '_') {
            slug += character == ' ' ? '-' : static_cast<char>(character);
            continue;
        }
        const auto category = utf8proc_category(character);
        if (category >= UTF8PROC_CATEGORY_LU && category <= UTF8PROC_CATEGORY_NO) {
            utf8proc_uint8_t bytes[4];
            const auto length = utf8proc_encode_char(utf8proc_tolower(character), bytes);
            slug.append(reinterpret_cast<const char *>(bytes), length);
        }
    }
    return slug;
}

std::string contentFingerprint(llvm::StringRef path, llvm::StringRef text) {
    Fingerprint hash("dagdocdiff.linked-content.v1");
    hash.add(path);
    hash.add(text);
    return hash.finish();
}

struct HeadingFingerprint {
    std::string hash;
    unsigned line;
};

std::map<std::string, HeadingFingerprint> headingFingerprints(llvm::StringRef path, llvm::StringRef text,
                                                              cmark_node *tree) {
    struct Heading {
        int level;
        size_t start;
        unsigned line;
        std::string anchor;
    };
    const auto starts = lineOffsets(text);
    std::vector<Heading> headings;
    std::set<std::string> anchors;
    std::map<std::string, unsigned> suffixes;
    std::unique_ptr<cmark_iter, decltype(&cmark_iter_free)> nodes(cmark_iter_new(tree), cmark_iter_free);
    for (auto event = cmark_iter_next(nodes.get()); event != CMARK_EVENT_DONE;
         event = cmark_iter_next(nodes.get())) {
        auto *node = cmark_iter_get_node(nodes.get());
        if (event != CMARK_EVENT_ENTER || cmark_node_get_type(node) != CMARK_NODE_HEADING)
            continue;
        const auto slug = headingSlug(inlineText(node, " ", true));
        auto anchor = slug;
        if (!anchor.empty()) {
            while (!anchors.insert(anchor).second)
                anchor = slug + "-" + std::to_string(++suffixes[slug]);
        }
        const auto line = static_cast<unsigned>(cmark_node_get_start_line(node));
        headings.push_back({cmark_node_get_heading_level(node), starts.at(line - 1), line, std::move(anchor)});
    }

    std::map<std::string, HeadingFingerprint> result;
    for (auto heading = headings.begin(); heading != headings.end(); ++heading) {
        if (heading->anchor.empty())
            continue;
        auto end = std::next(heading);
        while (end != headings.end() && end->level > heading->level)
            ++end;
        const auto offset = end == headings.end() ? text.size() : end->start;
        const auto contents = text.slice(heading->start, offset).rtrim();
        result.emplace(heading->anchor, HeadingFingerprint{contentFingerprint(path, contents), heading->line});
    }
    return result;
}

struct FileLink {
    std::string hash;
    LinkTarget target;
};

class MarkdownLinks {
public:
    explicit MarkdownLinks(const fs::path &source) : source(source) {}

    llvm::Expected<std::optional<FileLink>> resolve(llvm::StringRef url, const fs::path &page) {
        const auto [destination, fragment] = url.split('#');
        const auto encodedPath = destination.split('?').first;
        if (encodedPath.starts_with("//") || encodedPath.split('/').first.contains(':'))
            return std::optional<FileLink>{};
        const auto decoded = decodePath(encodedPath);
        if (!decoded)
            return error("invalid encoding in local link: " + url);

        const fs::path path(*decoded);
        const auto base = (decoded->starts_with('/') || path.is_absolute()) ? source : page.parent_path();
        std::error_code ec;
        auto target = fs::weakly_canonical(path.empty() ? page : base / path.relative_path(), ec);
        if (ec || !inside(target, source))
            return error("local link leaves the source directory or cannot be read: " + url);
        const auto status = fs::status(target, ec);
        if (!fs::exists(status))
            return error("local link target does not exist: " + url);

        if (fs::is_directory(status) && !fragment.empty()) {
            target = fs::weakly_canonical(target / "README.md", ec);
            if (ec || !inside(target, source) || !fs::is_regular_file(target, ec))
                return error("local link directory has no Markdown index for the fragment: " + url);
        } else if (!fs::is_regular_file(status) || (!fragment.empty() && !markdownFile(target))) {
            return std::optional<FileLink>{};
        }
        const bool samePage = path.empty() || target == page;
        if (fragment.empty() && samePage)
            return std::optional<FileLink>{};

        const auto relative = target.lexically_relative(source).generic_string();
        auto found = files.find(target);
        if (found == files.end()) {
            auto contents = llvm::MemoryBuffer::getFile(target.string());
            if (!contents)
                return error("cannot read local link target: " + url);
            const auto text = (*contents)->getBuffer();
            FileFingerprints fingerprints{contentFingerprint(relative, text), {}};
            if (markdownFile(target)) {
                std::unique_ptr<cmark_node, decltype(&cmark_node_free)> tree(
                    cmark_parse_document(text.data(), text.size(), CMARK_OPT_DEFAULT), cmark_node_free);
                fingerprints.headings = headingFingerprints(relative, text, tree.get());
            }
            found = files.emplace(target, std::move(fingerprints)).first;
        }
        if (fragment.empty())
            return std::optional<FileLink>{{found->second.whole, {relative, 1, 1}}};
        const auto anchor = decodePath(fragment);
        if (!anchor)
            return error("invalid encoding in local link fragment: " + url);
        const auto heading = found->second.headings.find(*anchor);
        if (heading == found->second.headings.end())
            return error("Markdown heading not found: " + url);
        if (samePage)
            return std::optional<FileLink>{};
        const auto line = heading->second.line;
        return std::optional<FileLink>{{heading->second.hash, {relative, line, line}}};
    }

private:
    struct FileFingerprints {
        std::string whole;
        std::map<std::string, HeadingFingerprint> headings;
    };

    const fs::path &source;
    std::map<fs::path, FileFingerprints> files;
};

std::string linkMessage(llvm::StringRef text, llvm::StringRef message) {
    return llvm::formatv("{0}: {1}", json::Value(text), message).str();
}

unsigned linkLine(cmark_node *node, unsigned fallback) {
    auto line = cmark_node_get_start_line(node);
    // cmark can place a multiline link on the line with its closing bracket.
    for (auto *child = cmark_node_first_child(node); child; child = cmark_node_next(child)) {
        const auto childLine = cmark_node_get_start_line(child);
        if (childLine > 0 && (line <= 0 || childLine < line))
            line = childLine;
    }
    return line > 0 ? static_cast<unsigned>(line) : fallback;
}

void collectLinks(cmark_node *node, unsigned blockLine, const fs::path &page,
                  Section &section, SourceLinks &sourceLinks, MarkdownLinks &markdown) {
    if (cmark_node_get_type(node) == CMARK_NODE_LINK) {
        const auto url = llvm::StringRef(cmark_node_get_url(node));
        auto source = sourceLinks.resolve(url, page);
        const auto line = linkLine(node, blockLine);
        if (!source) {
            section.issues.emplace_back(line, linkMessage(inlineText(node, " "), llvm::toString(source.takeError())));
        } else if (*source) {
            auto &link = **source;
            const auto key = url.str();
            if (!link.definitions.empty())
                section.links.try_emplace(key, std::move(link.definitions));
            if (!link.declarations.empty())
                section.declarationLinks.try_emplace(key, std::move(link.declarations));
            if (!link.text.empty())
                section.sourceTextLinks.try_emplace(key, std::move(link.text));
            section.occurrences.push_back({line, inlineText(node, " "), key, std::move(link.target)});
        } else {
            auto fingerprint = markdown.resolve(url, page);
            if (!fingerprint) {
                section.issues.emplace_back(line, linkMessage(inlineText(node, " "),
                                                               llvm::toString(fingerprint.takeError())));
            } else if (*fingerprint) {
                auto &link = **fingerprint;
                section.fileLinks.try_emplace(url.str(), std::move(link.hash));
                section.fileOccurrences.push_back({line, inlineText(node, " "), url.str(), std::move(link.target)});
            }
        }
    }
    for (auto *child = cmark_node_first_child(node); child; child = cmark_node_next(child))
        collectLinks(child, blockLine, page, section, sourceLinks, markdown);
}

llvm::Error readPage(const fs::path &page, const fs::path &docs, Sections &sections,
                     SourceLinks &sourceLinks, MarkdownLinks &markdown) {
    auto file = llvm::MemoryBuffer::getFile(page.string());
    if (!file)
        return fileError(page, file.getError());
    const auto text = (*file)->getBuffer();
    std::unique_ptr<cmark_node, decltype(&cmark_node_free)> tree(
        cmark_parse_document(text.data(), text.size(), CMARK_OPT_DEFAULT), cmark_node_free);
    const auto relative = page.lexically_relative(docs).generic_string();
    const auto starts = lineOffsets(text);

    std::vector<std::pair<int, std::string>> headings;
    std::string name = "(preamble)";
    Section section;
    auto finish = [&](size_t end) -> llvm::Error {
        if (section.occurrences.empty() && section.fileOccurrences.empty() && section.issues.empty())
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
            headings.emplace_back(level, inlineText(block));
            name.clear();
            for (const auto &[rank, heading] : headings) {
                if (!name.empty())
                    name += " / ";
                name += heading;
            }
            section = Section{.line = line};
        }
        collectLinks(block, line, page, section, sourceLinks, markdown);
    }
    return finish(text.size());
}

llvm::Expected<Sections> readDocs(const fs::path &docs, const fs::path &source,
                                 const Snapshot &snapshot) {
    SourceLinks sourceLinks(source, snapshot);
    MarkdownLinks markdown(source);
    std::error_code ec;
    Sections sections;
    for (fs::recursive_directory_iterator it(docs, ec), end; it != end && !ec; it.increment(ec)) {
        if (it->is_regular_file(ec) && markdownFile(it->path())) {
            if (auto result = readPage(it->path(), docs, sections, sourceLinks, markdown))
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

fs::path approvalPath(const fs::path &docs) {
    const auto path = docs / approvalName;
    std::error_code ec;
    if (fs::exists(path, ec) || ec)
        return path;
    const auto previous = docs / ".dagdocdiff-approvals.json";
    return fs::exists(previous, ec) || ec ? previous : path;
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
        Review review{text->str(), clang->str(), reason->str(), {}, {}, {}, {}, {}, {}};
        const auto *fileLinks = entry->get("file_links");
        const auto *declarationLinks = entry->get("declaration_links");
        const auto *sourceTextLinks = entry->get("source_text_links");
        if (!readLinks(entry->getObject("links"), review.links) ||
            !readHashes(entry->getObject("symbols"), review.symbols) ||
            !readHashes(entry->getObject("inputs"), review.inputs) ||
            (fileLinks && !readHashes(fileLinks->getAsObject(), review.fileLinks)) ||
            (declarationLinks && !readHashes(declarationLinks->getAsObject(), review.declarationLinks)) ||
            (sourceTextLinks && !readHashes(sourceTextLinks->getAsObject(), review.sourceTextLinks)) ||
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
        json::Object entry{
            {"page", key.first}, {"section", key.second}, {"text_hash", review.textHash},
            {"clang_version", review.clangVersion}, {"reason", review.reason},
            {"links", std::move(links)}, {"symbols", hashesJson(review.symbols)},
            {"inputs", hashesJson(review.inputs)}
        };
        if (!review.fileLinks.empty())
            entry["file_links"] = hashesJson(review.fileLinks);
        if (!review.declarationLinks.empty())
            entry["declaration_links"] = hashesJson(review.declarationLinks);
        if (!review.sourceTextLinks.empty())
            entry["source_text_links"] = hashesJson(review.sourceTextLinks);
        sections.push_back(std::move(entry));
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
    Review result{section.textHash, clang::getClangFullVersion(), reason.str(), section.links,
                  {}, {}, section.fileLinks, section.declarationLinks, section.sourceTextLinks};
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

struct ChangeCause {
    std::string description;
    std::optional<Location> location;
};

ChangeCause describeCause(const std::string &id, const Snapshot &snapshot) {
    const auto symbol = snapshot.symbols.find(id);
    if (symbol == snapshot.symbols.end())
        return {id, std::nullopt};
    const auto &location = symbol->second.definition;
    return {id + " (" + location.file + ":" + std::to_string(location.line) + ")", location};
}

ChangeCause causePath(const std::string &root, const Review &before, const Review &after,
                      const Snapshot &snapshot) {
    std::map<std::string, std::string> parents{{root, ""}};
    std::deque<std::string> pending{root};
    while (!pending.empty()) {
        auto id = std::move(pending.front());
        pending.pop_front();
        const auto previous = before.inputs.find(id);
        if (previous == before.inputs.end() || previous->second != after.inputs.at(id)) {
            auto cause = describeCause(id, snapshot);
            for (auto parent = parents.at(id); !parent.empty(); parent = parents.at(parent))
                cause.description = parent + " -> " + cause.description;
            return cause;
        }
        const auto symbol = snapshot.symbols.find(id);
        if (symbol != snapshot.symbols.end()) {
            for (const auto &edge : symbol->second.dependencies) {
                if (parents.try_emplace(edge.target, id).second)
                    pending.push_back(edge.target);
            }
        }
    }
    return describeCause(root, snapshot);
}

void report(const Key &key, unsigned line, llvm::StringRef message) {
    llvm::errs() << key.first << ':' << line << ": " << key.second << ": " << message << '\n';
}

struct Inputs {
    fs::path source;
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
    return Inputs{root, docsRoot, std::move(*snapshot), std::move(*sections)};
}

void reportSourceFingerprint(Issues &issues, const LinkOccurrence &link, const Hashes &before,
                             const Hashes &after, llvm::StringRef change) {
    const auto previous = before.find(link.url);
    const auto current = after.find(link.url);
    if (previous == before.end()) {
        if (current != after.end())
            issues.emplace_back(link.line, linkMessage(link.text, "approval lacks a fingerprint for " + link.url));
    } else if (current == after.end() || previous->second != current->second) {
        issues.emplace_back(link.line, linkMessage(link.text, (change + ": " + link.url).str()));
    }
}

Issues reviewIssues(const Section &section, const Review &before, const Snapshot &snapshot) {
    const auto after = currentReview(section, snapshot, "");
    Issues issues;
    if (before.textHash != after.textHash)
        issues.emplace_back(section.line, "section text changed");
    if (!after.links.empty() && before.clangVersion != after.clangVersion)
        issues.emplace_back(section.line, "Clang version changed");
    if (before.links != after.links)
        issues.emplace_back(section.line, "source link targets changed");

    std::map<std::string, ChangeCause> symbolIssues;
    for (const auto &[id, hash] : after.symbols) {
        const auto oldHash = before.symbols.find(id);
        if (oldHash == before.symbols.end()) {
            symbolIssues.emplace(id, ChangeCause{"approval lacks a code fingerprint for " + id, std::nullopt});
        } else if (oldHash->second != hash) {
            auto cause = causePath(id, before, after, snapshot);
            cause.description = "code changed: " + cause.description;
            symbolIssues.emplace(id, std::move(cause));
        }
    }
    for (const auto &link : section.occurrences) {
        const auto currentIds = after.links.find(link.url);
        if (currentIds != after.links.end()) {
            const auto &ids = currentIds->second;
            const auto oldIds = before.links.find(link.url);
            if (oldIds != before.links.end() && oldIds->second != ids) {
                issues.emplace_back(link.line, linkMessage(link.text, link.url + " now resolves to " +
                                    llvm::join(ids, ", ") + "; previously " + llvm::join(oldIds->second, ", ")));
            }
            for (const auto &id : ids) {
                const auto issue = symbolIssues.find(id);
                if (issue != symbolIssues.end())
                    issues.emplace_back(link.line, linkMessage(link.text, issue->second.description),
                                        issue->second.location);
            }
        }
        reportSourceFingerprint(issues, link, before.declarationLinks, after.declarationLinks, "declaration changed");
        reportSourceFingerprint(issues, link, before.sourceTextLinks, after.sourceTextLinks, "source text changed");
    }
    for (const auto &link : section.fileOccurrences) {
        const auto previous = before.fileLinks.find(link.url);
        if (previous == before.fileLinks.end()) {
            issues.emplace_back(link.line, linkMessage(link.text, "approval lacks a fingerprint for " + link.url));
        } else if (previous->second != after.fileLinks.at(link.url)) {
            const auto kind = llvm::StringRef(link.url).split('#').second.empty() ? "file" : "section";
            issues.emplace_back(link.line, linkMessage(link.text, "linked " + std::string(kind) + " changed: " + link.url));
        }
    }
    return issues;
}

struct PendingReview {
    Key key;
    unsigned line;
    const Section *section;
    Issues issues;
};

std::vector<PendingReview> pendingReviews(const Inputs &inputs, const Reviews &reviews) {
    std::vector<PendingReview> result;
    for (const auto &[key, section] : inputs.sections) {
        if (!section.issues.empty()) {
            result.push_back({key, section.line, &section, section.issues});
            continue;
        }
        const auto previous = reviews.find(key);
        if (previous == reviews.end()) {
            result.push_back({key, section.line, &section, {{section.line, "approval required"}}});
        } else if (auto issues = reviewIssues(section, previous->second, inputs.snapshot); !issues.empty()) {
            result.push_back({key, section.line, &section, std::move(issues)});
        }
    }
    for (const auto &[key, review] : reviews) {
        if (!inputs.sections.contains(key)) {
            result.push_back({key, 0, nullptr,
                              {{0, "approved section or its tracked links were removed; approve the removal"}}});
        }
    }
    llvm::sort(result, [](const PendingReview &a, const PendingReview &b) {
        return std::tie(a.key.first, a.line, a.key.second) < std::tie(b.key.first, b.line, b.key.second);
    });
    return result;
}

llvm::Error updateReviews(const fs::path &path, llvm::function_ref<llvm::Error(Reviews &)> update) {
    llvm::LockFileManager lock(path.string());
    auto acquired = lock.tryLock();
    if (!acquired)
        return acquired.takeError();
    if (!*acquired)
        return error("another approval is being written; retry after it finishes");
    auto reviews = loadReviews(path);
    if (!reviews)
        return reviews.takeError();
    if (auto result = update(*reviews))
        return result;
    return saveReviews(path, *reviews);
}

llvm::Error recordReview(const fs::path &path, const Key &key, std::optional<Review> review,
                         const Reviews *baseline = nullptr) {
    auto result = updateReviews(path, [&](Reviews &reviews) -> llvm::Error {
        if (baseline) {
            const auto before = baseline->find(key);
            const auto current = reviews.find(key);
            const bool hadApproval = before != baseline->end();
            const bool hasApproval = current != reviews.end();
            if (hadApproval != hasApproval || (hasApproval && before->second != current->second))
                return error("approval changed during review for " + key.first + " / " + key.second + "; run approve again");
        }
        if (review) {
            reviews.insert_or_assign(key, std::move(*review));
        } else if (reviews.erase(key) == 0) {
            return error("no linked section or existing approval for " + key.first + " / " + key.second);
        }
        return llvm::Error::success();
    });
    if (result)
        return result;
    llvm::outs() << "Recorded review for " << key.first << " / " << key.second << '\n';
    return llvm::Error::success();
}

enum class Decision { Approve, Revise, Quit };

Decision askForDecision() {
    while (true) {
        llvm::outs() << "Approve [y], leave for revision [r], or quit [q] (default r): ";
        llvm::outs().flush();
        std::string answer;
        if (!std::getline(std::cin, answer)) {
            llvm::outs() << '\n';
            return Decision::Quit;
        }
        const auto choice = llvm::StringRef(answer).trim().lower();
        if (choice == "y")
            return Decision::Approve;
        if (choice.empty() || choice == "r" || choice == "n")
            return Decision::Revise;
        if (choice == "q")
            return Decision::Quit;
        llvm::outs() << "Enter y, r, or q.\n";
    }
}

void printStatus(const Inputs &inputs, const std::vector<PendingReview> &pending) {
    json::Array items;
    for (const auto &review : pending) {
        json::Array issues;
        for (const auto &issue : review.issues) {
            json::Object item{{"line", issue.line}, {"message", issue.message}};
            if (issue.cause) {
                const auto &cause = *issue.cause;
                item["cause"] = json::Object{{"file", cause.file}, {"line", cause.line}, {"column", cause.column}};
            }
            issues.push_back(std::move(item));
        }
        items.push_back(json::Object{
            {"page", review.key.first}, {"section", review.key.second}, {"line", review.line},
            {"approvable", !review.section || review.section->issues.empty()}, {"issues", std::move(issues)}
        });
    }
    json::Array references;
    json::Array fileReferences;
    for (const auto &[key, section] : inputs.sections) {
        auto appendReferences = [&](json::Array &out, const std::vector<LinkOccurrence> &links) {
            for (const auto &link : links) {
                out.push_back(json::Object{
                    {"file", link.target.file}, {"first_line", link.target.firstLine}, {"last_line", link.target.lastLine},
                    {"page", key.first}, {"section", key.second}, {"line", link.line}, {"text", link.text}
                });
            }
        };
        appendReferences(references, section.occurrences);
        appendReferences(fileReferences, section.fileOccurrences);
    }
    llvm::outs() << llvm::formatv("{0:2}\n", json::Value(json::Object{
        {"schema_version", 1}, {"source_dir", inputs.source.string()}, {"docs_dir", inputs.docs.string()},
        {"pending", std::move(items)}, {"references", std::move(references)}, {"file_references", std::move(fileReferences)}
    }));
}

} // namespace

llvm::Error checkDocs(llvm::StringRef source, llvm::StringRef build, llvm::StringRef docs, bool json) {
    auto inputs = loadInputs(source, build, docs);
    if (!inputs)
        return inputs.takeError();
    auto reviews = loadReviews(approvalPath(inputs->docs));
    if (!reviews)
        return reviews.takeError();
    const auto pending = pendingReviews(*inputs, *reviews);
    if (json) {
        printStatus(*inputs, pending);
        return pending.empty() ? llvm::Error::success() : error("documentation needs review");
    }
    for (const auto &item : pending) {
        for (const auto &issue : item.issues)
            report(item.key, issue.line, issue.message);
    }
    if (!pending.empty())
        return error("documentation needs review");
    std::vector<std::tuple<llvm::StringRef, unsigned, llvm::StringRef>> approved;
    approved.reserve(inputs->sections.size());
    for (const auto &[key, section] : inputs->sections)
        approved.emplace_back(key.first, section.line, key.second);
    llvm::sort(approved);

    llvm::outs() << "Approved documentation sections: " << approved.size() << '\n';
    for (const auto &[page, line, heading] : approved)
        llvm::outs() << "  " << page << ':' << line << ": " << heading << '\n';
    return llvm::Error::success();
}

llvm::Error reviewDocs(llvm::StringRef source, llvm::StringRef build, llvm::StringRef docs,
                       llvm::StringRef reason) {
    auto inputs = loadInputs(source, build, docs);
    if (!inputs)
        return inputs.takeError();
    const auto path = approvalPath(inputs->docs);
    auto reviews = loadReviews(path);
    if (!reviews)
        return reviews.takeError();
    const auto pending = pendingReviews(*inputs, *reviews);
    if (pending.empty())
        llvm::outs() << "No sections need approval.\n";
    for (const auto &item : pending) {
        llvm::outs() << '\n' << (inputs->docs / item.key.first).string();
        if (item.line)
            llvm::outs() << ':' << item.line;
        llvm::outs() << "  " << item.key.second << '\n';
        for (const auto &issue : item.issues) {
            llvm::outs() << "  ";
            if (issue.line && issue.line != item.line)
                llvm::outs() << "Line " << issue.line << ": ";
            llvm::outs() << issue.message << '\n';
        }
        if (item.section && !item.section->issues.empty()) {
            llvm::outs() << "Fix the broken links before approving this section.\n";
            continue;
        }

        switch (askForDecision()) {
        case Decision::Quit:
            return llvm::Error::success();
        case Decision::Revise:
            llvm::outs() << "Left for revision.\n";
            continue;
        case Decision::Approve:
            std::optional<Review> review;
            if (item.section)
                review = currentReview(*item.section, inputs->snapshot, reason.trim());
            if (auto result = recordReview(path, item.key, std::move(review), &*reviews))
                return result;
            break;
        }
    }
    return llvm::Error::success();
}

llvm::Error approvePage(llvm::StringRef source, llvm::StringRef build, llvm::StringRef docs,
                        llvm::StringRef page, llvm::StringRef reason) {
    auto inputs = loadInputs(source, build, docs);
    if (!inputs)
        return inputs.takeError();
    const auto pagePath = fs::path(page.str()).lexically_normal().generic_string();
    Reviews replacements;
    bool blocked = false;
    for (const auto &[key, section] : inputs->sections) {
        if (key.first != pagePath)
            continue;
        if (!section.issues.empty()) {
            for (const auto &issue : section.issues)
                report(key, issue.line, issue.message);
            blocked = true;
            continue;
        }
        replacements.emplace(key, currentReview(section, inputs->snapshot, reason.trim()));
    }
    if (blocked)
        return error("resolve links before approving this page");

    auto result = updateReviews(approvalPath(inputs->docs), [&](Reviews &reviews) -> llvm::Error {
        const auto count = std::erase_if(reviews, [&](const auto &entry) {
            return entry.first.first == pagePath;
        });
        if (replacements.empty() && count == 0)
            return error("no linked sections or existing approvals for " + pagePath);
        reviews.merge(replacements);
        return llvm::Error::success();
    });
    if (result)
        return result;
    llvm::outs() << "Recorded review for " << pagePath << '\n';
    return llvm::Error::success();
}

llvm::Error approveDocs(llvm::StringRef source, llvm::StringRef build, llvm::StringRef docs,
                        llvm::StringRef page, llvm::StringRef section, llvm::StringRef reason) {
    auto inputs = loadInputs(source, build, docs);
    if (!inputs)
        return inputs.takeError();
    const Key key{fs::path(page.str()).lexically_normal().generic_string(), section.str()};
    const auto current = inputs->sections.find(key);
    std::optional<Review> review;
    if (current != inputs->sections.end()) {
        if (!current->second.issues.empty()) {
            for (const auto &issue : current->second.issues)
                report(key, issue.line, issue.message);
            return error("resolve links before approving this section");
        }
        review = currentReview(current->second, inputs->snapshot, reason.trim());
    }
    return recordReview(approvalPath(inputs->docs), key, std::move(review));
}

} // namespace c_docc
