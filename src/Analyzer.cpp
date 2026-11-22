#include "Analyzer.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/JSONCompilationDatabase.h"
#include "clang/Tooling/Syntax/Tokens.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/StringSaver.h"

#include <filesystem>
#include <utility>

namespace dagdocdiff {
namespace {

namespace fs = std::filesystem;
using namespace clang;

std::string relativePath(const fs::path &file, const fs::path &root) {
    const auto relative = file.lexically_normal().lexically_relative(root);
    if (relative.empty() || relative == "." || *relative.begin() == "..")
        return {};
    return relative.generic_string();
}

bool nestedInFunction(const Decl &decl) {
    for (auto *scope = decl.getDeclContext(); scope; scope = scope->getParent()) {
        if (scope->isFunctionOrMethod())
            return true;
    }
    return false;
}

class TranslationUnit {
public:
    TranslationUnit(ASTContext &context, const fs::path &root, syntax::TokenBuffer &tokens,
                    Snapshot &snapshot)
        : context(context), tokens(tokens), snapshot(snapshot),
          root_(root), sources_(context.getSourceManager()),
          mainFile_(filePath(sources_.getMainFileID())) {}

    Location location(SourceLocation loc) {
        if (loc.isInvalid())
            return {};
        const auto [file, offset] = sources_.getDecomposedExpansionLoc(loc);
        auto path = filePath(file);
        if (path.empty())
            return {};
        return {std::move(path), sources_.getLineNumber(file, offset),
                sources_.getColumnNumber(file, offset)};
    }

    std::string functionId(const FunctionDecl &decl) const {
        if (decl.isImplicit() || nestedInFunction(decl) || !decl.getDeclName() ||
            decl.getTemplatedKind() != FunctionDecl::TK_NonTemplate ||
            decl.getTrailingRequiresClause() || decl.getOwningModule())
            return {};

        const bool c = !context.getLangOpts().CPlusPlus;
        std::string id = c || decl.isExternC() ? decl.getNameAsString()
                                              : decl.getQualifiedNameAsString();
        if (!c && !decl.isExternC()) {
            PrintingPolicy policy(context.getLangOpts());
            policy.FullyQualifiedName = true;
            llvm::raw_string_ostream out(id);
            out << '(';
            for (unsigned i = 0; i < decl.getNumParams(); ++i) {
                if (i != 0)
                    out << ", ";
                QualType parameter = context.getCanonicalParamType(decl.getParamDecl(i)->getType());
                parameter.print(out, policy);
            }
            if (decl.isVariadic())
                out << (decl.getNumParams() == 0 ? "..." : ", ...");
            out << ')';
            if (const auto *method = dyn_cast<CXXMethodDecl>(&decl)) {
                if (method->isConst())
                    out << " const";
                if (method->isVolatile())
                    out << " volatile";
                if (method->getRefQualifier() == RQ_LValue)
                    out << " &";
                if (method->getRefQualifier() == RQ_RValue)
                    out << " &&";
            }
        }
        return decl.hasExternalFormalLinkage() ? id : mainFile_ + "::" + id;
    }

    std::string recordId(const RecordDecl &decl) const {
        const auto *alias = decl.getTypedefNameForAnonDecl();
        if ((!decl.getIdentifier() && !alias) || decl.isImplicit() ||
            nestedInFunction(decl) || decl.getOwningModule())
            return {};

        if (const auto *record = dyn_cast<CXXRecordDecl>(&decl)) {
            if (record->isDependentContext() || isa<ClassTemplateSpecializationDecl>(record))
                return {};
        }
        const auto &named = alias ? static_cast<const NamedDecl &>(*alias) : decl;
        auto name = named.getQualifiedNameAsString();
        if (!context.getLangOpts().CPlusPlus) {
            // C tags are scoped to a translation unit, including tags from headers.
            return mainFile_ + (decl.isUnion() ? "::union " : "::struct ") + name;
        }
        return named.hasExternalFormalLinkage() ? name : mainFile_ + "::" + name;
    }

    void note(SourceLocation loc, std::string message) {
        auto where = location(loc);
        if (!where.file.empty())
            snapshot.findings.insert({std::move(where), std::move(message)});
    }

    ASTContext &context;
    syntax::TokenBuffer &tokens;
    Snapshot &snapshot;
    std::string mergeError;

private:
    std::string filePath(FileID id) {
        auto [it, inserted] = files_.try_emplace(id.getHashValue());
        if (inserted) {
            if (auto file = sources_.getFileEntryRefForID(id)) {
                const auto path = sources_.getFileManager().getCanonicalName(*file);
                it->second = relativePath(path.str(), root_);
            }
        }
        return it->second;
    }

    const fs::path &root_;
    SourceManager &sources_;
    std::map<unsigned, std::string> files_;
    std::string mainFile_;
};

class Dependencies : public RecursiveASTVisitor<Dependencies> {
public:
    explicit Dependencies(TranslationUnit &unit) : unit_(unit) {}

    bool VisitCallExpr(CallExpr *expr) {
        const auto *callee = expr->getDirectCallee();
        if (!callee) {
            unit_.note(expr->getExprLoc(), "indirect or unresolved call");
            return true;
        }
        addCall(*callee, expr->getExprLoc());
        if (const auto *method = dyn_cast<CXXMethodDecl>(callee); method && method->isVirtual()) {
            const auto *member = dyn_cast<MemberExpr>(expr->getCallee()->IgnoreParenImpCasts());
            if (!member || !member->hasQualifier())
                unit_.note(expr->getExprLoc(), "virtual call tracks only the statically resolved method");
        }
        return true;
    }

    bool VisitCXXConstructExpr(CXXConstructExpr *expr) {
        if (const auto *ctor = expr->getConstructor(); !ctor->isImplicit())
            addCall(*ctor, expr->getExprLoc());
        return true;
    }

    bool VisitCXXDefaultArgExpr(CXXDefaultArgExpr *expr) {
        unit_.note(expr->getUsedLocation(), "default argument value is outside local source tokens");
        return true;
    }

    bool VisitDeclRefExpr(DeclRefExpr *expr) {
        if (const auto *var = dyn_cast<VarDecl>(expr->getDecl());
            var && var->hasGlobalStorage() && !var->isLocalVarDecl() &&
            !unit_.location(var->getLocation()).file.empty()) {
            unit_.note(expr->getExprLoc(), "global variable dependency is outside the symbol graph");
        }
        return true;
    }

    bool VisitExpr(Expr *expr) {
        addType(expr->getType());
        return true;
    }

    bool VisitTypeLoc(TypeLoc loc) {
        addType(loc.getType());
        return true;
    }

    void addType(QualType type, llvm::StringRef kind = "type") {
        if (type.isNull())
            return;
        type = type.getCanonicalType();
        if (const auto *record = type->getAsRecordDecl()) {
            if (!unit_.location(record->getLocation()).file.empty()) {
                auto id = unit_.recordId(*record);
                if (!id.empty())
                    edges.insert({kind.str(), std::move(id)});
            }
        } else if (const auto *pointer = type->getAs<PointerType>()) {
            addType(pointer->getPointeeType(), kind);
        } else if (const auto *reference = type->getAs<ReferenceType>()) {
            addType(reference->getPointeeType(), kind);
        } else if (const auto *array = unit_.context.getAsArrayType(type)) {
            addType(array->getElementType(), kind);
        } else if (const auto *function = type->getAs<FunctionType>()) {
            addType(function->getReturnType(), kind);
            if (const auto *prototype = dyn_cast<FunctionProtoType>(function)) {
                for (auto parameter : prototype->param_types())
                    addType(parameter, kind);
            }
        }
    }

    std::set<Dependency> edges;

private:
    void addCall(const FunctionDecl &decl, SourceLocation loc) {
        if (decl.isImplicit() || decl.getBuiltinID())
            return;
        if (unit_.location(decl.getLocation()).file.empty()) {
            unit_.note(loc, "call target outside the source package");
            return;
        }
        auto id = unit_.functionId(*decl.getCanonicalDecl());
        if (id.empty())
            unit_.note(loc, "unsupported call target: " + decl.getQualifiedNameAsString());
        else
            edges.insert({"call", std::move(id)});
    }

    TranslationUnit &unit_;
};

class Definitions : public RecursiveASTVisitor<Definitions> {
public:
    explicit Definitions(TranslationUnit &unit) : unit_(unit) {}

    bool TraverseDecl(Decl *decl) {
        if (decl && !isa<TranslationUnitDecl>(decl) &&
            unit_.context.getSourceManager().isInSystemHeader(decl->getLocation()))
            return true;
        return RecursiveASTVisitor<Definitions>::TraverseDecl(decl);
    }

    bool VisitFunctionDecl(FunctionDecl *decl) {
        if (decl->isImplicit() || nestedInFunction(*decl) ||
            !decl->isThisDeclarationADefinition() || unit_.location(decl->getLocation()).file.empty())
            return true;
        auto id = unit_.functionId(*decl->getCanonicalDecl());
        if (id.empty()) {
            unit_.note(decl->getLocation(), "unsupported function definition: " +
                                              decl->getQualifiedNameAsString());
            return true;
        }
        Dependencies dependencies(unit_);
        dependencies.TraverseDecl(decl);
        return add(*decl, std::move(id), isa<CXXMethodDecl>(decl) ? "method" : "function",
                   std::move(dependencies.edges));
    }

    bool VisitRecordDecl(RecordDecl *decl) {
        if (decl->isImplicit() || nestedInFunction(*decl) ||
            !decl->isCompleteDefinition() || unit_.location(decl->getLocation()).file.empty())
            return true;
        auto id = unit_.recordId(*decl);
        if (id.empty()) {
            unit_.note(decl->getLocation(), "unsupported record definition");
            return true;
        }
        Dependencies dependencies(unit_);
        if (const auto *cxx = dyn_cast<CXXRecordDecl>(decl)) {
            for (const auto &base : cxx->bases())
                dependencies.addType(base.getType(), "base");
        }
        for (auto *field : decl->fields())
            dependencies.TraverseDecl(field);
        return add(*decl, std::move(id), "record", std::move(dependencies.edges));
    }

private:
    bool add(const NamedDecl &decl, std::string id, std::string kind,
             std::set<Dependency> dependencies) {
        const auto tokens = unit_.tokens.expandedTokens(decl.getSourceRange());
        if (tokens.empty()) {
            unit_.note(decl.getLocation(), "no expanded tokens for " + id);
            return true;
        }
        const auto &sources = unit_.context.getSourceManager();
        Fingerprint hash("dagdocdiff.source.v1");
        for (const auto &token : tokens) {
            hash.add(tok::getTokenName(token.kind()));
            hash.add(token.text(sources));
        }
        auto definition = unit_.location(decl.getLocation());
        Symbol symbol{std::move(kind), std::move(definition), hash.finish(), std::move(dependencies), {}};
        auto [it, inserted] = unit_.snapshot.symbols.try_emplace(id, std::move(symbol));
        if (inserted)
            return true;
        auto &previous = it->second;
        if (previous.localHash != symbol.localHash || previous.kind != symbol.kind ||
            previous.dependencies != symbol.dependencies) {
            unit_.mergeError = "conflicting definitions of " + id + " (" +
                               previous.definition.file + " and " + symbol.definition.file + ")";
            return false;
        }
        if (symbol.definition < previous.definition)
            previous.definition = std::move(symbol.definition);
        return true;
    }

    TranslationUnit &unit_;
};

class Consumer : public ASTConsumer {
public:
    Consumer(CompilerInstance &compiler, const fs::path &root, Snapshot &snapshot, std::string &error)
        : collector_(compiler.getPreprocessor()), root_(root), snapshot_(snapshot), error_(error) {}

    void HandleTranslationUnit(ASTContext &ctx) override {
        if (!error_.empty() || ctx.getDiagnostics().hasErrorOccurred())
            return;
        auto tokens = std::move(collector_).consume();
        tokens.indexExpandedTokens();
        TranslationUnit unit(ctx, root_, tokens, snapshot_);
        Definitions(unit).TraverseDecl(ctx.getTranslationUnitDecl());
        error_ = std::move(unit.mergeError);
    }

private:
    syntax::TokenCollector collector_;
    const fs::path &root_;
    Snapshot &snapshot_;
    std::string &error_;
};

class Extractor : public tooling::FrontendActionFactory {
public:
    Extractor(const fs::path &root, Snapshot &snapshot) : root_(root), snapshot_(snapshot) {}

    std::unique_ptr<FrontendAction> create() override {
        class Action : public ASTFrontendAction {
        public:
            explicit Action(Extractor &extractor) : extractor_(extractor) {}

            std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &compiler,
                                                           llvm::StringRef) override {
                return std::make_unique<Consumer>(compiler, extractor_.root_, extractor_.snapshot_,
                                                  extractor_.error);
            }

        private:
            Extractor &extractor_;
        };
        return std::make_unique<Action>(*this);
    }

    std::string error;

private:
    const fs::path &root_;
    Snapshot &snapshot_;
};

tooling::CommandLineArguments compilerDefaults(const tooling::CommandLineArguments &args,
                                               llvm::StringRef) {
    tooling::CommandLineArguments adjusted{args.front(), "-resource-dir=" DAGDOCDIFF_RESOURCE_DIR};
    if (llvm::StringRef(args.front()).starts_with("/nix/store/")) {
        // LibTooling opens an in-process Clang driver, so the Nix wrapper's
        // shell script does not add its standard library flags.
        const auto support = fs::path(args.front()).parent_path().parent_path() / "nix-support";
        auto appendFlags = [&](const char *name) {
            auto file = llvm::MemoryBuffer::getFile((support / name).string());
            if (!file)
                return;
            llvm::BumpPtrAllocator allocator;
            llvm::StringSaver saver(allocator);
            llvm::SmallVector<const char *, 16> flags;
            llvm::cl::TokenizeGNUCommandLine((*file)->getBuffer(), saver, flags);
            adjusted.insert(adjusted.end(), flags.begin(), flags.end());
        };
        appendFlags("cc-cflags-before");
        appendFlags("cc-cflags");
        if (!llvm::is_contained(args, "-nostdinc")) {
            appendFlags("libc-cflags");
            if (!llvm::is_contained(args, "-nostdinc++"))
                appendFlags("libcxx-cxxflags");
        }
    }
    // Place explicit options later so they can override the defaults.
    adjusted.insert(adjusted.end(), args.begin() + 1, args.end());
    return adjusted;
}

llvm::Error failure(llvm::Twine message) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(), message);
}

} // namespace

llvm::Expected<Snapshot> analyze(llvm::StringRef sourceRoot, llvm::StringRef buildDirectory) {
    std::error_code ec;
    const auto root = fs::canonical(sourceRoot.str(), ec);
    if (ec || !fs::is_directory(root, ec))
        return failure("cannot open source root " + sourceRoot);
    const auto databasePath = fs::path(buildDirectory.str()) / "compile_commands.json";
    std::string error;
    auto database = tooling::JSONCompilationDatabase::loadFromFile(
        databasePath.string(), error, tooling::JSONCommandLineSyntax::AutoDetect);
    if (!database)
        return failure(llvm::Twine(databasePath.string()) + ": " + error);
    std::vector<std::string> sources;
    for (const auto &file : database->getAllFiles()) {
        const auto path = fs::weakly_canonical(file, ec);
        if (ec || relativePath(path, root).empty())
            continue;
        if (database->getCompileCommands(file).size() != 1)
            return failure("expected one compile command for " + llvm::Twine(file));
        sources.push_back(file);
    }
    if (sources.empty())
        return failure("compilation database contains no inputs in " + sourceRoot);
    llvm::sort(sources);

    tooling::ClangTool tool(*database, sources);
    tool.appendArgumentsAdjuster(compilerDefaults);

    Snapshot snapshot;
    Extractor extractor(root, snapshot);
    if (tool.run(&extractor) != 0)
        return failure("analysis failed for " + sourceRoot);
    if (!extractor.error.empty())
        return failure(extractor.error);
    hashComponents(snapshot);
    return snapshot;
}

} // namespace dagdocdiff
