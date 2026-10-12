// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// CLI-only token adaptation, followed by Clang's actual parser and Sema.
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/TypeLoc.h>
#include <clang/AST/Mangle.h>
#include <clang/AST/RecordLayout.h>
#include <clang/AST/VTableBuilder.h>
#include <clang/Basic/Version.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/Pragma.h>
#include <clang/Sema/Sema.h>
#include <llvm/Support/ConvertUTF.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/VirtualFileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace clang;
namespace clang { void cppcliParseAST(Sema&, bool, bool); }
namespace {
std::string annotation(const Decl* d, llvm::StringRef prefix) {
    for (auto* a : d->specific_attrs<AnnotateAttr>())
        if (a->getAnnotation().starts_with(prefix)) return a->getAnnotation().drop_front(prefix.size()).str();
    return {};
}
bool annotated(const Decl* d, llvm::StringRef text) {
    for (auto* a : d->specific_attrs<AnnotateAttr>()) if (a->getAnnotation() == text) return true;
    return false;
}
bool record_annotated(const CXXRecordDecl* r, llvm::StringRef name) {
    if (!r) return false;
    if (annotated(r, name) || (r->getDefinition() && annotated(r->getDefinition(), name))) return true;
    if (auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(r)) {
        auto* primary = spec->getSpecializedTemplate()->getTemplatedDecl();
        return annotated(primary, name) || (primary->getDefinition() && annotated(primary->getDefinition(), name));
    }
    return false;
}
bool is_interface(const CXXRecordDecl* r) {
    return record_annotated(r, "cli.interface");
}
std::string record_annotation(const CXXRecordDecl* r, llvm::StringRef prefix) {
    if (!r) return {};
    auto value = annotation(r, prefix);
    if (value.empty() && r->getDefinition()) value = annotation(r->getDefinition(), prefix);
    if (value.empty()) if (auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(r)) {
        auto* primary = spec->getSpecializedTemplate()->getTemplatedDecl();
        value = annotation(primary, prefix);
        if (value.empty() && primary->getDefinition()) value = annotation(primary->getDefinition(), prefix);
    }
    return value;
}
const CXXRecordDecl* class_base(const CXXRecordDecl* r) {
    for (auto& b : r->bases()) {
        auto* parent = b.getType()->getAsCXXRecordDecl();
        if (!is_interface(parent)) return parent;
    }
    return nullptr;
}
std::string quote(const std::string& s) {
    std::string result = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else result += c;
    }
    return result + "\"";
}
std::string assembly_reference(const std::string& name) {
    // A single identifier can be an IL keyword (for example an assembly named
    // 'switch'). Dotted framework names remain unambiguous unquoted scopes.
    if (name.find('.') != std::string::npos && name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.") == std::string::npos) return "[" + name + "]";
    std::string escaped;
    for (char c : name) { if (c == '\\' || c == '\'') escaped += '\\'; escaped += c; }
    return "['" + escaped + "']";
}
std::string read_file(const std::string& path) {
    auto buffer = llvm::MemoryBuffer::getFile(path);
    if (!buffer) throw std::runtime_error("cannot read " + path);
    return (*buffer)->getBuffer().str();
}
void write_file(const std::string& path, const std::string& text) {
    std::ofstream out(std::filesystem::u8path(path), std::ios::binary);
    out << text;
    out.close();
    if (!out) throw std::runtime_error("cannot write " + path);
}
struct ImportedMethod {
    std::string assembly, owner, name, result;
    std::vector<std::string> parameters;
    bool is_static;
    bool is_abstract;
};
struct ImportedField { std::string assembly, owner, name, type, constant; };
struct Metadata {
    std::map<std::string, ImportedMethod> methods;
    std::map<std::string, ImportedField> fields;
    std::map<std::string, std::string> types;
    std::set<std::string> short_types, interface_types;
    std::string externs;
    void load(const std::string& path) {
        auto value = llvm::json::parse(read_file(path));
        if (!value) throw std::runtime_error("invalid metadata manifest");
        auto* root = value->getAsObject();
        if (!root || root->getInteger("version") != 1) throw std::runtime_error("unsupported metadata manifest version");
        auto* assemblies = root->getArray("assemblies");
        auto* classes = root->getArray("types");
        if (!assemblies || !classes) throw std::runtime_error("incomplete metadata manifest");
        for (auto& entry : *assemblies) {
            auto& a = *entry.getAsObject();
            std::string name = a.getString("Name")->str(), token = a.getString("Token")->str();
            std::string ver = a.getString("Version")->str();
            std::replace(ver.begin(), ver.end(), '.', ':');
            externs += ".assembly extern '" + name + "' { .ver " + ver;
            if (!token.empty()) {
                externs += " .publickeytoken = (";
                for (size_t i = 0; i < token.size(); i += 2) externs += token.substr(i, 2) + " ";
                externs += ")";
            }
            externs += " }\n";
        }
        for (auto& entry : *classes) {
            auto& c = *entry.getAsObject();
            std::string name = c.getString("Name")->str(), assembly = c.getString("Assembly")->str();
            types[name] = assembly;
            short_types.insert(name.substr(name.find_last_of('.') + 1));
            if (c.getString("Kind").value_or("") == "interface") {
                std::string cpp_name = name;
                for (size_t pos = 0; (pos = cpp_name.find('.', pos)) != std::string::npos; pos += 2) cpp_name.replace(pos, 1, "::");
                auto generic = cpp_name.find('`');
                if (generic != std::string::npos) cpp_name.erase(generic);
                interface_types.insert(cpp_name);
                interface_types.insert(cpp_name.substr(cpp_name.find_last_of(':') + 1));
            }
            for (auto& method : *c.getArray("Methods")) {
                auto& m = *method.getAsObject();
                if (!m.getBoolean("Supported").value_or(false)) continue;
                ImportedMethod im{assembly, name, m.getString("Name")->str(),
                    m.getObject("Return")->getString("Il")->str(), {}, m.getBoolean("Static").value_or(false), m.getBoolean("Abstract").value_or(false)};
                for (auto& p : *m.getArray("Parameters")) im.parameters.push_back(p.getAsObject()->getString("Il")->str());
                methods.emplace(m.getString("Id")->str(), std::move(im));
            }
            if (auto* fields_list = c.getArray("Fields")) for (auto& field : *fields_list) {
                auto& f = *field.getAsObject();
                if (!f.getBoolean("Supported").value_or(false)) continue;
                auto* constant = f.getObject("Constant");
                fields.emplace(f.getString("Id")->str(), ImportedField{assembly, name, f.getString("Name")->str(), f.getObject("Type")->getString("Il")->str(), constant ? constant->getString("Il")->str() : ""});
            }
        }
    }
};

struct Options {
    std::string metadata, output, evidence;
    std::string assembly = "cli_program", target = "exe", native_library, native_imports, public_key;
    bool parse(const CompilerInstance& ci, const std::vector<std::string>& args, bool require_metadata = true) {
        for (const auto& arg : args) {
            auto eq = arg.find('=');
            if (eq == std::string::npos) return false;
            std::string key = arg.substr(0, eq), value = arg.substr(eq + 1);
            if (key == "metadata") metadata = value;
            else if (key == "output") output = value;
            else if (key == "evidence") evidence = value;
            else if (key == "assembly") assembly = value;
            else if (key == "target") target = value;
            else if (key == "native-library") native_library = value;
            else if (key == "native-imports") native_imports = value;
            else if (key == "public-key") public_key = value;
            else return false;
        }
        return (!require_metadata || !metadata.empty()) && !output.empty() && (target == "exe" || target == "library");
    }
};
void report(CompilerInstance& ci, SourceLocation loc, const std::string& message) {
    unsigned id = ci.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, "C++/CLI: %0");
    ci.getDiagnostics().Report(loc, id) << message;
}

struct Lexeme { std::string text, file; unsigned line; tok::TokenKind kind; bool system; };
void append_fragment(std::vector<Lexeme>& destination, const std::string& source, const Lexeme& anchor, SourceManager& sm, const LangOptions& lang) {
    auto file = sm.createFileID(llvm::MemoryBuffer::getMemBufferCopy(source + "\n", "<cli-fragment>"));
    auto data = sm.getBufferData(file);
    Lexer lexer(sm.getLocForStartOfFile(file), lang, data.begin(), data.begin(), data.end());
    Token token;
    while (!lexer.LexFromRawLexer(token)) {
        auto lexeme = anchor;
        lexeme.text = Lexer::getSpelling(token, sm, lang);
        lexeme.kind = token.is(tok::raw_identifier) ? tok::identifier : token.getKind();
        destination.push_back(std::move(lexeme));
    }
}
// Raw lexing protects CLI keywords and directives. Clang's preprocessor owns
// include lookup, macro expansion, conditional directives and pragma-once IDs.
std::string shim_using(SourceManager& sm, FileID file, const LangOptions& lang) {
    auto buffer = sm.getBufferData(file); auto start = sm.getLocForStartOfFile(file);
    Lexer lexer(start, lang, buffer.begin(), buffer.begin(), buffer.end());
    struct Replacement { unsigned offset, length; std::string text; };
    std::vector<Replacement> replacements;
    std::vector<Token> tokens;
    Token token;
    while (!lexer.LexFromRawLexer(token)) if (!token.is(tok::comment)) tokens.push_back(token);
    bool directive = false;
    for (size_t i = 0; i < tokens.size(); ++i) {
        auto& token = tokens[i];
        if (token.isAtStartOfLine()) directive = token.is(tok::hash);
        if (token.is(tok::hash) && token.isAtStartOfLine() && i + 1 < tokens.size()) {
            auto& name = tokens[i + 1];
            if (!name.isAtStartOfLine() && Lexer::getSpelling(name, sm, lang) == "using")
                replacements.push_back({sm.getFileOffset(name.getLocation()), name.getLength(), "pragma cli_reference"});
        }
        // Windows headers define interface as struct for native COM. Protect
        // only the two CLI declaration forms; native uses still expand normally.
        if (!directive && i + 1 < tokens.size() && Lexer::getSpelling(token, sm, lang) == "interface") {
            auto next = Lexer::getSpelling(tokens[i + 1], sm, lang);
            if (next == "class" || next == "struct")
                replacements.push_back({sm.getFileOffset(token.getLocation()), token.getLength(), "__cli_interface"});
        }
    }
    std::string text = buffer.str();
    for (auto it = replacements.rbegin(); it != replacements.rend(); ++it)
        text.replace(it->offset, it->length, it->text);
    return text;
}
class UsingFile : public llvm::vfs::File {
    llvm::vfs::Status info;
    std::string contents;
public:
    UsingFile(llvm::vfs::Status status, std::string text) :
        info(llvm::vfs::Status::copyWithNewSize(status, text.size())), contents(std::move(text)) {}
    llvm::ErrorOr<llvm::vfs::Status> status() override { return info; }
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> getBuffer(const llvm::Twine& name, int64_t, bool, bool) override {
        return llvm::MemoryBuffer::getMemBufferCopy(contents, name.str());
    }
    std::error_code close() override { return {}; }
};
class UsingFileSystem : public llvm::vfs::ProxyFileSystem {
    CompilerInstance& ci;
    std::map<std::string, std::string> cache;
    std::set<std::string> unchanged;
    const std::string* adapted(const llvm::Twine& path, const llvm::vfs::Status& info) {
        if (!info.isRegularFile()) return nullptr;
        auto key = path.str();
        if (auto found = cache.find(key); found != cache.end()) return &found->second;
        if (unchanged.count(key)) return nullptr;
        unchanged.insert(key);
        auto buffer = getUnderlyingFS().getBufferForFile(path);
        if (!buffer) return nullptr;
        auto bytes = (*buffer)->getBuffer();
        if (bytes.find("using") == llvm::StringRef::npos && bytes.find("interface") == llvm::StringRef::npos && bytes.find('\\') == llvm::StringRef::npos) return nullptr;
        // A separate source manager avoids changing the real translation unit's
        // include ancestry and file IDs while raw lexing the intercepted buffer.
        DiagnosticsEngine diagnostics(ci.getDiagnostics().getDiagnosticIDs(), ci.getDiagnosticOpts());
        SourceManager sm(diagnostics, ci.getFileManager());
        auto file = sm.createFileID(std::move(*buffer));
        auto original = sm.getBufferData(file);
        auto text = shim_using(sm, file, ci.getLangOpts());
        if (text == original) return nullptr;
        return &cache.emplace(std::move(key), std::move(text)).first->second;
    }
public:
    explicit UsingFileSystem(CompilerInstance& compiler) :
        ProxyFileSystem(compiler.getFileManager().getVirtualFileSystemPtr()), ci(compiler) {}
    llvm::ErrorOr<llvm::vfs::Status> status(const llvm::Twine& path) override {
        auto info = getUnderlyingFS().status(path);
        if (info) if (auto* text = adapted(path, *info)) return llvm::vfs::Status::copyWithNewSize(*info, text->size());
        return info;
    }
    llvm::ErrorOr<std::unique_ptr<llvm::vfs::File>> openFileForRead(const llvm::Twine& path) override {
        auto info = getUnderlyingFS().status(path);
        if (info) if (auto* text = adapted(path, *info))
            return std::unique_ptr<llvm::vfs::File>(new UsingFile(*info, *text));
        return getUnderlyingFS().openFileForRead(path);
    }
};
void enable_header_using(CompilerInstance& ci) {
    ci.getFileManager().setVirtualFileSystem(llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem>(new UsingFileSystem(ci)));
}
class ReferencePragma : public PragmaHandler {
    llvm::json::Array& references;
public:
    explicit ReferencePragma(llvm::json::Array& result) : PragmaHandler("cli_reference"), references(result) {}
    void HandlePragma(Preprocessor& pp, PragmaIntroducer, Token& first) override {
        Token path; pp.LexHeaderName(path);
        std::string text = pp.getSpelling(path);
        if (text.size() < 2 || (text.front() != '"' && text.front() != '<')) {
            unsigned id = pp.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, "C++/CLI: #using requires a quoted or angle-bracket assembly path");
            pp.getDiagnostics().Report(path.getLocation(), id); return;
        }
        auto loc = pp.getSourceManager().getPresumedLoc(first.getLocation());
        Token suffix; pp.Lex(suffix);
        bool as_friend = suffix.is(tok::identifier) && pp.getSpelling(suffix) == "as_friend";
        if (as_friend) pp.Lex(suffix);
        if (!suffix.is(tok::eod)) {
            unsigned id = pp.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, "C++/CLI: unexpected tokens after #using assembly path");
            pp.getDiagnostics().Report(suffix.getLocation(), id); return;
        }
        references.push_back(llvm::json::Object{{"path", text.substr(1, text.size() - 2)},
            {"source", loc.isValid() ? loc.getFilename() : ""}, {"as_friend", as_friend}});
    }
};
// This preprocessing action has no Parser/Sema pragma handlers. Carry pack
// directives through flattening so the real parser owns layout and pack stacks.
class PreservePackPragma : public PragmaHandler {
    std::vector<Lexeme>& tokens;
public:
    explicit PreservePackPragma(std::vector<Lexeme>& result) : PragmaHandler("pack"), tokens(result) {}
    void HandlePragma(Preprocessor& pp, PragmaIntroducer, Token& first) override {
        auto& sm = pp.getSourceManager();
        auto location = sm.getPresumedLoc(first.getLocation());
        if (!location.isValid()) throw std::runtime_error("pack pragma has no source location");
        std::string text = "pack";
        Token token;
        for (;;) {
            pp.Lex(token);
            if (token.is(tok::eod) || token.is(tok::eof)) break;
            text += " " + pp.getSpelling(token);
        }
        tokens.push_back({"_Pragma(" + quote(text) + ")", location.getFilename(), location.getLine(), tok::identifier,
                          sm.isInSystemHeader(first.getLocation())});
    }
};
class ShimAction : public PluginASTAction {
    Options options;
protected:
    bool ParseArgs(const CompilerInstance& ci, const std::vector<std::string>& args) override { return options.parse(ci, args, false); }
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance&, llvm::StringRef) override { return std::make_unique<ASTConsumer>(); }
    void ExecuteAction() override {
        auto& ci = getCompilerInstance();
        try {
            auto& sm = ci.getSourceManager(); auto file = sm.getMainFileID();
            auto start = sm.getLocForStartOfFile(file);
            std::string text = shim_using(sm, file, ci.getLangOpts());
            text = "#line 1 " + quote(sm.getFilename(start).str()) + "\n" + text;
            write_file(options.output, text);
        } catch (const std::exception& e) { report(ci, {}, e.what()); }
    }
};
class ReferencesAction : public PluginASTAction {
    Options options;
protected:
    bool ParseArgs(const CompilerInstance& ci, const std::vector<std::string>& args) override { return options.parse(ci, args, false); }
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance&, llvm::StringRef) override { return std::make_unique<ASTConsumer>(); }
    void ExecuteAction() override {
        auto& ci = getCompilerInstance();
        try {
            auto& pp = ci.getPreprocessor(); llvm::json::Array references;
            enable_header_using(ci);
            pp.AddPragmaHandler(new ReferencePragma(references));
            pp.EnterMainSourceFile(); Token token;
            do { pp.Lex(token); } while (!token.is(tok::eof));
            if (ci.getDiagnostics().hasErrorOccurred()) return;
            std::string text; llvm::raw_string_ostream stream(text); stream << llvm::json::Value(std::move(references));
            write_file(options.output, text + "\n");
        } catch (const std::exception& e) { report(ci, {}, e.what()); }
    }
};
class AdaptAction : public PluginASTAction {
    Options options;
protected:
    bool ParseArgs(const CompilerInstance& ci, const std::vector<std::string>& args) override { return options.parse(ci, args); }
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance&, llvm::StringRef) override { return std::make_unique<ASTConsumer>(); }
    void ExecuteAction() override {
        auto& ci = getCompilerInstance();
        try {
            Metadata metadata;
            metadata.load(options.metadata);
            auto& pp = ci.getPreprocessor();
            enable_header_using(ci);
            llvm::json::Array ignored_references;
            pp.AddPragmaHandler(new ReferencePragma(ignored_references));
            auto& sm = ci.getSourceManager();
            std::vector<Lexeme> tokens;
            pp.AddPragmaHandler(new PreservePackPragma(tokens));
            pp.EnterMainSourceFile();
            Token token;
            for (;;) {
                pp.Lex(token);
                if (token.is(tok::eof)) break;
                auto loc = sm.getPresumedLoc(token.getLocation());
                if (!loc.isValid()) throw std::runtime_error("token has no source location");
                auto spelling = pp.getSpelling(token);
                if (spelling == "__cli_interface") spelling = "interface";
                tokens.push_back({spelling, loc.getFilename(), loc.getLine(), token.getKind(), sm.isInSystemHeader(token.getLocation())});
            }
            if (ci.getDiagnostics().hasErrorOccurred()) return;
            // Lower only the CLI loop boundary. Collection expressions, element
            // declarations and loop bodies are still parsed and checked by Clang.
            auto balance = [&](size_t begin, llvm::StringRef open, llvm::StringRef close) {
                int depth = 0;
                for (size_t end = begin; end < tokens.size(); ++end) {
                    if (tokens[end].text == open) ++depth;
                    if (tokens[end].text == close && --depth == 0) return end;
                }
                throw std::runtime_error("unterminated for each statement");
            };
            unsigned each_id = 0;
            for (size_t pos = tokens.size(); pos-- > 0;) {
                if (tokens[pos].text != "for" || pos+2 >= tokens.size() || tokens[pos+1].text != "each" || tokens[pos+2].text != "(") continue;
                size_t close = balance(pos+2, "(", ")"), in = pos+3;
                while (in < close && tokens[in].text != "in") ++in;
                if (in < pos+5 || in+1 == close || tokens[in-1].kind != tok::identifier)
                    throw std::runtime_error("for each requires an element declaration and a collection expression");
                for (size_t j = pos+3; j < in-1; ++j) if (tokens[j].text == "%" || tokens[j].text == "&")
                    throw std::runtime_error("tracking-reference for each variables are not enabled yet");
                if (close+1 >= tokens.size() || tokens[close+1].text != "{")
                    throw std::runtime_error("for each currently requires a braced body");
                size_t end = balance(close+1, "{", "}");
                auto anchor = tokens[pos];
                std::vector<Lexeme> replacement;
                auto append = [&](const std::string& source) {
                    append_fragment(replacement, source, anchor, sm, ci.getLangOpts());
                };
                auto append_tokens = [&](size_t first, size_t last) { replacement.insert(replacement.end(), tokens.begin()+first, tokens.begin()+last); };
                auto enumerator = "__cli_each_enumerator_" + std::to_string(each_id);
                auto index = "__cli_each_index_" + std::to_string(each_id++);
                append("{ auto " + enumerator + " = __cli_each_get((");
                append_tokens(in+1, close);
                append(")); int " + index + " = -1; try { while (__cli_each_next(" + enumerator + ", " + index + ")) {");
                append_tokens(pos+3, in);
                append("= __cli_safe_cast<"); append_tokens(pos+3, in-1);
                append(">(__cli_each_current(" + enumerator + ", " + index + "));");
                append_tokens(close+1, end+1);
                append("} } finally { __cli_each_dispose(" + enumerator + "); } }");
                tokens.erase(tokens.begin()+pos, tokens.begin()+end+1);
                tokens.insert(tokens.begin()+pos, replacement.begin(), replacement.end());
            }
            // Fundamental keyword scopes have no identifier to shadow. Map
            // their native target widths to the corresponding selected CLR
            // primitive alias, leaving ordinary declarations unchanged.
            const std::set<std::string> keyword_types = {"signed", "unsigned", "short", "long", "int", "char", "wchar_t", "bool", "float", "double"};
            for (size_t pos = 0; pos < tokens.size(); ++pos) {
                if (!keyword_types.count(tokens[pos].text) || (pos && keyword_types.count(tokens[pos-1].text))) continue;
                size_t end = pos; std::string spelling;
                while (end < tokens.size() && keyword_types.count(tokens[end].text)) { if (!spelling.empty()) spelling += " "; spelling += tokens[end++].text; }
                if (end >= tokens.size() || tokens[end].text != "::") continue;
                std::string managed;
                bool is_unsigned = spelling.rfind("unsigned", 0) == 0;
                auto plain = spelling;
                if (plain.rfind("unsigned ", 0) == 0) plain.erase(0, 9);
                else if (plain.rfind("signed ", 0) == 0) plain.erase(0, 7);
                if ((is_unsigned || spelling.rfind("signed ", 0) == 0) &&
                    plain != "char" && plain != "short" && plain != "short int" && plain != "int" &&
                    plain != "long" && plain != "long int" && plain != "long long" && plain != "long long int") continue;
                if (plain == "long" || plain == "long int") managed = ci.getTarget().getLongWidth() == 64 ? "Int64" : "Int32";
                else if (plain == "long long" || plain == "long long int") managed = "Int64";
                else if (plain == "int" || plain == "signed" || plain == "unsigned") managed = "Int32";
                else if (plain == "short" || plain == "short int") managed = "Int16";
                else if (plain == "char") managed = is_unsigned ? "Byte" : spelling.rfind("signed ", 0) == 0 || ci.getLangOpts().CharIsSigned ? "SByte" : "Byte";
                else if (plain == "wchar_t") managed = ci.getTarget().getWCharWidth() == 16 ? "Char" : "Int32";
                else if (plain == "bool") managed = "Boolean";
                else if (plain == "float") managed = "Single";
                else if (plain == "double") managed = "Double";
                if (is_unsigned && managed.rfind("Int", 0) == 0) managed.insert(0, "U");
                else if (is_unsigned && managed == "SByte") managed = "Byte";
                if (managed.empty()) continue;
                std::vector<Lexeme> replacement;
                append_fragment(replacement, "::System::" + managed, tokens[pos], sm, ci.getLangOpts());
                tokens.erase(tokens.begin()+pos, tokens.begin()+end);
                tokens.insert(tokens.begin()+pos, replacement.begin(), replacement.end());
                pos += replacement.size()-1;
            }
            for (size_t pos = tokens.size(); pos-- > 0;) {
                if (tokens[pos].text != "delegate") continue;
                size_t open = pos+1;
                while (open < tokens.size() && tokens[open].text != "(" && tokens[open].text != ";" && tokens[open].text != "{") ++open;
                if (open < pos+3 || open == tokens.size() || tokens[open].text != "(" || tokens[open-1].kind != tok::identifier)
                    throw std::runtime_error("delegate requires an ordinary function declaration");
                auto close = balance(open, "(", ")");
                if (close+1 >= tokens.size() || tokens[close+1].text != ";") throw std::runtime_error("delegate declarations require a semicolon");
                auto name = tokens[open-1].text;
                auto anchor = tokens[pos];
                std::string result, parameters;
                for (size_t j = pos+1; j+1 < open; ++j) result += tokens[j].text + " ";
                for (size_t j = open+1; j < close; ++j) parameters += tokens[j].text + " ";
                size_t begin = pos;
                std::string visibility = "private";
                if (pos && (tokens[pos-1].text == "public" || tokens[pos-1].text == "private")) { visibility = tokens[pos-1].text; --begin; }
                std::vector<Lexeme> replacement;
                append_fragment(replacement, visibility + " ref class " + name + " __cli_delegate sealed : System::MulticastDelegate { public: " +
                    "using _cli_signature = " + result + "(" + parameters + "); " + name + "(_cli_signature*); " +
                    "template<class T, class C> " + name + "(T*, " + result + " (C::*)(" + parameters + ")); " +
                    result + " Invoke(" + parameters + "); };", anchor, sm, ci.getLangOpts());
                tokens.erase(tokens.begin()+begin, tokens.begin()+close+2);
                tokens.insert(tokens.begin()+begin, replacement.begin(), replacement.end());
                pos = begin;
            }
            // ::typeid is a CLI type operand, unlike native typeid(expr).
            // Rewrite only its qualified type spelling; Clang resolves T.
            for (size_t pos = tokens.size(); pos-- > 0;) {
                if (tokens[pos].text != "typeid" || pos < 2 || tokens[pos-1].text != "::") continue;
                auto atom_begin = [&](size_t end) {
                    if (tokens[end].text != ">" && tokens[end].text != ">>") return end;
                    int angles = 0;
                    for (size_t begin = end+1; begin-- > 0;) {
                        if (tokens[begin].text == ">") ++angles;
                        else if (tokens[begin].text == ">>") angles += 2;
                        else if (tokens[begin].text == "<") --angles;
                        if (!angles) {
                            if (!begin || tokens[begin-1].kind != tok::identifier) throw std::runtime_error("unsupported managed typeid operand");
                            return begin-1;
                        }
                    }
                    throw std::runtime_error("unterminated managed typeid template operand");
                };
                size_t begin = atom_begin(pos-2);
                while (begin >= 2 && tokens[begin-1].text == "::" && (tokens[begin-2].kind == tok::identifier || tokens[begin-2].text == ">" || tokens[begin-2].text == ">>"))
                    begin = atom_begin(begin-2);
                if (begin && tokens[begin-1].text == "::") --begin;
                static const std::set<std::string> builtin = {"unsigned", "signed", "long", "short", "int", "char", "double"};
                if (builtin.count(tokens[begin].text)) while (begin && builtin.count(tokens[begin-1].text)) --begin;
                std::vector<Lexeme> replacement;
                append_fragment(replacement, "__cli_typeid<", tokens[pos], sm, ci.getLangOpts());
                replacement.insert(replacement.end(), tokens.begin()+begin, tokens.begin()+pos-1);
                append_fragment(replacement, ">()", tokens[pos], sm, ci.getLangOpts());
                tokens.erase(tokens.begin()+begin, tokens.begin()+pos+1);
                tokens.insert(tokens.begin()+begin, replacement.begin(), replacement.end());
                pos = begin;
            }
            // Only managed declarations and allocation markers are rewritten.
            // Expressions/statements remain C++ and are parsed by Clang later.
            for (size_t i = 0; i + 2 < tokens.size(); ++i)
                if ((tokens[i].text == "ref" || tokens[i].text == "interface" || tokens[i].text == "value") && (tokens[i + 1].text == "class" || tokens[i + 1].text == "struct")) {
                    metadata.short_types.insert(tokens[i + 2].text);
                    if (tokens[i].text == "interface") metadata.interface_types.insert(tokens[i + 2].text);
                }
            std::ostringstream out;
            std::string file;
            unsigned line = 0;
            bool system = false;
            auto emit = [&](const Lexeme& at, const std::string& text) {
                if (file != at.file || line != at.line || system != at.system) {
                    out << "\n# " << at.line << " " << quote(at.file) << (at.system ? " 3" : "") << "\n";
                    file = at.file; line = at.line; system = at.system;
                }
                out << text << " ";
            };
            std::set<std::string> managed_string_calls;
            std::map<std::string, std::set<unsigned>> managed_string_constructors;
            for (const auto& entry : metadata.methods) {
                auto& m = entry.second;
                if (std::find(m.parameters.begin(), m.parameters.end(), "string") != m.parameters.end()) {
                    auto owner = m.owner.substr(m.owner.find_last_of('.') + 1);
                    if (m.name == ".ctor") {
                        for (unsigned index = 0; index < m.parameters.size(); ++index)
                            if (m.parameters[index] == "string") managed_string_constructors[owner.substr(0, owner.find('`'))].insert(index);
                    } else managed_string_calls.insert(owner + "::" + m.name);
                }
            }
            // Find only CLI class/member declaration boundaries. Clang still
            // parses parameter expressions, default arguments and method bodies.
            std::vector<std::string> class_context(tokens.size()), class_access(tokens.size());
            std::vector<unsigned> class_depth(tokens.size());
            std::vector<bool> class_interface(tokens.size(), false);
            std::set<size_t> member_starts;
            std::set<size_t> interface_virtual, interface_pure;
            std::map<size_t, std::string> value_defaults;
            for (size_t i = 0; i + 2 < tokens.size(); ++i) {
                if ((tokens[i].text != "ref" && tokens[i].text != "interface" && tokens[i].text != "value") || (tokens[i + 1].text != "class" && tokens[i + 1].text != "struct")) continue;
                std::string name = tokens[i + 2].text;
                size_t open = i + 3;
                while (open < tokens.size() && tokens[open].text != "{" && tokens[open].text != ";") ++open;
                if (open == tokens.size() || tokens[open].text != "{") continue;
                if (tokens[i].text == "value") value_defaults[open] = "public: __attribute__((annotate(\"cli.value.zero\"))) " + name + "() {} __attribute__((annotate(\"cli.box\"))) operator " + name + "*() const; __attribute__((annotate(\"cli.box.object\"))) operator ::System::Object*() const; " + (tokens[i+1].text == "struct" ? "public:" : "private:");
                int depth = 1;
                bool member_begin = true;
                std::string access = tokens[i].text == "interface" || tokens[i + 1].text == "struct" ? "public" : "private";
                for (size_t j = open + 1; j < tokens.size() && depth; ++j) {
                    bool access_label = depth == 1 && j + 1 < tokens.size() && tokens[j + 1].text == ":" &&
                        (tokens[j].text == "public" || tokens[j].text == "private" || tokens[j].text == "protected" || tokens[j].text == "internal");
                    if (access_label) { access = tokens[j].text; member_begin = false; }
                    else if (depth == 1 && tokens[j].text == ":" && j > open + 1 &&
                        (tokens[j-1].text == "public" || tokens[j-1].text == "private" || tokens[j-1].text == "protected" || tokens[j-1].text == "internal")) member_begin = true;
                    else if (depth == 1 && member_begin && tokens[j].text != "}" && tokens[j].text != ";") { member_starts.insert(j); member_begin = false; }
                    class_context[j] = name; class_access[j] = access;
                    class_interface[j] = tokens[i].text == "interface";
                    class_depth[j] = depth;
                    if (tokens[j].text == "{") ++depth;
                    else if (tokens[j].text == "}") { --depth; if (depth == 1) member_begin = true; }
                    else if (depth == 1 && tokens[j].text == ";") member_begin = true;
                    else if (depth == 1 && tokens[j].text == "[") {
                        int brackets = 1; size_t end = j+1;
                        for (; end < tokens.size() && brackets; ++end) {
                            if (tokens[end].text == "[") ++brackets;
                            else if (tokens[end].text == "]") --brackets;
                            class_context[end] = name; class_access[end] = access;
                            class_depth[end] = 1; class_interface[end] = tokens[i].text == "interface";
                        }
                        if (brackets) throw std::runtime_error("unterminated managed member attribute or array bound");
                        j = end-1;
                    }
                    else if (depth == 1 && tokens[j].text == "(" && j && tokens[j - 1].kind == tok::identifier) {
                        size_t end = j + 1; int parentheses = 1, parameter_angles = 0; bool string_parameter = false;
                        unsigned parameter_index = 0;
                        std::set<unsigned> string_parameters;
                        while (end < tokens.size() && parentheses) {
                            if (tokens[end].text == "(") ++parentheses;
                            if (tokens[end].text == ")") --parentheses;
                            if (parentheses == 1) {
                                if (tokens[end].text == "<") ++parameter_angles;
                                else if (tokens[end].text == ">") --parameter_angles;
                                else if (tokens[end].text == ">>") parameter_angles -= 2;
                                else if (!parameter_angles && tokens[end].text == ",") ++parameter_index;
                                else if (!parameter_angles && tokens[end].text == "String" && end+1 < tokens.size() && tokens[end+1].text == "^") {
                                    string_parameter = true; string_parameters.insert(parameter_index);
                                }
                            }
                            ++end;
                        }
                        if (tokens[i].text == "value" && tokens[j-1].text == name && end == j+2 &&
                            (j < 2 || (tokens[j-2].text != "static" && tokens[j-2].text != "~")))
                            throw std::runtime_error("user-defined zero-argument value constructors are not supported; default storage is zero initialized");
                        if (string_parameter) {
                            if (tokens[j-1].text == name) managed_string_constructors[name].insert(string_parameters.begin(), string_parameters.end());
                            else managed_string_calls.insert(name + "::" + tokens[j - 1].text);
                        }
                        if (tokens[i].text == "interface") {
                            if (end >= tokens.size() || tokens[end].text != ";") throw std::runtime_error("interface instance methods currently require a declaration without a body");
                            size_t start = j - 1;
                            while (start > open + 1 && tokens[start - 1].text != ";" && tokens[start - 1].text != ":" &&
                                tokens[start - 1].text != "}" && tokens[start - 1].text != "{") --start;
                            bool already_virtual = false;
                            for (size_t k = start; k < j; ++k) already_virtual |= tokens[k].text == "virtual";
                            if (!already_virtual) interface_virtual.insert(start);
                            interface_pure.insert(end);
                            // Parameter attributes contain calls such as
                            // MarshalAs(...), which are not interface methods.
                            for (size_t k = j + 1; k < end; ++k) {
                                class_context[k] = name; class_access[k] = access;
                                class_depth[k] = 1; class_interface[k] = true;
                            }
                            j = end - 1;
                        }
                    }
                }
            }
            // Contextual literal conversion must happen before C++ overload
            // resolution: otherwise char* -> bool outranks the surrogate's
            // user-defined char* -> String^ conversion. Native calls/literals
            // remain untouched. Initially recognize direct imported static calls.
            std::vector<bool> call_context(tokens.size(), false);
            std::vector<bool> parentheses;
            for (size_t i = 0; i < tokens.size(); ++i) {
                if (tokens[i].text == "(") {
                    bool managed = i >= 3 && tokens[i - 2].text == "::" &&
                        managed_string_calls.count(tokens[i - 3].text + "::" + tokens[i - 1].text);
                    if (!managed && i && !class_context[i].empty())
                        managed = managed_string_calls.count(class_context[i] + "::" + tokens[i - 1].text);
                    parentheses.push_back(managed);
                }
                if (!parentheses.empty()) call_context[i] = parentheses.back();
                if (tokens[i].text == ")" && !parentheses.empty()) parentheses.pop_back();
            }
            // A typed catch marker lets Clang parse/check finally bodies. With
            // real catch clauses, nest the catch region inside the finally
            // region, as required by CLR exception table structure.
            std::map<size_t, std::string> cli_structural;
            auto matching = [&](size_t begin, const std::string& open, const std::string& close) {
                int depth = 0;
                for (size_t end = begin; end < tokens.size(); ++end) {
                    if (tokens[end].text == open) ++depth;
                    if (tokens[end].text == close && --depth == 0) return end;
                }
                throw std::runtime_error("unterminated managed exception region");
            };
            // Layout attributes are CLI metadata, not native Clang record
            // attributes. Keep their arguments as expressions so Clang checks
            // names/types and evaluates constants; never infer native layout.
            std::map<size_t, std::string> layouts, layout_fields;
            std::map<size_t, size_t> layout_attribute_ends;
            std::set<size_t> enum_flags;
            for (size_t begin = 0; begin + 3 < tokens.size(); ++begin) {
                if (tokens[begin].text != "[" || tokens[begin+1].text == "[") continue;
                size_t name = begin + 1;
                while (name + 2 < tokens.size() && tokens[name+1].text == "::") name += 2;
                auto attr = tokens[name].text;
                if (attr == "Flags" || attr == "FlagsAttribute") {
                    size_t close = name+1;
                    if (tokens[close].text == "(") {
                        if (tokens[close+1].text != ")") throw std::runtime_error("FlagsAttribute takes no arguments");
                        close += 2;
                    }
                    if (tokens[close].text != "]") throw std::runtime_error("FlagsAttribute must appear individually");
                    size_t enumeration = close+1;
                    if (enumeration+3 >= tokens.size() || (tokens[enumeration].text != "public" && tokens[enumeration].text != "private") ||
                        tokens[enumeration+1].text != "enum" || (tokens[enumeration+2].text != "class" && tokens[enumeration+2].text != "struct"))
                        throw std::runtime_error("FlagsAttribute must precede a managed enum declaration");
                    if (!enum_flags.insert(enumeration).second) throw std::runtime_error("duplicate FlagsAttribute");
                    layout_attribute_ends[begin] = close; begin = close; continue;
                }
                bool layout = attr == "StructLayout" || attr == "StructLayoutAttribute";
                bool offset = attr == "FieldOffset" || attr == "FieldOffsetAttribute";
                if ((!layout && !offset) || tokens[name+1].text != "(") continue;
                size_t close = matching(name+1, "(", ")");
                if (close+1 >= tokens.size() || tokens[close+1].text != "]") throw std::runtime_error("layout attributes must appear individually");
                std::vector<std::string> args;
                size_t start = name+2; int depth = 0;
                for (size_t j = start; j <= close; ++j) {
                    if (j == close || (tokens[j].text == "," && !depth)) {
                        std::string arg;
                        for (size_t k = start; k < j; ++k) arg += tokens[k].text + " ";
                        if (arg.empty()) throw std::runtime_error("layout attributes require a constant argument");
                        args.push_back(arg); start = j+1;
                    } else if (tokens[j].text == "(" || tokens[j].text == "[") ++depth;
                    else if (tokens[j].text == ")" || tokens[j].text == "]") --depth;
                }
                layout_attribute_ends[begin] = close+1;
                if (offset) {
                    if (class_depth[begin] != 1 || args.size() != 1) throw std::runtime_error("FieldOffset requires one argument on a managed instance field");
                    layout_fields[begin] = "__attribute__((annotate(\"cli.field.offset\", " + args[0] + ")))";
                } else {
                    size_t record = close+2;
                    if (record < tokens.size() && (tokens[record].text == "public" || tokens[record].text == "private")) ++record;
                    if (record+1 >= tokens.size() || (tokens[record].text != "ref" && tokens[record].text != "value") ||
                        (tokens[record+1].text != "class" && tokens[record+1].text != "struct")) throw std::runtime_error("StructLayout must precede a managed class or struct");
                    std::string attrs = ", annotate(\"cli.layout\", " + args[0] + ")";
                    std::set<std::string> named;
                    for (size_t a = 1; a < args.size(); ++a) {
                        auto equal = args[a].find('=');
                        std::string key = equal == std::string::npos ? "" : args[a].substr(0, equal);
                        key.erase(std::remove(key.begin(), key.end(), ' '), key.end());
                        if ((key != "Pack" && key != "Size" && key != "CharSet") || !named.insert(key).second)
                            throw std::runtime_error("unsupported or duplicate StructLayout named argument");
                        attrs += ", annotate(\"cli.layout." + key + "\", " + args[a].substr(equal+1) + ")";
                    }
                    if (!layouts.emplace(record, attrs).second) throw std::runtime_error("duplicate StructLayout attribute");
                }
                begin = close+1;
            }
            std::map<size_t, std::string> interop_attributes, interop_types;
            std::map<size_t, size_t> interop_ends;
            std::set<size_t> interop_closes;
            for (size_t begin = 0; begin+3 < tokens.size(); ++begin) {
                if (tokens[begin].text != "[") continue;
                size_t name = begin+1;
                while (name+2 < tokens.size() && tokens[name+1].text == "::") name += 2;
                if (tokens[name].text != "ReliabilityContract" && tokens[name].text != "ReliabilityContractAttribute") continue;
                if (tokens[name+1].text != "(") throw std::runtime_error("ReliabilityContract requires two enum arguments");
                auto end = matching(name+1, "(", ")");
                if (end+1 >= tokens.size() || tokens[end+1].text != "]") throw std::runtime_error("ReliabilityContract must appear individually");
                size_t next = end+2;
                if (next < tokens.size() && (tokens[next].text == "public" || tokens[next].text == "private")) ++next;
                if (next < tokens.size() && (tokens[next].text == "ref" || tokens[next].text == "value" || tokens[next].text == "interface" || tokens[next].text == "class" || tokens[next].text == "struct"))
                    throw std::runtime_error("ReliabilityContract applied to an unsupported declaration target");
                std::vector<std::string> arguments(1); int depth = 0;
                for (size_t j = name+2; j < end; ++j) {
                    if (tokens[j].text == "," && !depth) arguments.emplace_back();
                    else {
                        arguments.back() += tokens[j].text + " ";
                        if (tokens[j].text == "(") ++depth;
                        else if (tokens[j].text == ")") --depth;
                    }
                }
                if (arguments.size() != 2 || arguments[0].empty() || arguments[1].empty()) throw std::runtime_error("ReliabilityContract requires two enum arguments");
                interop_ends[begin] = end+1; interop_closes.insert(end+1);
                interop_attributes[begin] = "__attribute__((annotate(\"cli.reliability\", static_cast<int>(" + arguments[0] + "), static_cast<int>(" + arguments[1] + "))))";
                begin = end+1;
            }
            auto interop_name = [](std::string name) {
                if (llvm::StringRef(name).ends_with("Attribute")) name.resize(name.size()-9);
                return name == "ComImport" || name == "Guid" || name == "InterfaceType" || name == "ClassInterface" || name == "ComVisible" || name == "PreserveSig" ||
                    name == "In" || name == "Out" || name == "MarshalAs" ? name : std::string();
            };
            for (size_t begin = 0; begin+2 < tokens.size(); ++begin) {
                if (tokens[begin].text != "[" || tokens[begin+1].text == "[" || layout_attribute_ends.count(begin) || interop_ends.count(begin)) continue;
                if (begin) {
                    auto previous = tokens[begin-1].text;
                    if (previous != "{" && previous != "}" && previous != ";" && previous != "(" && previous != "," && previous != ":" &&
                        previous != "public" && previous != "private" && previous != "protected" && previous != "internal" && previous != "static" && previous != "virtual" &&
                        !(previous == "]" && interop_closes.count(begin-1))) continue;
                }
                size_t first = begin+1;
                while (first+2 < tokens.size() && tokens[first+1].text == "::") first += 2;
                if (interop_name(tokens[first].text).empty()) continue;
                size_t close = matching(begin, "[", "]"), cursor = begin+1;
                std::string attributes; bool type_attribute = false, member_attribute = false;
                std::set<std::string> seen_attributes;
                while (cursor < close) {
                    size_t name = cursor;
                    std::string qualifier;
                    while (name+2 < close && tokens[name+1].text == "::") { qualifier += tokens[name].text + "::"; name += 2; }
                    if (!qualifier.empty() && qualifier != "System::Runtime::InteropServices::" && qualifier != "Runtime::InteropServices::" && qualifier != "InteropServices::")
                        throw std::runtime_error(tokens[begin].file + ":" + std::to_string(tokens[begin].line) + ": unsupported interop attribute namespace: " + qualifier);
                    auto attr = interop_name(tokens[name].text);
                    if (attr.empty() || !seen_attributes.insert(attr).second) throw std::runtime_error("unsupported or duplicate interop attribute");
                    cursor = name+1;
                    std::string argument;
                    if (cursor < close && tokens[cursor].text == "(") {
                        auto end = matching(cursor, "(", ")");
                        int depth = 0;
                        for (size_t j = cursor+1; j < end; ++j) {
                            if (tokens[j].text == "," && !depth) throw std::runtime_error("interop attributes currently take one positional argument");
                            if (tokens[j].text == "(") ++depth;
                            else if (tokens[j].text == ")") --depth;
                            argument += tokens[j].text + " ";
                        }
                        cursor = end+1;
                    }
                    bool takes_argument = attr == "Guid" || attr == "InterfaceType" || attr == "ClassInterface" || attr == "ComVisible" || attr == "MarshalAs";
                    if (takes_argument == argument.empty()) throw std::runtime_error("wrong interop attribute argument count: " + attr);
                    bool on_type = attr == "ComImport" || attr == "Guid" || attr == "InterfaceType" || attr == "ClassInterface";
                    if (attr == "ComVisible") {
                        size_t next = close+1;
                        while (next < tokens.size() && tokens[next].text == "[") next = matching(next, "[", "]")+1;
                        if (next < tokens.size() && (tokens[next].text == "public" || tokens[next].text == "private")) ++next;
                        on_type = next < tokens.size() && (tokens[next].text == "ref" || tokens[next].text == "value" || tokens[next].text == "interface");
                    }
                    type_attribute |= on_type; member_attribute |= !on_type;
                    auto marker = attr == "ComImport" ? "cli.com.import" : attr == "Guid" ? "cli.com.guid" : attr == "InterfaceType" ? "cli.com.interface" :
                        attr == "ClassInterface" ? "cli.com.class" : attr == "ComVisible" ? "cli.com.visible" :
                        attr == "PreserveSig" ? "cli.preservesig" : attr == "In" ? "cli.param.in" : attr == "Out" ? "cli.param.out" : "cli.param.marshal";
                    attributes += ", annotate(" + quote(marker) + (argument.empty() ? "" : ", " + (attr == "Guid" ? argument : "static_cast<int>(" + argument + ")")) + ")";
                    if (cursor < close) {
                        if (tokens[cursor].text != ",") throw std::runtime_error("unsupported interop attribute arguments");
                        ++cursor;
                    }
                }
                if (type_attribute && member_attribute) throw std::runtime_error("interop attribute list mixes type and member targets");
                interop_ends[begin] = close;
                interop_closes.insert(close);
                if (type_attribute) {
                    size_t record = close+1;
                    while (record < tokens.size() && tokens[record].text == "[") record = matching(record, "[", "]")+1;
                    if (record < tokens.size() && (tokens[record].text == "public" || tokens[record].text == "private")) ++record;
                    if (record+1 >= tokens.size() || (tokens[record].text != "interface" && tokens[record].text != "ref" && tokens[record].text != "value") ||
                        (tokens[record+1].text != "class" && tokens[record+1].text != "struct")) throw std::runtime_error(tokens[begin].file + ":" + std::to_string(tokens[begin].line) + ": interop type attributes must precede a managed type");
                    interop_types[record] += attributes;
                } else interop_attributes[begin] = "__attribute__((" + attributes.substr(2) + "))";
                begin = close;
            }
            for (size_t begin = 0; begin + 1 < tokens.size(); ++begin) {
                if (tokens[begin].text != "try" || tokens[begin + 1].text != "{") continue;
                size_t end = matching(begin + 1, "{", "}") + 1;
                bool catches = false;
                while (end + 1 < tokens.size() && tokens[end].text == "catch" && tokens[end + 1].text == "(") {
                    catches = true; end = matching(end + 1, "(", ")") + 1;
                    if (end == tokens.size() || tokens[end].text != "{") throw std::runtime_error("catch requires a block");
                    end = matching(end, "{", "}") + 1;
                }
                if (end + 1 >= tokens.size() || tokens[end].text != "finally" || tokens[end + 1].text != "{") continue;
                if (catches) cli_structural[begin] = "try { try";
                cli_structural[end] = std::string(catches ? "} " : "") + "catch (::cli::__finally_marker*)";
                size_t close = matching(end + 1, "{", "}");
                cli_structural[close] = "::__cli_end_finally(); }";
            }
            auto adapt_allocation = [&](size_t& i) {
                if (tokens[i].text != "gcnew") return false;
                auto& t = tokens[i];
                size_t end = i + 1;
                int angles = 0;
                while (end < tokens.size()) {
                    if (tokens[end].text == "<") ++angles;
                    else if (tokens[end].text == ">") --angles;
                    else if (tokens[end].text == ">>") angles -= 2;
                    if (!angles && tokens[end].text == "(") break;
                    if (angles < 0 || (!angles && tokens[end].text != ">" && tokens[end].text != ">>" && tokens[end].kind != tok::identifier && tokens[end].text != "::")) break;
                    ++end;
                }
                bool default_allocation = end < tokens.size() && !angles &&
                    (tokens[end].text == ";" || tokens[end].text == "," || tokens[end].text == ")");
                if (end == i + 1 || end == tokens.size() || (tokens[end].text != "(" && !default_allocation))
                    throw std::runtime_error(t.file + ":" + std::to_string(t.line) + ": gcnew currently requires a named type and parenthesized constructor arguments");
                std::string type, primary;
                for (size_t j = i + 1; j < end && tokens[j].text != "<"; ++j)
                    if (tokens[j].kind == tok::identifier) primary = tokens[j].text;
                for (size_t j = i + 1; j < end; ++j) type += tokens[j].text + " ";
                if (!default_allocation && managed_string_constructors.count(primary)) {
                    // Mark only immediate literal arguments of this constructor;
                    // literals inside nested native calls keep their native type.
                    size_t close = matching(end, "(", ")");
                    unsigned depth = 0, argument_index = 0;
                    for (size_t j = end + 1; j < close; ++j) {
                        if (tokens[j].text == "(" || tokens[j].text == "[" || tokens[j].text == "{") ++depth;
                        else if (tokens[j].text == ")" || tokens[j].text == "]" || tokens[j].text == "}") --depth;
                        if (!depth && tokens[j].text == ",") ++argument_index;
                        if (!depth && tok::isStringLiteral(tokens[j].kind) && managed_string_constructors.at(primary).count(argument_index)) call_context[j] = true;
                    }
                }
                // A real new-expression keeps overload resolution and access checks
                // in the original member/friend context. Only its marker is
                // emitted as CLR allocation; no native allocation is generated.
                if (!default_allocation) cli_structural[matching(end, "(", ")")] = ") )";
                emit(t, "__cli_alloc(new " + type + (default_allocation ? "())" : ""));
                i = end - 1;
                return true;
            };
            auto adapt_named_override = [&](size_t& i) {
                if (tokens[i].text != "=") return false;
                size_t end = i+1; int angles = 0;
                std::vector<size_t> separators;
                for (; end < tokens.size(); ++end) {
                    auto text = tokens[end].text;
                    if (!angles && (text == "{" || text == ";")) break;
                    if (text == "<") ++angles; else if (text == ">") --angles; else if (text == ">>") angles -= 2;
                    if (text == "::" && !angles) separators.push_back(end);
                    if (text == "," && !angles) throw std::runtime_error("named override lists currently require one target");
                }
                if (separators.empty()) return false;
                size_t split = separators.back();
                std::string member;
                if (split+1 < end && (tokens[split+1].text == "get" || tokens[split+1].text == "set")) {
                    if (separators.size() < 2) throw std::runtime_error("named accessor override requires an interface property");
                    member = tokens[split+1].text + "_" + tokens[split-1].text;
                    split = separators[separators.size()-2];
                } else {
                    if (split+2 != end) throw std::runtime_error("unsupported named override member");
                    member = tokens[split+1].text;
                }
                std::string target;
                for (size_t j = i+1; j < split; ++j) target += tokens[j].text + " ";
                emit(tokens[i], "__attribute__((annotate(\"cli.override:" + member + "\", static_cast<" + target + "*>(nullptr))))");
                i = end-1; return true;
            };
            std::set<std::pair<std::string, std::string>> property_names;
            std::map<std::pair<std::string, std::string>, std::string> indexed_facades;
            for (size_t i = 0; i < tokens.size(); ++i) {
                auto& t = tokens[i];
                if (interop_ends.count(i)) {
                    if (interface_virtual.count(i)) emit(t, "virtual");
                    if (interop_attributes.count(i)) emit(t, interop_attributes[i]);
                    i = interop_ends[i]; continue;
                }
                if (layout_attribute_ends.count(i)) {
                    if (layout_fields.count(i)) {
                        if (class_access[i] == "internal") emit(t, "__attribute__((annotate(\"cli.access:assembly\")))");
                        emit(t, layout_fields[i]);
                    }
                    i = layout_attribute_ends[i]; continue;
                }
                if (value_defaults.count(i)) { emit(t, "{ " + value_defaults[i]); continue; }
                if (cli_structural.count(i)) { emit(t, cli_structural[i]); continue; }
                if (interface_virtual.count(i)) emit(t, "virtual");
                if (interface_pure.count(i)) emit(t, "= 0");
                if (member_starts.count(i) && class_access[i] == "internal" && t.text != "property" &&
                    !((t.text == "virtual" || t.text == "static") && i+1 < tokens.size() && tokens[i+1].text == "property"))
                    emit(t, "__attribute__((annotate(\"cli.access:assembly\")))");
                if (t.text == "internal" && class_depth[i] == 1 && i + 1 < tokens.size() && tokens[i+1].text == ":") { emit(t, "public"); continue; }
                if (i >= 2 && tokens[i-1].text == "::" && i + 2 < tokens.size() && tokens[i+1].text == "::" &&
                    (tokens[i+2].text == "get" || tokens[i+2].text == "set")) {
                    size_t owner = i-2;
                    if (tokens[owner].text == ">" || tokens[owner].text == ">>") {
                        int angles = 0;
                        for (size_t pos = owner+1; pos-- > 0;) {
                            if (tokens[pos].text == ">") ++angles;
                            else if (tokens[pos].text == ">>") angles += 2;
                            else if (tokens[pos].text == "<" && --angles == 0) {
                                if (pos) owner = pos-1;
                                break;
                            }
                        }
                    }
                    if (property_names.count({tokens[owner].text, t.text})) {
                        emit(t, tokens[i+2].text + "_" + (t.text == "default" ? "Item" : t.text)); i += 2; continue;
                    }
                }
                if (t.text == "static" && i + 1 < tokens.size() && tokens[i+1].text == "property") continue;
                if (t.text == "virtual" && i + 1 < tokens.size() && tokens[i+1].text == "property") continue;
                if (t.text == "=" && class_depth[i] == 1 && i && (tokens[i-1].text == ")" || tokens[i-1].text == "sealed" || tokens[i-1].text == "override"))
                    if (adapt_named_override(i)) continue;
                if (t.text == "static" && i+3 < tokens.size()) {
                    size_t name = i+1;
                    while (name+2 < tokens.size() && tokens[name+1].text == "::") name += 2;
                    bool local_ctor = class_depth[i] == 1 && name == i+1 && tokens[name].text == class_context[i];
                    bool qualified_ctor = class_depth[i] == 0 && name >= i+3 && tokens[name].text == tokens[name-2].text;
                    if ((local_ctor || qualified_ctor) && tokens[name+1].text == "(") {
                        if (tokens[name+2].text != ")") throw std::runtime_error("a managed static constructor takes no parameters");
                        if (tokens[name+3].text == ":") throw std::runtime_error("a managed static constructor cannot have an initializer list");
                        emit(t, std::string(local_ctor ? "static " : "") + "void __attribute__((annotate(\"cli.cctor\")))");
                        for (size_t j = i+1; j < name; ++j) emit(tokens[j], tokens[j].text);
                        emit(tokens[name], "_cli_type_initializer"); i = name; continue;
                    }
                }
                if (t.text == "static" && class_depth[i] == 1) {
                    size_t equal = i + 1, end = equal;
                    while (equal < tokens.size() && tokens[equal].text != "=" && tokens[equal].text != ";" && tokens[equal].text != "(" && tokens[equal].text != "{") ++equal;
                    if (equal < tokens.size() && tokens[equal].text == "=" && tokens[equal - 1].kind == tok::identifier) {
                        end = equal + 1;
                        unsigned parentheses = 0, braces = 0, brackets = 0;
                        for (; end < tokens.size(); ++end) {
                            auto text = tokens[end].text;
                            if (text == ";" && !parentheses && !braces && !brackets) break;
                            if (text == "(") ++parentheses; else if (text == ")") --parentheses;
                            if (text == "{") ++braces; else if (text == "}") --braces;
                            if (text == "[") ++brackets; else if (text == "]") --brackets;
                        }
                        if (end == tokens.size()) throw std::runtime_error("unterminated managed static initializer");
                        std::string field = tokens[equal - 1].text, field_type;
                        for (size_t j = i + 1; j + 1 < equal; ++j) field_type += tokens[j].text + " ";
                        emit(t, std::string(class_access[i] == "internal" ? "__attribute__((annotate(\"cli.access:assembly\"))) " : "") + "static " + field_type + field + "; private: static " + field_type +
                            " __attribute__((annotate(\"cli.static.init:" + field + "\"))) _cli_init_" + field + "() { return");
                        for (size_t j = equal + 1; j < end; ++j) {
                            if (cli_structural.count(j)) { emit(tokens[j], cli_structural[j]); continue; }
                            if (adapt_allocation(j)) continue;
                            if (tokens[j].text == "safe_cast") { emit(tokens[j], "__cli_safe_cast"); continue; }
                            if (tok::isStringLiteral(tokens[j].kind) && call_context[j] &&
                                (tokens[j-1].text == "(" || tokens[j-1].text == ",") &&
                                (tokens[j+1].text == ")" || tokens[j+1].text == ",")) emit(tokens[j], "__cli_literal(" + tokens[j].text + ")");
                            else emit(tokens[j], tokens[j].text);
                        }
                        emit(tokens[end], "; } " + (class_access[i] == "internal" ? "public" : class_access[i]) + ":");
                        i = end; continue;
                    }
                }
                if (!class_context[i].empty() && t.text == "abstract" && i && tokens[i - 1].text == ")") { emit(t, "= 0"); continue; }
                if (!class_context[i].empty() && t.text == "sealed" && i && (tokens[i - 1].text == ")" || tokens[i - 1].text == "override")) { emit(t, "final"); continue; }
                if ((t.text == "public" || t.text == "private") && i + 3 < tokens.size() && tokens[i + 1].text == "enum" &&
                    (tokens[i + 2].text == "class" || tokens[i + 2].text == "struct")) {
                    emit(t, "enum class __attribute__((annotate(\"cli.enum\"), annotate(\"cli.visibility:" + t.text + "\")" +
                        (enum_flags.count(i) ? ", annotate(\"cli.flags\")" : "") + "))");
                    i += 2; continue;
                }
                if (t.text == "property" && !class_context[i].empty()) {
                    size_t name = i + 1;
                    while (name + 1 < tokens.size() && !((tokens[name].kind == tok::identifier || tokens[name].text == "default") &&
                        (tokens[name + 1].text == "{" || tokens[name + 1].text == ";" || tokens[name + 1].text == "["))) ++name;
                    if (name + 1 == tokens.size()) throw std::runtime_error("unsupported property declarator");
                    std::string property = tokens[name].text, property_type;
                    property_names.insert({class_context[i], property});
                    bool is_static = i && tokens[i-1].text == "static";
                    bool is_virtual = class_interface[i] || (i && tokens[i-1].text == "virtual");
                    std::string access_attribute = class_access[i] == "internal" ? "__attribute__((annotate(\"cli.access:assembly\"))) " : "";
                    for (size_t j = i + 1; j < name; ++j) property_type += tokens[j].text + " ";
                    size_t open = name + 1;
                    bool indexed = tokens[open].text == "[", default_indexer = indexed && property == "default";
                    std::string index_type;
                    if (indexed) {
                        if (is_static) throw std::runtime_error("static indexed properties are not enabled yet");
                        size_t close = matching(open, "[", "]"); int angles = 0;
                        for (size_t j = open + 1; j < close; ++j) {
                            if (tokens[j].text == "<") ++angles;
                            else if (tokens[j].text == ">") --angles;
                            else if (tokens[j].text == ">>") angles -= 2;
                            if (tokens[j].text == "," && !angles) throw std::runtime_error("indexed properties currently require one index parameter");
                            index_type += tokens[j].text + " ";
                        }
                        open = close + 1;
                        if (tokens[open].text != "{") throw std::runtime_error("indexed properties require accessor bodies/declarations");
                        if (default_indexer) property = "Item";
                    }
                    size_t end = open; bool getter = false, setter = false;
                    if (tokens[end].text == "{") {
                        int depth = 1; ++end;
                        for (; end < tokens.size() && depth; ++end) {
                            if (tokens[end].text == "{") ++depth;
                            else if (tokens[end].text == "}") --depth;
                            else if (depth == 1 && end + 1 < tokens.size() && tokens[end + 1].text == "(") {
                                getter |= tokens[end].text == "get";
                                setter |= tokens[end].text == "set";
                            }
                        }
                        if (end == tokens.size() && tokens.back().text != "}") throw std::runtime_error("unterminated property");
                    } else { getter = setter = true; ++end; }
                    std::string attribute = "__declspec(property(";
                    if (getter) attribute += "get=get_" + property;
                    if (getter && setter) attribute += ",";
                    if (setter) attribute += "put=set_" + property;
                    bool declare_property = true;
                    if (indexed) {
                        emit(t, "using _cli_index_" + property + "_" + std::to_string(i) + " __attribute__((annotate(\"cli.index:" + property + "\"))) = " + index_type + ";");
                        auto key = std::make_pair(class_context[i], property);
                        auto facade = attribute + ")) " + property_type;
                        auto inserted = indexed_facades.emplace(key, facade);
                        declare_property = inserted.second;
                        if (!inserted.second && inserted.first->second != facade)
                            throw std::runtime_error("overloaded indexed properties require the same value type and accessors");
                    }
                    if (is_static) emit(t, "static __attribute__((annotate(\"cli.static.property:" + std::string(getter ? "get_" + property : "") + "|" + (setter ? "set_" + property : "") + "\"))) __cli_static_property<" + property_type + "> " + property + ";");
                    else if (declare_property) emit(t, attribute + ")) " + property_type + (default_indexer ? "__cli_default" : property) + (indexed ? "[]" : "") + ";");
                    if (tokens[name + 1].text == ";") {
                        if (is_static) throw std::runtime_error("static auto-properties are not enabled yet");
                        emit(t, "private: " + property_type + " __cli_backing_" + property + "; " + class_access[i] + ":");
                        emit(t, std::string(is_virtual ? "virtual " : "") + property_type + " __attribute__((annotate(\"cli.accessor:get\"))) get_" + property + "() { return __cli_backing_" + property + "; }");
                        emit(t, std::string(is_virtual ? "virtual " : "") + "void __attribute__((annotate(\"cli.accessor:set\"))) set_" + property + "(" + property_type + " value) { __cli_backing_" + property + " = value; }");
                    } else {
                        int depth = 1;
                        for (size_t j = open + 1; j + 1 < end; ++j) {
                            if (interop_ends.count(j)) {
                                if (interop_attributes.count(j)) emit(tokens[j], interop_attributes[j]);
                                j = interop_ends[j]; continue;
                            }
                            if (class_interface[i] && depth == 1 && tokens[j].text == ";") emit(tokens[j], "= 0");
                            if (class_interface[i] && depth == 1 && tokens[j].text == "{")
                                throw std::runtime_error("interface property accessors require declarations without bodies");
                            if (depth == 1 && tokens[j].text == "sealed") { emit(tokens[j], "final"); continue; }
                            if (depth == 1 && tokens[j].text == "=") if (adapt_named_override(j)) continue;
                            if (depth == 1 && (tokens[j-1].text == "{" || tokens[j-1].text == "}" || tokens[j-1].text == ";" || tokens[j-1].text == ":") &&
                                tokens[j].text != "private" && tokens[j].text != "public" && tokens[j].text != "protected" && tokens[j].text != ";" && tokens[j].text != "}") {
                                bool explicit_virtual = false;
                                for (size_t k = j; k < end && tokens[k].text != "("; ++k) explicit_virtual |= tokens[k].text == "virtual";
                                emit(tokens[j], std::string(is_static ? "static " : is_virtual && !explicit_virtual ? "virtual " : "") + access_attribute);
                            }
                            if (cli_structural.count(j)) {
                                emit(tokens[j], cli_structural[j]);
                                if (tokens[j].text == "}") --depth;
                                continue;
                            }
                            if (adapt_allocation(j)) continue;
                            if (tokens[j].text == "safe_cast") { emit(tokens[j], "__cli_safe_cast"); continue; }
                            if (depth == 1 && j + 1 < end && tokens[j + 1].text == "(" && (tokens[j].text == "get" || tokens[j].text == "set"))
                                emit(tokens[j], "__attribute__((annotate(\"cli.accessor:" + tokens[j].text + "\"))) " + tokens[j].text + "_" + property);
                            else if (tok::isStringLiteral(tokens[j].kind) && call_context[j] && j > 0 && j + 1 < tokens.size() &&
                                (tokens[j - 1].text == "(" || tokens[j - 1].text == ",") &&
                                (tokens[j + 1].text == ")" || tokens[j + 1].text == ",")) emit(tokens[j], "__cli_literal(" + tokens[j].text + ")");
                            else emit(tokens[j], tokens[j].text);
                            if (tokens[j].text == "{") ++depth;
                            else if (tokens[j].text == "}") --depth;
                        }
                    }
                    i = end - 1; continue;
                }
                if (t.text == "safe_cast") { emit(t, "__cli_safe_cast"); continue; }
                if (t.text == "dynamic_cast" && i + 1 < tokens.size() && tokens[i + 1].text == "<") {
                    size_t end = i + 2;
                    while (end < tokens.size() && tokens[end].text != ">") ++end;
                    bool handle = false;
                    for (size_t j = i + 2; j < end; ++j) if (tokens[j].text == "^") handle = true;
                    if (handle) { emit(t, "__cli_dynamic_cast"); continue; }
                }
                if ((t.text == "public" || t.text == "private") && i + 2 < tokens.size() &&
                    (tokens[i + 1].text == "ref" || tokens[i + 1].text == "interface" || tokens[i + 1].text == "value") &&
                    (tokens[i + 2].text == "class" || tokens[i + 2].text == "struct")) continue;
                if ((t.text == "public" || t.text == "private") && i+2 < tokens.size() &&
                    (tokens[i+1].text == "class" || tokens[i+1].text == "struct") && tokens[i+2].kind == tok::identifier && class_depth[i] == 0) {
                    emit(t, tokens[i+1].text + " __attribute__((annotate(\"cli.native.visibility:" + t.text + "\")))"); ++i; continue;
                }
                if ((t.text == "ref" || t.text == "interface" || t.text == "value") && i + 2 < tokens.size() &&
                    (tokens[i + 1].text == "class" || tokens[i + 1].text == "struct")) {
                    bool interface = t.text == "interface", value = t.text == "value";
                    std::string attributes = value ? "annotate(\"cli.value\")" : "annotate(\"cli.ref\")";
                    if (layouts.count(i)) attributes += layouts[i];
                    if (interop_types.count(i)) attributes += interop_types[i];
                    if (i && (tokens[i-1].text == "public" || tokens[i-1].text == "private")) attributes += ", annotate(\"cli.visibility:" + tokens[i-1].text + "\")";
                    if (interface) attributes += ", annotate(\"cli.interface\"), annotate(\"cli.abstract\")";
                    size_t end = i + 3;
                    while (end < tokens.size() && (tokens[end].text == "abstract" || tokens[end].text == "sealed" || tokens[end].text == "__cli_delegate")) {
                        attributes += ", annotate(\"cli." + (tokens[end].text == "__cli_delegate" ? "delegate" : tokens[end].text) + "\")";
                        ++end;
                    }
                    emit(t, std::string(interface || tokens[i + 1].text == "struct" ? "struct" : "class") + " __attribute__((" + attributes + ")) " + tokens[i + 2].text);
                    if (attributes.find("cli.sealed") != std::string::npos) emit(t, "final");
                    if (end < tokens.size() && tokens[end].text == ":") {
                        // An interface-only class still derives System.Object
                        // in the CLR. Make that implicit class base visible to
                        // Clang's normal reference conversions as well.
                        bool only_interfaces = true;
                        std::string base_name; int base_angles = 0;
                        for (size_t j = end+1; j < tokens.size(); ++j) {
                            auto text = tokens[j].text;
                            if (!base_angles && (text == "," || text == "{" || text == ";")) {
                                if (base_name.rfind("::", 0) == 0) base_name.erase(0, 2);
                                only_interfaces &= metadata.interface_types.count(base_name) != 0;
                                base_name.clear();
                                if (text != ",") break;
                            } else if (text == "<") ++base_angles;
                            else if (text == ">") --base_angles;
                            else if (text == ">>") base_angles -= 2;
                            else if (!base_angles && text != "public" && text != "protected" && text != "private" && text != "virtual") base_name += text;
                        }
                        emit(tokens[end], !interface && !value && only_interfaces ? ": public ::System::Object," : ":"); ++end;
                        bool start = true; int angles = 0;
                        while (end < tokens.size() && tokens[end].text != "{" && tokens[end].text != ";") {
                            if (start && tokens[end].text != "public" && tokens[end].text != "protected" && tokens[end].text != "private") emit(tokens[end], "public");
                            if (tokens[end].text == "protected" || tokens[end].text == "private") throw std::runtime_error("managed bases require public inheritance");
                            if (tokens[end].text == "<") ++angles;
                            else if (tokens[end].text == ">") --angles;
                            else if (tokens[end].text == ">>") angles -= 2;
                            emit(tokens[end], tokens[end].text); start = tokens[end].text == "," && !angles; ++end;
                        }
                    } else if (!interface && !value && end < tokens.size() && tokens[end].text == "{") emit(t, ": public ::System::Object");
                    i = end - 1;
                    continue;
                }
                if (adapt_allocation(i)) continue;
                if (tok::isStringLiteral(t.kind) && call_context[i] && i > 0 && i + 1 < tokens.size() &&
                    (tokens[i - 1].text == "(" || tokens[i - 1].text == ",") &&
                    (tokens[i + 1].text == ")" || tokens[i + 1].text == ",")) {
                    emit(t, "__cli_literal(" + t.text + ")");
                    continue;
                }
                // Keep ^ unchanged. Only the private Clang parser can resolve
                // declarator/type context correctly after C++ scope lookup.
                std::string next = i + 1 < tokens.size() ? tokens[i + 1].text : "";
                if ((t.text == "generic" && next == "<") ||
                    ((t.text == "property" || t.text == "event") && (metadata.short_types.count(next) || next == "int" || next == "void")) ||
                    (t.text == "finally" && next == "{"))
                    throw std::runtime_error("unsupported managed construct: " + t.text);
                emit(t, t.text);
            }
            write_file(options.output, out.str());
        } catch (const std::exception& e) { report(ci, {}, e.what()); }
    }
};

struct FunctionIR { std::string name, owner, result; bool instance; unsigned max_stack; std::string locals, code; };
class Emitter {
    CompilerInstance& ci;
    ASTContext& context;
    const Metadata& metadata;
    std::vector<FunctionDecl*> functions;
    std::vector<CXXRecordDecl*> records;
    std::vector<EnumDecl*> enums;
    std::set<const FunctionDecl*> seen;
    std::set<const CXXRecordDecl*> seen_records;
    std::map<const VarDecl*, unsigned> locals;
    std::map<const ParmVarDecl*, unsigned> parameters;
    std::ostringstream code;
    FunctionDecl* current = nullptr;
    std::string native_library;
    struct NativeImport { std::string library, entry; };
    std::map<std::string, NativeImport> native_imports;
    std::set<const FunctionDecl*> selected_native_imports;
    unsigned label_count = 0, max_stack = 0;
    int stack = 0;
    struct ControlLabels { std::string stop, next; unsigned depth; };
    std::vector<ControlLabels> control_labels;
    std::map<const SwitchCase*, std::pair<std::string, unsigned>> switch_labels;
    unsigned exception_depth = 0, catch_depth = 0, finally_depth = 0, return_slot = 0;
    std::string return_label;
    bool return_slot_set = false;
    std::vector<FunctionIR> ir;
    mutable std::map<std::string, const CXXRecordDecl*> native_records;
    std::vector<std::string> temporary_types;
    std::map<const OpaqueValueExpr*, unsigned> opaque_values;

    [[noreturn]] void fail(const Stmt* s, const std::string& message) const {
        auto loc = ci.getSourceManager().getPresumedLoc(s ? s->getBeginLoc() : current->getLocation());
        throw std::runtime_error((loc.isValid() ? std::string(loc.getFilename()) + ":" + std::to_string(loc.getLine()) + ": " : "") + message);
    }
    void op(const std::string& text, int pop = 0, int push = 0) {
        if (stack < pop) throw std::runtime_error("internal IL stack underflow");
        stack += push - pop;
        max_stack = std::max(max_stack, static_cast<unsigned>(stack));
        code << "    " << text << "\n";
    }
    std::string label() { return "L" + std::to_string(label_count++); }
    void mark(const std::string& name) {
        if (stack != 0) throw std::runtime_error("internal nonempty stack at statement boundary");
        code << name << ":\n";
    }
    const Expr* strip(const Expr* e) const {
        for (;;) {
            if (auto* x = dyn_cast<ParenExpr>(e)) e = x->getSubExpr();
            else if (auto* x = dyn_cast<ExprWithCleanups>(e)) e = x->getSubExpr();
            else if (auto* x = dyn_cast<MaterializeTemporaryExpr>(e)) e = x->getSubExpr();
            else if (auto* x = dyn_cast<CXXBindTemporaryExpr>(e)) e = x->getSubExpr();
            else break;
        }
        return e;
    }
    const CXXRecordDecl* value_target(QualType type) const {
        auto* r = type->getAsCXXRecordDecl();
        return r && record_annotated(r, "cli.value") ? r : nullptr;
    }
    const CXXRecordDecl* handle_target(QualType type) const {
        if (type->isPointerType()) {
            auto* r = type->getPointeeType()->getAsCXXRecordDecl();
            if (!r) return nullptr;
            if (!record_annotation(r, "cli.nativeint:").empty()) return nullptr;
            auto* definition = r->getDefinition();
            if (record_annotated(r, "cli.ref") || record_annotated(r, "cli.value") || !record_annotation(r, "cli.external:").empty() ||
                (definition && (annotated(definition, "cli.ref") || !annotation(definition, "cli.external:").empty()))) return r;
            return nullptr;
        }
        auto* rec = type->getAsCXXRecordDecl();
        auto* spec = dyn_cast_or_null<ClassTemplateSpecializationDecl>(rec);
        if (!spec || spec->getSpecializedTemplate()->getName() != "__cli_handle") return nullptr;
        return spec->getTemplateArgs()[0].getAsType()->getAsCXXRecordDecl();
    }
    const CXXRecordDecl* boxed_target(QualType type) const {
        auto* r = handle_target(type);
        return r && record_annotated(r, "cli.value") ? r : nullptr;
    }
    const CXXMethodDecl* delegate_invoke(const CXXRecordDecl* record) const {
        if (!record_annotated(record, "cli.delegate")) return nullptr;
        auto* definition = record->getDefinition();
        if (!definition) throw std::runtime_error("incomplete delegate type");
        for (auto* method : definition->methods()) if (method->getNameAsString() == "Invoke") return method;
        throw std::runtime_error("delegate lacks an Invoke signature");
    }
    QualType array_element(const CXXRecordDecl* r) const {
        if (!record_annotated(r, "cli.array")) return {};
        auto* specialization = dyn_cast<ClassTemplateSpecializationDecl>(r);
        if (!specialization || specialization->getTemplateArgs().size() != 2 || specialization->getTemplateArgs()[1].getAsIntegral() != 1)
            throw std::runtime_error("only rank-one managed arrays are supported");
        return specialization->getTemplateArgs()[0].getAsType();
    }
    QualType interior_element(QualType t) const {
        auto* r = t->getAsCXXRecordDecl();
        if (!r || (!record_annotated(r, "cli.pin") && !record_annotated(r, "cli.interior"))) return {};
        auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(r);
        if (!spec || spec->getTemplateArgs().size() != 1) throw std::runtime_error("invalid managed pointer intrinsic");
        auto element = spec->getTemplateArgs()[0].getAsType();
        if (!element->isIntegerType() && !element->isRealFloatingType())
            throw std::runtime_error("managed pointer intrinsics currently require primitive elements");
        return element;
    }
    QualType static_property_element(QualType t) const {
        auto* r = t->getAsCXXRecordDecl();
        if (!r || !record_annotated(r, "cli.static.property.proxy")) return {};
        auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(r);
        if (!spec || spec->getTemplateArgs().size() != 1) throw std::runtime_error("invalid static property intrinsic");
        return spec->getTemplateArgs()[0].getAsType();
    }
    bool pinned(QualType t) const { return record_annotated(t->getAsCXXRecordDecl(), "cli.pin"); }
    QualType native_value_pointer(QualType t, const TypeSourceInfo* info) const {
        if (!t->isPointerType() || !value_target(t->getPointeeType()) ||
            !record_annotation(value_target(t->getPointeeType()), "cli.nativeint:").empty() || !info) return {};
        auto& sm = ci.getSourceManager();
        for (TypeLoc loc = info->getTypeLoc(); !loc.isNull(); loc = loc.getNextTypeLoc()) {
            auto alias = loc.getAs<TypedefTypeLoc>();
            if (!alias.isNull()) return native_value_pointer(t, alias.getTypePtr()->getDecl()->getTypeSourceInfo());
            auto pointer = loc.getAs<PointerTypeLoc>();
            if (pointer.isNull()) continue;
            auto sigil = sm.getSpellingLoc(pointer.getStarLoc());
            if (sigil.isValid() && sm.isWrittenInMainFile(sigil) && *sm.getCharacterData(sigil) == '*') return t->getPointeeType();
            return {};
        }
        return {};
    }
    QualType native_value_pointer(const ValueDecl* declaration) const {
        if (annotated(declaration, "cli.box") || annotated(declaration, "cli.box.object")) return {};
        QualType t;
        const TypeSourceInfo* info = nullptr;
        if (auto* function = dyn_cast<FunctionDecl>(declaration)) { t = function->getReturnType(); info = function->getTypeSourceInfo(); }
        else if (auto* field = dyn_cast<FieldDecl>(declaration)) { t = field->getType(); info = field->getTypeSourceInfo(); }
        else if (auto* variable = dyn_cast<VarDecl>(declaration)) { t = variable->getType(); info = variable->getTypeSourceInfo(); }
        else return {};
        if (annotated(declaration, "cli.native.value.pointer") && t->isPointerType()) return t->getPointeeType();
        if (auto* parameter = dyn_cast<ParmVarDecl>(declaration)) {
            auto* function = dyn_cast<FunctionDecl>(parameter->getDeclContext());
            if (function && !annotation(function, "cli.import:").empty()) return {};
        }
        if (!annotation(declaration, "cli.import:").empty() || !annotation(declaration, "cli.import.field:").empty()) return {};
        return native_value_pointer(t, info);
    }
    QualType native_value_pointer(const Expr* original) const {
        const Expr* e = strip(original);
        if (auto* cast = dyn_cast<ExplicitCastExpr>(e)) return native_value_pointer(cast->getType(), cast->getTypeInfoAsWritten());
        if (auto* cast = dyn_cast<ImplicitCastExpr>(e)) {
            if (cast->getCastKind() == CK_NullToPointer) return {};
            return native_value_pointer(cast->getSubExpr());
        }
        if (auto* reference = dyn_cast<DeclRefExpr>(e)) return native_value_pointer(reference->getDecl());
        if (auto* member = dyn_cast<MemberExpr>(e)) return native_value_pointer(member->getMemberDecl());
        if (auto* call = dyn_cast<CallExpr>(e); call && call->getDirectCallee()) return native_value_pointer(call->getDirectCallee());
        if (auto* address = dyn_cast<UnaryOperator>(e); address && address->getOpcode() == UO_AddrOf && value_target(address->getSubExpr()->getType()))
            return address->getSubExpr()->getType();
        if (auto* opaque = dyn_cast<OpaqueValueExpr>(e)) return native_value_pointer(opaque->getSourceExpr());
        if (auto* pseudo = dyn_cast<PseudoObjectExpr>(e)) return native_value_pointer(pseudo->getResultExpr());
        if (auto* conditional = dyn_cast<ConditionalOperator>(e)) {
            auto left = native_value_pointer(conditional->getTrueExpr()), right = native_value_pointer(conditional->getFalseExpr());
            if (left.isNull() != right.isNull()) {
                const Expr* other = left.isNull() ? conditional->getTrueExpr() : conditional->getFalseExpr();
                if (!other->isNullPointerConstant(context, Expr::NPC_ValueDependentIsNotNull))
                    fail(conditional, "native value pointers and boxed value handles cannot be implicitly mixed");
            }
            if (!left.isNull() && !right.isNull() && context.hasSameUnqualifiedType(left, right)) return left;
            if (!left.isNull() && conditional->getFalseExpr()->isNullPointerConstant(context, Expr::NPC_ValueDependentIsNotNull)) return left;
            if (!right.isNull() && conditional->getTrueExpr()->isNullPointerConstant(context, Expr::NPC_ValueDependentIsNotNull)) return right;
        }
        return {};
    }
    std::string declared_type(const ValueDecl* declaration) const {
        auto element = native_value_pointer(declaration);
        if (!element.isNull()) {
            if (!native_storage(element)) throw std::runtime_error("native value pointers require proven GC-free element storage");
            return type(element) + "*";
        }
        if (auto* function = dyn_cast<FunctionDecl>(declaration)) return type(function->getReturnType());
        return type(declaration->getType());
    }
    std::string expression_type(const Expr* expression) const {
        auto element = native_value_pointer(expression);
        return element.isNull() ? type(expression->getType()) : type(element) + "*";
    }
    void pointer_binding(const Expr* value, const ValueDecl* target) const {
        auto target_type = isa<FunctionDecl>(target) ? cast<FunctionDecl>(target)->getReturnType() : target->getType();
        if (!boxed_target(target_type)) return;
        bool native_target = !native_value_pointer(target).isNull();
        if (value->isNullPointerConstant(context, Expr::NPC_ValueDependentIsNotNull)) return;
        if (native_target != !native_value_pointer(value).isNull())
            fail(value, "native value pointers and boxed value handles cannot be implicitly mixed");
    }
    void pointer_value(const Expr* value, const ValueDecl* target) {
        pointer_binding(value, target);
        if (!native_value_pointer(target).isNull() && value->isNullPointerConstant(context, Expr::NPC_ValueDependentIsNotNull)) {
            op("ldc.i4.0", 0, 1); op("conv.u", 1, 1); return;
        }
        expr(value);
    }
    const CXXDestructorDecl* source_destructor(const CXXRecordDecl* record) const {
        if (!record || !record->getDefinition() || !record_annotation(record, "cli.external:").empty()) return nullptr;
        auto* destructor = record->getDefinition()->getDestructor();
        return destructor && !destructor->isImplicit() && !destructor->isDeleted() ? destructor : nullptr;
    }
    struct DisposeBase {
        const CXXRecordDecl* owner = nullptr;
        const CXXMethodDecl* method = nullptr;
        bool generated = false;
        bool boolean = false;
    };
    bool implements_disposable(const CXXRecordDecl* record, std::set<const CXXRecordDecl*> visiting = {}) const {
        if (!record || !visiting.insert(record->getCanonicalDecl()).second) return false;
        if (source_destructor(record) || llvm::StringRef(record_annotation(record, "cli.external:")).ends_with("|System.IDisposable")) return true;
        auto* definition = record->getDefinition();
        if (!definition) return false;
        for (auto& base : definition->bases()) if (implements_disposable(base.getType()->getAsCXXRecordDecl(), visiting)) return true;
        return false;
    }
    DisposeBase dispose_base(const CXXRecordDecl* record) const {
        DisposeBase simple;
        for (auto* parent = class_base(record); parent; parent = class_base(parent)) {
            if (source_destructor(parent)) return {parent, nullptr, true, true};
            if (!implements_disposable(parent)) continue;
            for (auto* method : parent->methods()) {
                if (method->getNameAsString() != "Dispose" || method->isStatic() || !method->getReturnType()->isVoidType()) continue;
                if (method->getNumParams() == 1 && method->getParamDecl(0)->getType()->isBooleanType() &&
                    method->isVirtual() && !method->hasAttr<FinalAttr>() && method->getAccess() != AS_private)
                    return {parent, method, false, true};
                if (!method->getNumParams() && method->getAccess() == AS_public && !simple.owner)
                    simple = {parent, method, false, false};
            }
        }
        return simple;
    }
    void dispose_handle(const Expr* value) {
        unsigned slot = temporary("class [System.Runtime]System.IDisposable");
        expr(value); op("isinst [System.Runtime]System.IDisposable", 1, 1); op("stloc " + std::to_string(slot), 1);
        auto done = label(); op("ldloc " + std::to_string(slot), 0, 1); op("brfalse " + done, 1);
        op("ldloc " + std::to_string(slot), 0, 1); op("callvirt instance void [System.Runtime]System.IDisposable::Dispose()", 1);
        mark(done);
    }
    bool native_reference(const VarDecl* variable) const {
        if (!variable->getType()->isLValueReferenceType() || !variable->getTypeSourceInfo()) return false;
        // Imported CLR byrefs use native C++ reference syntax in the facade,
        // but remain managed byrefs. Source sigils retain their original kind.
        if (auto* parameter = dyn_cast<ParmVarDecl>(variable))
            if (auto* function = dyn_cast<FunctionDecl>(parameter->getDeclContext()); function && !annotation(function, "cli.import:").empty()) return false;
        auto& sm = ci.getSourceManager();
        std::function<bool(const TypeSourceInfo*)> source_kind = [&](const TypeSourceInfo* info) {
            if (!info) return false;
            for (TypeLoc loc = info->getTypeLoc(); !loc.isNull(); loc = loc.getNextTypeLoc()) {
                auto reference = loc.getAs<ReferenceTypeLoc>();
                if (!reference.isNull()) {
                    auto sigil = sm.getSpellingLoc(reference.getSigilLoc());
                    return sigil.isValid() && sm.isWrittenInMainFile(sigil) && *sm.getCharacterData(sigil) == '&';
                }
                auto alias = loc.getAs<TypedefTypeLoc>();
                if (!alias.isNull()) return source_kind(alias.getTypePtr()->getDecl()->getTypeSourceInfo());
            }
            return false;
        };
        return source_kind(variable->getTypeSourceInfo());
    }
    bool native_storage(QualType t, std::set<const CXXRecordDecl*> visiting = {}) const {
        t = t.getCanonicalType();
        if (t->isArithmeticType() || t->isEnumeralType()) return true;
        if (t->isPointerType()) return !handle_target(t);
        if (auto* array = context.getAsConstantArrayType(t)) return native_storage(array->getElementType(), visiting);
        auto* r = t->getAsCXXRecordDecl();
        if (!r) return false;
        if (record_annotated(r, "cli.pin") || record_annotated(r, "cli.interior") || record_annotated(r, "cli.array") ||
            record_annotated(r, "cli.static.property.proxy")) return false;
        if (record_annotated(r, "cli.value") && !record_annotation(r, "cli.external:").empty())
            return record_annotated(r, "cli.native.storage.safe");
        r = r->getDefinition();
        if (!r || !visiting.insert(r).second || r->getNumBases() || r->isPolymorphic()) return false;
        if (!record_annotated(r, "cli.value") && (record_annotated(r, "cli.ref") || !r->isTriviallyCopyable() || !r->isStandardLayout())) return false;
        for (auto* field : r->fields()) if (!native_storage(field->getType(), visiting)) return false;
        return true;
    }
    bool native_pod(QualType t) const {
        auto* r = t->getAsCXXRecordDecl();
        return r && !record_annotated(r, "cli.value") && !record_annotated(r, "cli.ref") &&
            record_annotation(r, "cli.external:").empty() && native_storage(t);
    }
    std::string native_record_type(QualType t) const {
        auto* r = t->getAsCXXRecordDecl()->getDefinition();
        const auto& layout = context.getASTRecordLayout(r);
        auto size = layout.getSize().getQuantity(), alignment = layout.getAlignment().getQuantity();
        if (size < 1 || size > INT32_MAX || alignment > 8) throw std::runtime_error("native POD layout exceeds supported CLR size/alignment");
        std::unique_ptr<MangleContext> mangler(context.createMangleContext());
        std::string identity; llvm::raw_string_ostream out(identity);
        mangler->mangleCanonicalTypeName(context.getCanonicalTagType(r), out);
        std::string name = "'__cpp_cli_native_" + identity + "'";
        native_records.emplace(name, r);
        return "valuetype " + name;
    }
    std::string variable_type(const VarDecl* variable) const {
        if (!native_reference(variable)) return declared_type(variable);
        auto element = variable->getType()->getPointeeType();
        if (!native_storage(element)) throw std::runtime_error("native references require a primitive or proven GC-free value/native layout");
        return type(element) + "* modopt([System.Runtime.CompilerServices.VisualC]System.Runtime.CompilerServices.IsImplicitlyDereferenced)";
    }
    bool heap_interior_return(const Expr* original) const {
        const Expr* e = strip(original)->IgnoreParenImpCasts();
        if (pinned(e->getType())) return false;
        if (auto* construction = dyn_cast<CXXConstructExpr>(e))
            return !interior_element(construction->getType()).isNull() && construction->getNumArgs() == 1 &&
                heap_interior_return(construction->getArg(0));
        if (auto* call = dyn_cast<CallExpr>(e)) {
            auto* callee = call->getDirectCallee();
            return callee && annotated(callee, "cli.string.chars");
        }
        if (auto* conditional = dyn_cast<ConditionalOperator>(e))
            return heap_interior_return(conditional->getTrueExpr()) && heap_interior_return(conditional->getFalseExpr());
        auto* address = dyn_cast<UnaryOperator>(e);
        if (!address || address->getOpcode() != UO_AddrOf) return false;
        auto* location = strip(address->getSubExpr())->IgnoreParenImpCasts();
        if (array_subscript(location)) return true;
        if (auto* member = dyn_cast<MemberExpr>(location)) {
            auto* owner = handle_target(member->getBase()->getType());
            return isa<FieldDecl>(member->getMemberDecl()) && owner && record_annotated(owner, "cli.ref") &&
                !record_annotated(owner, "cli.value");
        }
        return false;
    }
    int64_t layout_constant(const Decl* d, llvm::StringRef name, int64_t fallback) const {
        const AnnotateAttr* found = nullptr;
        for (auto* attr : d->specific_attrs<AnnotateAttr>()) if (attr->getAnnotation() == name) {
            if (found) throw std::runtime_error("duplicate CLI layout metadata");
            found = attr;
        }
        if (!found) return fallback;
        Expr::EvalResult result;
        if (found->args_size() != 1 || !(*found->args_begin())->EvaluateAsInt(result, context) ||
            !result.Val.getInt().isSignedIntN(64)) throw std::runtime_error("CLI layout argument must be an integer constant");
        return result.Val.getInt().getSExtValue();
    }
    int64_t layout_kind(const CXXRecordDecl* r) const {
        auto kind = layout_constant(r, "cli.layout", record_annotated(r, "cli.value") ? 0 : 3);
        if (kind != 0 && kind != 2 && kind != 3) throw std::runtime_error("invalid StructLayout LayoutKind");
        return kind;
    }
    std::string guid_attribute(const Decl* d) const {
        const AnnotateAttr* found = nullptr;
        for (auto* attr : d->specific_attrs<AnnotateAttr>()) if (attr->getAnnotation() == "cli.com.guid") {
            if (found) throw std::runtime_error("duplicate GuidAttribute");
            found = attr;
        }
        if (!found) return {};
        const Expr* argument = found->args_size() == 1 ? (*found->args_begin())->IgnoreParenImpCasts() : nullptr;
        while (auto* constant = dyn_cast_or_null<ConstantExpr>(argument)) argument = constant->getSubExpr()->IgnoreParenImpCasts();
        auto* value = dyn_cast_or_null<StringLiteral>(argument);
        if (!value || !value->isOrdinary()) throw std::runtime_error("GuidAttribute requires an ordinary string literal");
        auto guid = value->getString().str();
        if (guid.size() != 36) throw std::runtime_error("GuidAttribute requires a canonical GUID");
        for (size_t i = 0; i < guid.size(); ++i) {
            bool dash = i == 8 || i == 13 || i == 18 || i == 23;
            if (dash ? guid[i] != '-' : std::string("0123456789abcdefABCDEF").find(guid[i]) == std::string::npos)
                throw std::runtime_error("GuidAttribute requires a canonical GUID");
        }
        return guid;
    }
    void validate_interop_attributes(const Decl* d, unsigned target) const {
        std::set<std::string> seen;
        for (auto* attr : d->specific_attrs<AnnotateAttr>()) {
            auto name = attr->getAnnotation();
            unsigned expected = name.starts_with("cli.com.") ? 1 : (name == "cli.preservesig" || name == "cli.reliability") ? 2 : name.starts_with("cli.param.") ? 3 : 0;
            if (name == "cli.com.visible" && target == 2) expected = 2;
            if (!expected) continue;
            if (expected != target) throw std::runtime_error("interop attribute applied to an unsupported declaration target");
            if (!seen.insert(name.str()).second) throw std::runtime_error("duplicate interop attribute");
        }
    }
    std::string parameter_signature(const ParmVarDecl* p) const {
        validate_interop_attributes(p, 3);
        std::string result = annotated(p, "cli.param.in") ? "[in] " : "";
        if (annotated(p, "cli.param.out")) result += "[out] ";
        result += variable_type(p);
        if (annotated(p, "cli.param.marshal")) {
            static const std::map<int64_t, std::string> native_types = {
                {2,"bool"}, {3,"int8"}, {4,"unsigned int8"}, {5,"int16"}, {6,"unsigned int16"},
                {7,"int32"}, {8,"unsigned int32"}, {9,"int64"}, {10,"unsigned int64"}, {11,"float32"}, {12,"float64"},
                {25,"iunknown"}, {26,"idispatch"}, {28,"interface"}, {31,"int"}, {32,"unsigned int"}
            };
            auto value = layout_constant(p, "cli.param.marshal", -1);
            auto found = native_types.find(value);
            if (found == native_types.end()) throw std::runtime_error("unsupported MarshalAs unmanaged type");
            result += " marshal(" + found->second + ")";
        }
        return result;
    }
    std::string attribute_constructor(const std::string& name, const std::vector<std::string>& parameters) const {
        for (const auto& entry : metadata.methods) {
            const auto& method = entry.second;
            if (method.owner == name && method.name == ".ctor" && method.parameters == parameters)
                return imported_signature(method);
        }
        throw std::runtime_error("select the interop attribute type and constructor dependencies: " + name);
    }
    std::string imported_value_type(const std::string& name) const {
        auto found = metadata.types.find(name);
        if (found == metadata.types.end()) throw std::runtime_error("select attribute enum dependency: " + name);
        return "valuetype " + assembly_reference(found->second) + "'" + name + "'";
    }
    std::pair<int64_t, int64_t> reliability_contract(const Decl* d) const {
        const AnnotateAttr* found = nullptr;
        for (auto* attr : d->specific_attrs<AnnotateAttr>()) if (attr->getAnnotation() == "cli.reliability") {
            if (found) throw std::runtime_error("duplicate ReliabilityContractAttribute");
            found = attr;
        }
        if (!found || found->args_size() != 2) throw std::runtime_error("ReliabilityContract requires two arguments");
        if (!isa<FunctionDecl>(d)) throw std::runtime_error("ReliabilityContract currently requires a method or accessor");
        auto argument = found->args_begin(); int64_t values[2];
        for (unsigned i = 0; i < 2; ++i, ++argument) {
            Expr::EvalResult result;
            if (!(*argument)->EvaluateAsInt(result, context) || !result.Val.getInt().isSignedIntN(64)) throw std::runtime_error("ReliabilityContract arguments must be constant enum values");
            values[i] = result.Val.getInt().getSExtValue();
        }
        if (values[0] < 0 || values[0] > 3 || values[1] < 0 || values[1] > 2) throw std::runtime_error("invalid ReliabilityContract enum value");
        return {values[0], values[1]};
    }
    std::string type_token(QualType t) const {
        if (auto* target = handle_target(t)) {
            if (!array_element(target).isNull()) return type(t);
            if (!target->getDefinition() && record_annotation(target, "cli.external:").empty()) throw std::runtime_error("incomplete managed typeid operand");
            return record_token(target);
        }
        if (auto* record = t->getAsCXXRecordDecl()) {
            if (!record->getDefinition() && record_annotation(record, "cli.external:").empty()) throw std::runtime_error("incomplete managed typeid operand");
            if (!record_annotation(record, "cli.nativeint:").empty()) return type(t);
            return record_token(record);
        }
        if (auto* enumeration = t->getAs<EnumType>()) {
            if (!annotated(enumeration->getDecl(), "cli.enum") && annotation(enumeration->getDecl(), "cli.external:").empty()) throw std::runtime_error("native enum typeid metadata is not enabled yet");
        }
        if (t->isPointerType() && t->getPointeeType()->isRecordType()) throw std::runtime_error("native record typeid metadata is not enabled yet");
        return type(t);
    }
    std::string record_name(const CXXRecordDecl* record) const {
        std::string ext = record_annotation(record, "cli.external:");
        if (!ext.empty()) {
            auto bar = ext.find('|');
            std::string result = assembly_reference(ext.substr(0, bar)) + "'" + ext.substr(bar + 1) + "'";
            if (auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(record)) {
                if (ext.find('`', bar) == std::string::npos) throw std::runtime_error("imported template lacks CLI generic type identity");
                result += '<';
                for (unsigned i = 0; i < spec->getTemplateArgs().size(); ++i) {
                    auto& arg = spec->getTemplateArgs()[i];
                    if (arg.getKind() != TemplateArgument::Type) throw std::runtime_error("CLI generic arguments must be managed types");
                    auto argument = arg.getAsType();
                    if (argument->isVoidType() || (argument->isPointerType() && !handle_target(argument)) || boxed_target(argument))
                        throw std::runtime_error("native pointers and typed boxed handles are not supported as CLI generic arguments");
                    if (i) result += ", ";
                    result += type(argument);
                }
                result += '>';
            }
            return result;
        }
        std::string name;
        if (auto* specialization = dyn_cast<ClassTemplateSpecializationDecl>(record)) {
            if (record->isDependentContext()) throw std::runtime_error("dependent managed template cannot be emitted as a CLR type");
            if (!record_annotated(record, "cli.ref") && !record_annotated(record, "cli.value"))
                throw std::runtime_error("native C++ class template is not a managed type");
            std::unique_ptr<MangleContext> mangler(context.createMangleContext());
            std::string identity; llvm::raw_string_ostream out(identity);
            mangler->mangleCanonicalTypeName(context.getCanonicalTagType(specialization), out);
            name = "'__cpp_cli_template_" + identity + "'";
        } else if (isa<CXXRecordDecl>(record->getDeclContext())) {
            name = "'" + record->getNameAsString() + "'";
        } else {
            name = record->getQualifiedNameAsString();
            for (size_t at; (at = name.find("::")) != std::string::npos;) name.replace(at, 2, ".");
            name = "'" + name + "'";
        }
        if (!record_annotated(record, "cli.ref") && !record_annotated(record, "cli.value"))
            throw std::runtime_error("native C++ class is not a managed type: " + record->getQualifiedNameAsString());
        if (auto* parent = dyn_cast<CXXRecordDecl>(record->getDeclContext())) return record_name(parent) + "/" + name;
        return name;
    }
    std::string record_token(const CXXRecordDecl* record) const {
        return (isa<ClassTemplateSpecializationDecl>(record) && !record_annotation(record, "cli.external:").empty() ?
            (record_annotated(record, "cli.value") ? "valuetype " : "class ") : "") + record_name(record);
    }
    std::string enum_name(const EnumDecl* enumeration) const {
        std::string ext = annotation(enumeration, "cli.external:");
        if (ext.empty() && enumeration->getDefinition()) ext = annotation(enumeration->getDefinition(), "cli.external:");
        if (!ext.empty()) {
            auto bar = ext.find('|'); return assembly_reference(ext.substr(0, bar)) + "'" + ext.substr(bar + 1) + "'";
        }
        std::string name = enumeration->getQualifiedNameAsString();
        for (size_t at; (at = name.find("::")) != std::string::npos;) name.replace(at, 2, ".");
        return "'" + name + "'";
    }
    std::string numeric_type(QualType t) const {
        if (auto* enumeration = t->getAs<EnumType>()) return type(enumeration->getDecl()->getIntegerType());
        return type(t);
    }
    bool boxable(QualType t) const {
        return value_target(t) || t->isEnumeralType() || t->isIntegerType() || t->isRealFloatingType();
    }
    void numeric_conversion(QualType t) {
        std::string target = numeric_type(t), instruction;
        if (target == "int8") instruction = "conv.i1";
        else if (target == "uint8") instruction = "conv.u1";
        else if (target == "int16") instruction = "conv.i2";
        else if (target == "uint16" || target == "char") instruction = "conv.u2";
        else if (target == "int32") instruction = "conv.i4";
        else if (target == "uint32") instruction = "conv.u4";
        else if (target == "int64") instruction = "conv.i8";
        else if (target == "uint64") instruction = "conv.u8";
        else if (target == "float32") instruction = "conv.r4";
        else if (target == "float64") instruction = "conv.r8";
        else throw std::runtime_error("unsupported numeric conversion");
        op(instruction, 1, 1);
    }
    std::string type(QualType t) const {
        t = t.getCanonicalType();
        if (auto element = static_property_element(t); !element.isNull()) return type(element);
        if (auto element = interior_element(t); !element.isNull()) return type(element) + "&";
        if (t->isLValueReferenceType()) return type(t->getPointeeType()) + "&";
        if (auto* r = value_target(t); r && !annotation(r, "cli.nativeint:").empty())
            return annotation(r, "cli.nativeint:") == "unsigned" ? "native unsigned int" : "native int";
        if (auto* enumeration = t->getAs<EnumType>()) {
            auto* decl = enumeration->getDecl();
            if (annotated(decl, "cli.enum") || (decl->getDefinition() && annotated(decl->getDefinition(), "cli.enum")))
                return "valuetype " + enum_name(decl);
            return type(decl->getIntegerType());
        }
        if (auto* r = value_target(t)) return "valuetype " + record_name(r);
        if (native_pod(t)) return native_record_type(t);
        if (t->isVoidType()) return "void";
        if (t->isBooleanType()) return "bool";
        if (t->isSpecificBuiltinType(BuiltinType::Char16) ||
            (t->isWideCharType() && context.getTypeSize(t) == 16)) return "char";
        if (t->isIntegerType()) {
            unsigned bits = context.getTypeSize(t);
            if (bits != 8 && bits != 16 && bits != 32 && bits != 64) throw std::runtime_error("unsupported integer width");
            return std::string(t->isUnsignedIntegerType() ? "uint" : "int") + std::to_string(bits);
        }
        if (t->isRealFloatingType()) {
            unsigned bits = context.getTypeSize(t);
            if (bits != 32 && bits != 64) throw std::runtime_error("unsupported floating-point width");
            return bits == 32 ? "float32" : "float64";
        }
        if (auto* r = handle_target(t)) {
            if (record_annotated(r, "cli.value"))
                return "class [System.Runtime]System.ValueType modopt(" + record_name(r) + ") modopt([System.Runtime.CompilerServices.VisualC]System.Runtime.CompilerServices.IsBoxed)";
            QualType element = array_element(r);
            if (!element.isNull()) return type(element) + "[]";
            std::string name = record_name(r);
            if (name == "[System.Runtime]'System.String'") return "string";
            if (name == "[System.Runtime]'System.Object'") return "object";
            return "class " + name;
        }
        if (t->isPointerType()) {
            auto element = t->getPointeeType();
            if (auto* r = value_target(element); r && !record_annotation(r, "cli.nativeint:").empty()) return type(element) + "*";
            if (element->isRecordType()) return "native unsigned int"; // Opaque native handle; no layout is claimed.
            return type(element) + "*";
        }
        throw std::runtime_error("unsupported CLI type: " + t.getAsString());
    }
    std::string reference_token(QualType t) const {
        auto* r = handle_target(t);
        if (!r) throw std::runtime_error("reference token requires a managed handle");
        return array_element(r).isNull() ? record_token(r) : type(t);
    }
    unsigned temporary(const std::string& il_type) {
        unsigned index = locals.size() + temporary_types.size();
        temporary_types.push_back(il_type);
        return index;
    }
    void validate_declarator(const TypeSourceInfo* info) const {
        if (!info) return;
        auto& sm = ci.getSourceManager();
        for (TypeLoc loc = info->getTypeLoc(); !loc.isNull(); loc = loc.getNextTypeLoc()) {
            auto specialization = loc.getAs<TemplateSpecializationTypeLoc>();
            if (!specialization.isNull()) for (unsigned i = 0; i < specialization.getNumArgs(); ++i) {
                auto argument = specialization.getArgLoc(i);
                if (argument.getArgument().getKind() == TemplateArgument::Type &&
                    !native_value_pointer(argument.getArgument().getAsType(), argument.getTypeSourceInfo()).isNull())
                    throw std::runtime_error("native value-pointer array/generic elements are not supported yet");
            }
            auto reference = loc.getAs<ReferenceTypeLoc>();
            if (!reference.isNull()) {
                if (reference.getType()->isRValueReferenceType()) throw std::runtime_error("rvalue reference signatures are not enabled yet");
                SourceLocation sigil = sm.getSpellingLoc(reference.getSigilLoc());
                if (sigil.isValid() && sm.isWrittenInMainFile(sigil) && *sm.getCharacterData(sigil) != '%' && *sm.getCharacterData(sigil) != '&')
                    throw std::runtime_error("unrecognized CLI reference declarator");
            }
            auto pointer = loc.getAs<PointerTypeLoc>();
            if (pointer.isNull()) continue;
            SourceLocation star = sm.getSpellingLoc(pointer.getStarLoc());
            if (auto* r = value_target(pointer.getType()->getPointeeType()); r && !record_annotation(r, "cli.nativeint:").empty()) {
                if (star.isValid() && sm.isWrittenInMainFile(star) && *sm.getCharacterData(star) == '^')
                    throw std::runtime_error("boxed native integers are not enabled yet");
                continue;
            }
            if (!handle_target(pointer.getType())) continue;
            if (star.isValid() && sm.isWrittenInMainFile(star) && *sm.getCharacterData(star) == '*') {
                if (value_target(pointer.getType()->getPointeeType())) {
                    if (!native_storage(pointer.getType()->getPointeeType())) throw std::runtime_error("native value pointers require proven GC-free element storage");
                    continue;
                }
                auto source = sm.getPresumedLoc(pointer.getStarLoc());
                throw std::runtime_error((source.isValid() ? std::string(source.getFilename()) + ":" + std::to_string(source.getLine()) + ": " : "") +
                    "native '*' pointers to managed reference/value classes are not supported: " + pointer.getType().getAsString() + "; declare a '^' handle or use a native storage bridge");
            }
        }
    }
    std::string field_signature(const FieldDecl* f) const {
        auto* parent = dyn_cast<CXXRecordDecl>(f->getParent());
        if (!parent) throw std::runtime_error("field has no managed parent");
        auto imported = metadata.fields.find(annotation(f, "cli.import.field:"));
        return (imported == metadata.fields.end() ? declared_type(f) : imported->second.type) + " " + record_token(parent) + "::'" + f->getNameAsString() + "'";
    }
    std::string static_field_signature(const VarDecl* f) const {
        if (!f->isStaticDataMember()) throw std::runtime_error("static storage requires a managed class field");
        auto imported = metadata.fields.find(annotation(f, "cli.import.field:"));
        return (imported == metadata.fields.end() ? declared_type(f) : imported->second.type) + " " + record_token(cast<CXXRecordDecl>(f->getDeclContext())) + "::'" + f->getNameAsString() + "'";
    }
    const CXXMethodDecl* static_property_method(const VarDecl* property, bool write) const {
        auto names = annotation(property, "cli.static.property:");
        auto bar = names.find('|');
        if (bar == std::string::npos) throw std::runtime_error("invalid static property declaration");
        auto name = write ? names.substr(bar+1) : names.substr(0,bar);
        if (name.empty()) throw std::runtime_error(write ? "static property has no setter" : "static property has no getter");
        auto* owner = cast<CXXRecordDecl>(property->getDeclContext());
        for (auto* method : owner->methods()) if (method->getNameAsString() == name) return method;
        throw std::runtime_error("static property accessor was not declared: " + name);
    }
    std::string method_name(const FunctionDecl* f) const {
        if (annotated(f, "cli.cctor")) return ".cctor";
        if (isa<CXXConstructorDecl>(f)) return ".ctor";
        if (f->getTemplatedKind() == FunctionDecl::TK_FunctionTemplateSpecialization) {
            std::unique_ptr<MangleContext> mangler(context.createMangleContext());
            std::string name; llvm::raw_string_ostream out(name);
            mangler->mangleName(f, out);
            return "__cpp_template_" + name;
        }
        return f->getNameAsString();
    }
    std::string signature(const FunctionDecl* f) const {
        std::string owner;
        auto* method = dyn_cast<CXXMethodDecl>(f);
        auto imported = metadata.methods.find(annotation(f, "cli.import:"));
        if (imported != metadata.methods.end()) return imported_signature(imported->second, method);
        if (method) owner = record_token(method->getParent()) + "::";
        std::string result = isa<CXXConstructorDecl>(f) ? "void" : declared_type(f);
        std::string name = method_name(f);
        std::string sig = (method && !method->isStatic() ? "instance " : "") + result + " " + owner + "'" + name + "'(";
        for (unsigned i = 0; i < f->getNumParams(); ++i) {
            if (i) sig += ", ";
            sig += variable_type(f->getParamDecl(i));
        }
        return sig + ")";
    }
    std::string imported_signature(const ImportedMethod& m, const CXXMethodDecl* bound = nullptr) const {
        std::string owner = bound ? record_token(bound->getParent()) : assembly_reference(m.assembly) + "'" + m.owner + "'";
        std::string sig = (m.is_static ? "" : "instance ") + m.result + " " + owner + "::'" + m.name + "'(";
        for (size_t i = 0; i < m.parameters.size(); ++i) { if (i) sig += ", "; sig += m.parameters[i]; }
        return sig + ")";
    }
    std::vector<const CXXMethodDecl*> named_overrides(const CXXMethodDecl* method) const {
        std::vector<const CXXMethodDecl*> result;
        for (auto* attribute : method->specific_attrs<AnnotateAttr>()) {
            if (!attribute->getAnnotation().starts_with("cli.override:")) continue;
            if (!method->isVirtual() || method->isStatic() || attribute->args_size() != 1) throw std::runtime_error("named override requires a virtual instance method");
            auto target_type = (*attribute->args_begin())->getType();
            auto* target = target_type->isPointerType() ? target_type->getPointeeType()->getAsCXXRecordDecl() : nullptr;
            if (!target || !is_interface(target)) throw std::runtime_error("named overrides currently require an interface target");
            if (!method->getParent()->isDerivedFrom(target)) throw std::runtime_error("named override target is not an implemented interface");
            auto member = attribute->getAnnotation().drop_front(13).str();
            const CXXMethodDecl* selected = nullptr;
            for (auto* candidate : target->methods()) {
                if (candidate->getNameAsString() != member || candidate->isStatic() || candidate->getNumParams() != method->getNumParams() ||
                    type(candidate->getReturnType()) != type(method->getReturnType())) continue;
                bool match = true;
                for (unsigned i = 0; i < candidate->getNumParams(); ++i) match &= type(candidate->getParamDecl(i)->getType()) == type(method->getParamDecl(i)->getType());
                if (!match) continue;
                if (selected) throw std::runtime_error("ambiguous named override target");
                selected = candidate;
            }
            if (!selected) throw std::runtime_error("named override does not match the interface member signature: " + member);
            result.push_back(selected);
        }
        return result;
    }
    void string_literal(const StringLiteral* s) {
        llvm::SmallVector<llvm::UTF16, 32> utf16;
        if (s->isOrdinary() || s->isUTF8()) {
            auto bytes = s->getString();
            if (!llvm::convertUTF8ToUTF16String(bytes, utf16)) fail(s, "string literal is not valid UTF-8");
        } else {
            for (unsigned i = 0; i < s->getLength(); ++i) {
                uint32_t c = s->getCodeUnit(i);
                if (c <= 0xffff) utf16.push_back(static_cast<llvm::UTF16>(c));
                else if (c <= 0x10ffff) { c -= 0x10000; utf16.push_back(0xd800 + (c >> 10)); utf16.push_back(0xdc00 + (c & 1023)); }
                else fail(s, "invalid Unicode code point");
            }
        }
        std::ostringstream bytes;
        bytes << "ldstr bytearray (";
        const char* hex = "0123456789ABCDEF";
        for (char16_t c : utf16) for (unsigned b : {unsigned(c & 255), unsigned(c >> 8)}) bytes << hex[b >> 4] << hex[b & 15] << ' ';
        bytes << ")";
        op(bytes.str(), 0, 1);
    }
    bool native_field(const FieldDecl* field) const {
        return field && native_pod(context.getCanonicalTagType(field->getParent()));
    }
    uint64_t native_field_offset(const FieldDecl* field, const Expr* where) const {
        if (field->isBitField()) fail(where, "native POD bitfield operations are not enabled yet");
        if (field->getType()->isArrayType()) fail(where, "native POD array field operations are not enabled yet");
        auto* record = cast<CXXRecordDecl>(field->getParent());
        auto bits = context.getASTRecordLayout(record).getFieldOffset(field->getFieldIndex());
        if (bits % context.getCharWidth()) fail(where, "native POD field is not byte-aligned");
        return bits / context.getCharWidth();
    }
    void native_offset(uint64_t offset) {
        if (!offset) return;
        op("ldc.i8 " + std::to_string(offset), 0, 1); op("conv.u", 1, 1); op("add", 2, 1);
    }
    void native_field_address(const MemberExpr* member) {
        auto* field = cast<FieldDecl>(member->getMemberDecl());
        auto offset = native_field_offset(field, member);
        if (member->isArrow()) expr(member->getBase());
        else native_address(member->getBase());
        native_offset(offset);
    }
    void native_field_load(const FieldDecl* field) {
        if (field->getType().isVolatileQualified()) op("volatile.");
        op("unaligned. 1"); op("ldobj " + type(field->getType()), 1, 1);
    }
    void native_field_store(const FieldDecl* field) {
        if (field->getType().isVolatileQualified()) op("volatile.");
        op("unaligned. 1"); op("stobj " + type(field->getType()), 2);
    }
    void address(const Expr* original) {
        const Expr* e = strip(original)->IgnoreParenImpCasts();
        if (auto* unary = dyn_cast<UnaryOperator>(e); unary && unary->getOpcode() == UO_Deref && !native_value_pointer(unary->getSubExpr()).isNull()) {
            expr(unary->getSubExpr()); return;
        }
        if (auto* unary = dyn_cast<UnaryOperator>(e); unary && unary->getOpcode() == UO_Deref && boxed_target(unary->getSubExpr()->getType())) {
            auto* r = boxed_target(unary->getSubExpr()->getType());
            if (isa<CXXThisExpr>(unary->getSubExpr()->IgnoreParenImpCasts())) op("ldarg.0", 0, 1);
            else { expr(unary->getSubExpr()); op("unbox " + record_token(r), 1, 1); }
            return;
        }
        if (auto* d = dyn_cast<DeclRefExpr>(e)) {
            if (auto* p = dyn_cast<ParmVarDecl>(d->getDecl())) op(std::string(p->getType()->isReferenceType() ? "ldarg " : "ldarga ") + std::to_string(parameters.at(p)), 0, 1);
            else if (auto* v = dyn_cast<VarDecl>(d->getDecl())) {
                if (annotated(v, "cli.readonly")) fail(e, "readonly imported fields cannot be passed by writable reference");
                if (!annotation(v, "cli.import.constant:").empty()) fail(e, "imported literals have no addressable CLR storage");
                if (v->isStaticDataMember()) op("ldsflda " + static_field_signature(v), 0, 1);
                else op(std::string(v->getType()->isReferenceType() ? "ldloc " : "ldloca ") + std::to_string(locals.at(v)), 0, 1);
            }
            else fail(e, "value address requires a local or parameter");
            return;
        }
        if (auto* member = dyn_cast<MemberExpr>(e)) {
            auto* field = dyn_cast<FieldDecl>(member->getMemberDecl());
            if (!field) fail(e, "value address requires an instance field");
            if (native_field(field)) { native_field_address(member); return; }
            if (annotated(field, "cli.readonly")) fail(e, "readonly imported fields cannot be passed by writable reference");
            emit_receiver(member->getBase()); op("ldflda " + field_signature(field), 1, 1); return;
        }
        if (auto* subscript = array_subscript(e)) {
            auto slots = capture_array(subscript); load_array_location(slots);
            op("ldelema " + type(subscript->getType()), 2, 1); return;
        }
        unsigned slot = temporary(type(e->getType()));
        expr(e); op("stloc " + std::to_string(slot), 1); op("ldloca " + std::to_string(slot), 0, 1);
    }
    void emit_receiver(const Expr* e) {
        if (!native_value_pointer(e).isNull()) { expr(e); return; }
        if (value_target(e->getType())) {
            auto* at = strip(e)->IgnoreParenImpCasts();
            const ValueDecl* field = nullptr;
            if (auto* d = dyn_cast<DeclRefExpr>(at)) field = d->getDecl();
            else if (auto* member = dyn_cast<MemberExpr>(at)) field = member->getMemberDecl();
            if (field && annotated(field, "cli.readonly")) {
                auto slot = temporary(type(e->getType()));
                expr(e); op("stloc " + std::to_string(slot), 1); op("ldloca " + std::to_string(slot), 0, 1);
            } else address(e);
        }
        else if (isa<CXXThisExpr>(strip(e)->IgnoreParenImpCasts()) && isa<CXXMethodDecl>(current) &&
                 record_annotated(cast<CXXMethodDecl>(current)->getParent(), "cli.value")) op("ldarg.0", 0, 1);
        else if (auto* r = boxed_target(e->getType())) { expr(e); op("unbox " + record_token(r), 1, 1); }
        else expr(e); // this on a value class is already a managed address.
    }
    void native_address(const Expr* original) {
        const Expr* e = strip(original)->IgnoreParenImpCasts();
        if (auto* member = dyn_cast<MemberExpr>(e); member && native_field(dyn_cast<FieldDecl>(member->getMemberDecl()))) {
            native_field_address(member); return;
        }
        if (auto* reference = dyn_cast<DeclRefExpr>(e)) {
            auto* variable = dyn_cast<VarDecl>(reference->getDecl());
            if (variable && variable->hasLocalStorage() && (!variable->getType()->isReferenceType() || native_reference(variable))) {
                address(e); op("conv.u", 1, 1); return;
            }
        }
        if (auto* dereference = dyn_cast<UnaryOperator>(e); dereference && dereference->getOpcode() == UO_Deref &&
            dereference->getSubExpr()->getType()->isPointerType() && !handle_target(dereference->getSubExpr()->getType())) {
            expr(dereference->getSubExpr()); return;
        }
        if (auto* operation = dyn_cast<CXXOperatorCallExpr>(e); operation && operation->getOperator() == OO_Star) {
            auto* method = dyn_cast_or_null<CXXMethodDecl>(operation->getDirectCallee());
            if (method && pinned(context.getCanonicalTagType(method->getParent()))) {
                expr(operation->getArg(0)); op("conv.u", 1, 1); return;
            }
        }
        fail(e, "native reference cannot bind to movable managed storage or an unproven alias; use a tracking reference or pin");
    }
    std::string receiver_type(const FieldDecl* field) const {
        auto* parent = cast<CXXRecordDecl>(field->getParent());
        return record_annotated(parent, "cli.value") ? "valuetype " + record_name(parent) + "&" : "class " + record_name(parent);
    }
    void load_decl(const ValueDecl* d, const Expr* at) {
        if (auto* enumeration = dyn_cast<EnumConstantDecl>(d)) {
            llvm::SmallString<32> value; enumeration->getInitVal().toString(value, 10);
            op(std::string(context.getTypeSize(enumeration->getType()) > 32 ? "ldc.i8 " : "ldc.i4 ") + value.str().str(), 0, 1); return;
        }
        if (auto* p = dyn_cast<ParmVarDecl>(d)) { op("ldarg " + std::to_string(parameters.at(p)), 0, 1); if (p->getType()->isReferenceType()) op("ldobj " + type(p->getType()->getPointeeType()), 1, 1); return; }
        if (auto* v = dyn_cast<VarDecl>(d)) {
            auto literal = annotation(v, "cli.import.constant:");
            if (!literal.empty()) {
                auto found = metadata.fields.find(literal);
                if (found == metadata.fields.end() || found->second.constant.empty()) fail(at, "imported literal is absent from metadata");
                op(found->second.constant, 0, 1); return;
            }
            if (!annotation(v, "cli.static.property:").empty()) { op("call " + signature(static_property_method(v, false)), 0, 1); return; }
            if (v->isStaticDataMember()) { op("ldsfld " + static_field_signature(v), 0, 1); return; }
            auto it = locals.find(v);
            if (it == locals.end()) fail(at, "global/static storage is not supported yet");
            op("ldloc " + std::to_string(it->second), 0, 1); if (v->getType()->isReferenceType()) op("ldobj " + type(v->getType()->getPointeeType()), 1, 1); return;
        }
        fail(at, "unsupported declaration reference");
    }
    const CXXOperatorCallExpr* array_subscript(const Expr* e) const {
        auto* call = dyn_cast<CXXOperatorCallExpr>(e->IgnoreParenImpCasts());
        if (!call || call->getOperator() != OO_Subscript || call->getNumArgs() != 2) return nullptr;
        auto* method = dyn_cast_or_null<CXXMethodDecl>(call->getDirectCallee());
        return method && !array_element(method->getParent()).isNull() ? call : nullptr;
    }
    const Expr* array_receiver(const CXXOperatorCallExpr* call) const {
        auto* dereference = dyn_cast<UnaryOperator>(call->getArg(0)->IgnoreParenImpCasts());
        if (!dereference || dereference->getOpcode() != UO_Deref) fail(call, "array indexing lacks a managed handle receiver");
        return dereference->getSubExpr();
    }
    std::pair<unsigned, unsigned> capture_array(const CXXOperatorCallExpr* call) {
        const Expr* receiver = array_receiver(call);
        unsigned handle = temporary(type(receiver->getType())), index = temporary("int32");
        expr(receiver); op("stloc " + std::to_string(handle), 1);
        expr(call->getArg(1)); op("stloc " + std::to_string(index), 1);
        return {handle, index};
    }
    void load_array_location(std::pair<unsigned, unsigned> slots) {
        op("ldloc " + std::to_string(slots.first), 0, 1);
        op("ldloc " + std::to_string(slots.second), 0, 1);
    }
    void store(const Expr* target, bool retain) {
        target = target->IgnoreParenImpCasts();
        if (auto* dereference = dyn_cast<UnaryOperator>(target); dereference && dereference->getOpcode() == UO_Deref &&
            (!handle_target(dereference->getSubExpr()->getType()) || !native_value_pointer(dereference->getSubExpr()).isNull())) {
            unsigned slot = temporary(type(target->getType()));
            op("stloc " + std::to_string(slot), 1); expr(dereference->getSubExpr()); op("ldloc " + std::to_string(slot), 0, 1);
            op("stobj " + type(target->getType()), 2);
            if (retain) op("ldloc " + std::to_string(slot), 0, 1);
            return;
        }
        if (auto* subscript = array_subscript(target)) {
            unsigned result = temporary(type(subscript->getType())); op("stloc " + std::to_string(result), 1);
            auto slots = capture_array(subscript); load_array_location(slots); op("ldloc " + std::to_string(result), 0, 1);
            op("stelem " + type(subscript->getType()), 3);
            if (retain) op("ldloc " + std::to_string(result), 0, 1);
            return;
        }
        if (auto* member = dyn_cast<MemberExpr>(target)) {
            auto* field = dyn_cast<FieldDecl>(member->getMemberDecl());
            if (!field) fail(target, "assignment target is not an instance field");
            if (annotated(field, "cli.readonly")) fail(target, "readonly imported fields cannot be assigned");
            unsigned slot = temporary(declared_type(field));
            op("stloc " + std::to_string(slot), 1);
            if (native_field(field)) {
                native_field_address(member); op("ldloc " + std::to_string(slot), 0, 1); native_field_store(field);
                if (retain) op("ldloc " + std::to_string(slot), 0, 1);
                return;
            }
            emit_receiver(member->getBase()); op("ldloc " + std::to_string(slot), 0, 1);
            op("stfld " + field_signature(field), 2);
            if (retain) op("ldloc " + std::to_string(slot), 0, 1);
            return;
        }
        auto* d = dyn_cast<DeclRefExpr>(target);
        if (!d) fail(target, "assignment target is not a local or parameter");
        if (annotated(d->getDecl(), "cli.readonly")) fail(target, "readonly imported fields cannot be assigned");
        if (d->getDecl()->getType()->isReferenceType()) {
            unsigned slot = temporary(type(d->getType()));
            op("stloc " + std::to_string(slot), 1); address(d); op("ldloc " + std::to_string(slot), 0, 1);
            op("stobj " + type(d->getType()), 2);
            if (retain) op("ldloc " + std::to_string(slot), 0, 1);
            return;
        }
        if (retain) op("dup", 1, 2);
        if (auto* p = dyn_cast<ParmVarDecl>(d->getDecl())) op("starg " + std::to_string(parameters.at(p)), 1);
        else if (auto* v = dyn_cast<VarDecl>(d->getDecl())) {
            if (v->isStaticDataMember()) op("stsfld " + static_field_signature(v), 1);
            else op("stloc " + std::to_string(locals.at(v)), 1);
        }
        else fail(target, "unsupported assignment target");
    }
    void binary(const BinaryOperator* b) {
        if ((b->getLHS()->getType()->isPointerType() || b->getRHS()->getType()->isPointerType()) &&
            !handle_target(b->getLHS()->getType()) && !handle_target(b->getRHS()->getType()) &&
            b->getOpcode() != BO_Assign && b->getOpcode() != BO_EQ && b->getOpcode() != BO_NE)
            fail(b, "unmanaged pointer arithmetic and ordering are not enabled yet");
        if ((handle_target(b->getLHS()->getType()) || handle_target(b->getRHS()->getType())) &&
            b->getOpcode() != BO_Assign && b->getOpcode() != BO_EQ && b->getOpcode() != BO_NE)
            fail(b, "managed handle arithmetic and ordering are unsupported");
        if (b->getOpcode() == BO_Assign) {
            const ValueDecl* declaration = nullptr;
            auto* target = b->getLHS()->IgnoreParenImpCasts();
            if (auto* reference = dyn_cast<DeclRefExpr>(target)) declaration = reference->getDecl();
            if (auto* member = dyn_cast<MemberExpr>(target)) declaration = member->getMemberDecl();
            if (declaration) pointer_value(b->getRHS(), declaration); else expr(b->getRHS());
            store(b->getLHS(), true); return;
        }
        if (b->isCompoundAssignmentOp()) {
            std::string operation;
            switch (b->getOpcode()) {
                case BO_AddAssign: operation = "add"; break;
                case BO_SubAssign: operation = "sub"; break;
                case BO_MulAssign: operation = "mul"; break;
                default: fail(b, "unsupported compound assignment");
            }
            if (auto* subscript = array_subscript(b->getLHS())) {
                auto slots = capture_array(subscript); load_array_location(slots);
                op("ldelem " + type(subscript->getType()), 2, 1); expr(b->getRHS()); op(operation, 2, 1);
                unsigned result = temporary(type(subscript->getType())); op("stloc " + std::to_string(result), 1);
                load_array_location(slots); op("ldloc " + std::to_string(result), 0, 1); op("stelem " + type(subscript->getType()), 3);
                op("ldloc " + std::to_string(result), 0, 1); return;
            }
            if (auto* member = dyn_cast<MemberExpr>(b->getLHS()->IgnoreParenImpCasts())) {
                auto* field = dyn_cast<FieldDecl>(member->getMemberDecl());
                if (!field) fail(b, "compound assignment target is not an instance field");
                if (native_field(field)) {
                    unsigned pointer = temporary("native unsigned int"), result = temporary(type(field->getType()));
                    native_field_address(member); op("stloc " + std::to_string(pointer), 1);
                    op("ldloc " + std::to_string(pointer), 0, 1); native_field_load(field);
                    expr(b->getRHS()); op(operation, 2, 1); op("stloc " + std::to_string(result), 1);
                    op("ldloc " + std::to_string(pointer), 0, 1); op("ldloc " + std::to_string(result), 0, 1);
                    native_field_store(field); op("ldloc " + std::to_string(result), 0, 1); return;
                }
                unsigned receiver = temporary(native_value_pointer(member->getBase()).isNull() ? receiver_type(field) : expression_type(member->getBase()));
                unsigned result = temporary(type(field->getType()));
                emit_receiver(member->getBase()); op("stloc " + std::to_string(receiver), 1);
                op("ldloc " + std::to_string(receiver), 0, 1); op("ldfld " + field_signature(field), 1, 1);
                expr(b->getRHS()); op(operation, 2, 1); op("stloc " + std::to_string(result), 1);
                op("ldloc " + std::to_string(receiver), 0, 1); op("ldloc " + std::to_string(result), 0, 1);
                op("stfld " + field_signature(field), 2); op("ldloc " + std::to_string(result), 0, 1);
                return;
            }
            expr(b->getLHS()); expr(b->getRHS());
            op(operation, 2, 1); store(b->getLHS(), true); return;
        }
        if (b->getOpcode() == BO_LAnd || b->getOpcode() == BO_LOr) {
            int base = stack; auto short_path = label(), done = label();
            expr(b->getLHS()); op(std::string(b->getOpcode() == BO_LAnd ? "brfalse " : "brtrue ") + short_path, 1);
            expr(b->getRHS());
            if (stack != base + 1) fail(b, "logical operand did not produce one value");
            op("br " + done); stack = base; code << short_path << ":\n";
            op(b->getOpcode() == BO_LAnd ? "ldc.i4.0" : "ldc.i4.1", 0, 1);
            code << done << ":\n"; return;
        }
        if ((b->getOpcode() == BO_EQ || b->getOpcode() == BO_NE) &&
            (!native_value_pointer(b->getLHS()).isNull() || !native_value_pointer(b->getRHS()).isNull())) {
            for (const Expr* operand : {b->getLHS(), b->getRHS()}) {
                if (operand->isNullPointerConstant(context, Expr::NPC_ValueDependentIsNotNull)) { op("ldc.i4.0", 0, 1); op("conv.u", 1, 1); }
                else {
                    if (boxed_target(operand->getType()) && native_value_pointer(operand).isNull()) fail(b, "native value pointers and boxed value handles cannot be implicitly mixed");
                    expr(operand);
                }
            }
        } else { expr(b->getLHS()); expr(b->getRHS()); }
        bool unsign = b->getLHS()->getType()->isUnsignedIntegerType();
        bool unordered = unsign || ((b->getOpcode() == BO_GE || b->getOpcode() == BO_LE) && b->getLHS()->getType()->isRealFloatingType());
        std::string operation;
        switch (b->getOpcode()) {
            case BO_Add: operation = "add"; break;
            case BO_Sub: operation = "sub"; break;
            case BO_Mul: operation = "mul"; break;
            case BO_Div: operation = unsign ? "div.un" : "div"; break;
            case BO_Rem: operation = unsign ? "rem.un" : "rem"; break;
            case BO_And: operation = "and"; break;
            case BO_Or: operation = "or"; break;
            case BO_Xor: operation = "xor"; break;
            case BO_Shl: operation = "shl"; break;
            case BO_Shr: operation = unsign ? "shr.un" : "shr"; break;
            case BO_EQ: case BO_NE: operation = "ceq"; break;
            case BO_LT: case BO_GE: operation = unordered ? "clt.un" : "clt"; break;
            case BO_GT: case BO_LE: operation = unordered ? "cgt.un" : "cgt"; break;
            default: fail(b, "unsupported binary operator");
        }
        op(operation, 2, 1);
        if (b->getOpcode() == BO_NE || b->getOpcode() == BO_GE || b->getOpcode() == BO_LE) { op("ldc.i4.0", 0, 1); op("ceq", 2, 1); }
    }
    bool native_com_interface(const CXXRecordDecl* original) const {
        auto* record = original ? original->getDefinition() : nullptr;
        if (!record || record_annotated(record, "cli.ref") || record_annotated(record, "cli.value") ||
            !record->isPolymorphic() || !record->field_empty() || record->getNumBases() > 1 || record->getNumVBases()) return false;
        for (auto* method : record->methods()) if (method->isVirtual() && (!method->isPureVirtual() || isa<CXXDestructorDecl>(method))) return false;
        if (!record->getNumBases())
            return record->getQualifiedNameAsString() == "IUnknown" && ci.getSourceManager().isInSystemHeader(record->getLocation());
        auto& base = *record->bases_begin();
        auto* parent = base.getType()->getAsCXXRecordDecl();
        return parent && native_com_interface(parent) &&
            context.getASTRecordLayout(record).getBaseClassOffset(parent).isZero();
    }
    std::string native_com_parameter(const ParmVarDecl* parameter) const {
        auto t = parameter->getType();
        if (t->isReferenceType()) {
            auto element = t->getPointeeType();
            if (!element->isArithmeticType() && !element->isEnumeralType() && !native_pod(element))
                throw std::runtime_error("native COM references require native scalar or POD storage");
            return "native unsigned int";
        }
        if (t->isEnumeralType()) return numeric_type(t);
        if ((!t->isIntegerType() && !t->isRealFloatingType() && !t->isPointerType()) || t->isBooleanType() ||
            (handle_target(t) && native_value_pointer(parameter).isNull()))
            throw std::runtime_error("native COM signatures require scalars or unmanaged pointers; automatic marshalling is not supported");
        return declared_type(parameter);
    }
    void native_com_call(const CXXMemberCallExpr* call, const CXXMethodDecl* method) {
        if (!context.getTargetInfo().getTriple().isOSWindows() || context.getTargetInfo().getTriple().getArch() != llvm::Triple::x86_64 ||
            context.getTargetInfo().getPointerWidth(LangAS::Default) != 64)
            fail(call, "native COM dispatch currently requires Windows x64");
        auto* callee = dyn_cast<MemberExpr>(call->getCallee()->IgnoreParenImpCasts());
        if (!callee || callee->hasQualifier()) fail(call, "native COM dispatch requires an unqualified virtual call");
        if (method->isVariadic()) fail(call, "variadic native COM dispatch is unsupported");
        auto convention = method->getType()->castAs<FunctionType>()->getCallConv();
        if (convention != CC_C && convention != CC_Win64) fail(call, "native COM dispatch requires the platform Windows ABI");
        auto result = method->getReturnType();
        if (!result->isVoidType() && ((!result->isIntegerType() && !result->isRealFloatingType() && !result->isPointerType() && !result->isEnumeralType()) ||
            result->isBooleanType() || handle_target(result))) fail(call, "native COM return values require a scalar or unmanaged pointer");
        auto* tables = context.getVTableContext();
        uint64_t slot;
        if (tables->isMicrosoft()) {
            auto location = cast<MicrosoftVTableContext>(tables)->getMethodVFTableLocation(GlobalDecl(method));
            if (!location.VFPtrOffset.isZero() || location.VBTableIndex) fail(call, "native COM dispatch excludes secondary or virtual-base vtables");
            slot = location.Index;
        } else slot = cast<ItaniumVTableContext>(tables)->getMethodVTableIndex(GlobalDecl(method));
        unsigned receiver = temporary("native unsigned int");
        expr(call->getImplicitObjectArgument()); op("stloc " + std::to_string(receiver), 1);
        op("ldloc " + std::to_string(receiver), 0, 1);
        std::string signature = "native unsigned int";
        for (unsigned i = 0; i < call->getNumArgs(); ++i) {
            auto* parameter = method->getParamDecl(i);
            signature += ", " + native_com_parameter(parameter);
            if (parameter->getType()->isReferenceType()) native_address(call->getArg(i));
            else pointer_value(call->getArg(i), parameter);
        }
        op("ldloc " + std::to_string(receiver), 0, 1); op("ldind.i", 1, 1);
        native_offset(slot * 8); op("ldind.i", 1, 1);
        op("calli unmanaged stdcall " + (result->isEnumeralType() ? numeric_type(result) : declared_type(method)) + "(" + signature + ")",
           call->getNumArgs() + 2, result->isVoidType() ? 0 : 1);
    }
    void call(const CallExpr* c) {
        auto* f = c->getDirectCallee();
        if (!f) fail(c, "indirect calls are not supported yet");
        if (auto* member = dyn_cast<CXXMemberCallExpr>(c); member && member->getMethodDecl()->isVirtual() &&
            native_com_interface(member->getMethodDecl()->getParent())) {
            native_com_call(member, member->getMethodDecl()); return;
        }
        // Clang can prove constexpr SDK calls without executing native code.
        // Require side-effect-free integer evaluation; dynamic calls still need
        // an emitted body or an explicit native binding.
        if (f->isConstexpr() && ci.getSourceManager().isInSystemHeader(f->getLocation()) &&
            (c->getType()->isIntegerType() || c->getType()->isEnumeralType())) {
            Expr::EvalResult evaluated;
            if (c->EvaluateAsInt(evaluated, context, Expr::SE_NoSideEffects)) {
                auto value = evaluated.Val.getInt();
                unsigned bits = context.getTypeSize(c->getType()) > 32 ? 64 : 32;
                auto constant = value.isSigned() ? value.sextOrTrunc(bits) : value.zextOrTrunc(bits);
                op(std::string(bits == 64 ? "ldc.i8 " : "ldc.i4 ") + std::to_string(constant.getSExtValue()), 0, 1);
                return;
            }
        }
        auto binding = native_imports.find(f->getNameAsString());
        if (binding != native_imports.end() && f->isExternC() && !isa<CXXMethodDecl>(f) && !f->hasBody() &&
            f->hasAttr<DLLImportAttr>()) {
            selected_native_imports.insert(f->getCanonicalDecl());
            if (seen.insert(f->getCanonicalDecl()).second) functions.push_back(const_cast<FunctionDecl*>(f));
        }
        if (isa<CXXDestructorDecl>(f)) {
            auto* member = dyn_cast<CXXMemberCallExpr>(c);
            if (!member || !handle_target(member->getImplicitObjectArgument()->getType()))
                fail(c, "explicit destruction currently requires a managed handle receiver");
            if (!native_value_pointer(member->getImplicitObjectArgument()).isNull()) fail(c, "native value destruction requires a native implementation");
            dispose_handle(member->getImplicitObjectArgument()); return;
        }
        if (annotated(f, "cli.cctor")) fail(c, "managed static constructors cannot be called directly");
        if (auto* method = dyn_cast<CXXMethodDecl>(f); method && record_annotated(method->getParent(), "cli.static.property.proxy")) {
            if (auto* opcall = dyn_cast<CXXOperatorCallExpr>(c); opcall && opcall->getOperator() == OO_Equal) {
                auto* reference = dyn_cast<DeclRefExpr>(strip(opcall->getArg(0))->IgnoreParenImpCasts());
                auto* property = reference ? dyn_cast<VarDecl>(reference->getDecl()) : nullptr;
                if (!property) fail(c, "static property assignment lacks a qualified property");
                unsigned slot = temporary(type(opcall->getArg(1)->getType()));
                expr(opcall->getArg(1)); op("stloc " + std::to_string(slot), 1); op("ldloc " + std::to_string(slot), 0, 1);
                op("call " + signature(static_property_method(property, true)), 1);
                op("ldloc " + std::to_string(slot), 0, 1); return;
            }
            auto* member = dyn_cast<CXXMemberCallExpr>(c);
            if (member) { expr(member->getImplicitObjectArgument()); return; }
            if (auto* opcall = dyn_cast<CXXOperatorCallExpr>(c); opcall && opcall->getOperator() == OO_Arrow) { expr(opcall->getArg(0)); return; }
            fail(c, "unsupported static property operation");
        }
        if (annotated(f, "cli.box.object")) {
            auto* member = dyn_cast<CXXMemberCallExpr>(c);
            auto* method = dyn_cast<CXXMethodDecl>(f);
            auto* target = handle_target(c->getType());
            if (!member || !method || !record_annotated(method->getParent(), "cli.value") || !target ||
                record_name(target) != "[System.Runtime]'System.Object'") fail(c, "invalid value-to-Object boxing conversion");
            auto* value = strip(member->getImplicitObjectArgument());
            if (value->getType()->isPointerType()) {
                if (!isa<CXXThisExpr>(value->IgnoreParenImpCasts())) { expr(value); return; }
                emit_receiver(value); op("ldobj valuetype " + record_name(method->getParent()), 1, 1);
            } else expr(value);
            op("box valuetype " + record_name(method->getParent()), 1, 1); return;
        }
        if (annotated(f, "cli.box")) {
            auto* member = dyn_cast<CXXMemberCallExpr>(c);
            if (!member || !boxed_target(c->getType())) fail(c, "invalid implicit boxing conversion");
            auto* value = member->getImplicitObjectArgument();
            expr(value); op("box valuetype " + record_name(boxed_target(c->getType())), 1, 1); return;
        }
        if (annotated(f, "cli.string.chars")) {
            if (c->getNumArgs() != 1 || type(c->getArg(0)->getType()) != "string") fail(c, "PtrToStringChars requires a String handle");
            expr(c->getArg(0));
            op("callvirt instance char& modreq([System.Runtime]System.Runtime.InteropServices.InAttribute) [System.Runtime]System.String::GetPinnableReference()", 1, 1);
            return;
        }
        if (auto* method = dyn_cast<CXXMethodDecl>(f); method && pinned(context.getCanonicalTagType(method->getParent()))) {
            if (auto* assignment = dyn_cast<CXXOperatorCallExpr>(c); assignment && assignment->getOperator() == OO_Equal) {
                auto* lhs = dyn_cast<DeclRefExpr>(assignment->getArg(0)->IgnoreParenImpCasts());
                auto* variable = lhs ? dyn_cast<VarDecl>(lhs->getDecl()) : nullptr;
                if (assignment->getNumArgs() != 2 || !variable || !variable->hasLocalStorage() || !pinned(variable->getType()))
                    fail(c, "pin assignment requires a block-local pin variable");
                expr(assignment->getArg(1)); op("dup", 1, 2);
                op("stloc " + std::to_string(locals.at(variable)), 1); return;
            }
        }
        if (auto* method = dyn_cast<CXXMethodDecl>(f); method && !interior_element(context.getCanonicalTagType(method->getParent())).isNull()) {
            const Expr* receiver = nullptr;
            if (auto* member = dyn_cast<CXXMemberCallExpr>(c)) receiver = member->getImplicitObjectArgument();
            else if (auto* operation = dyn_cast<CXXOperatorCallExpr>(c)) receiver = operation->getArg(0);
            if (!receiver) fail(c, "managed pointer operation lacks a receiver");
            expr(receiver);
            if (isa<CXXConversionDecl>(method) && pinned(receiver->getType()) && c->getType()->isPointerType() && !handle_target(c->getType())) {
                if (interior_element(receiver->getType()).isConstQualified() && !c->getType()->getPointeeType().isConstQualified())
                    fail(c, "pin_ptr cast cannot discard element const qualification");
                op("conv.u", 1, 1);
            }
            else if (method->getOverloadedOperator() == OO_Star) op("ldobj " + type(interior_element(receiver->getType())), 1, 1);
            else fail(c, "unsupported managed pointer operation");
            return;
        }
        if (auto* subscript = array_subscript(c)) {
            expr(array_receiver(subscript)); expr(subscript->getArg(1)); op("ldelem " + type(subscript->getType()), 2, 1); return;
        }
        if (auto* method = dyn_cast<CXXMethodDecl>(f)) if (!array_element(method->getParent()).isNull() && method->getNameAsString() == "get_Length") {
            auto* member = dyn_cast<CXXMemberCallExpr>(c);
            if (!member) fail(c, "array Length lacks a receiver");
            emit_receiver(member->getImplicitObjectArgument()); op("ldlen", 1, 1); op("conv.i4", 1, 1); return;
        }
        if (f->getNameAsString() == "__cli_end_finally") {
            if (!finally_depth) fail(c, "finally marker outside a finally region");
            return;
        }
        if (f->getNameAsString() == "__cli_literal") {
            if (c->getNumArgs() != 1 || !isa<StringLiteral>(c->getArg(0)->IgnoreParenImpCasts())) fail(c, "invalid string literal marker");
            expr(c->getArg(0)); return;
        }
        if (f->getNameAsString() == "__cli_empty_params") {
            auto* target = handle_target(c->getType());
            auto element = target ? array_element(target) : QualType();
            if (c->getNumArgs() || element.isNull()) fail(c, "empty parameter-array marker requires a rank-one managed array result");
            op("ldc.i4.0", 0, 1); op("newarr " + type(element), 1, 1); return;
        }
        if (f->getNameAsString() == "__cli_typeid") {
            auto* arguments = f->getTemplateSpecializationArgs();
            if (c->getNumArgs() || !arguments || arguments->size() != 1 || arguments->get(0).getKind() != TemplateArgument::Type)
                fail(c, "managed typeid requires a type operand");
            op("ldtoken " + type_token(arguments->get(0).getAsType()), 0, 1);
            op("call class [System.Runtime]System.Type [System.Runtime]System.Type::GetTypeFromHandle(valuetype [System.Runtime]System.RuntimeTypeHandle)", 1, 1); return;
        }
        if (llvm::StringRef(f->getNameAsString()).starts_with("__cli_each_")) {
            if (!c->getNumArgs()) fail(c, "enumeration intrinsic lacks a receiver");
            auto receiver = c->getArg(0)->getType();
            auto* record = handle_target(receiver);
            if (!record) fail(c, "for each currently requires managed array or reference enumerator handles");
            auto element = array_element(record);
            auto name = f->getNameAsString();
            if (name == "__cli_each_dispose") {
                if (element.isNull()) {
                    unsigned slot = temporary("class [System.Runtime]System.IDisposable");
                    expr(c->getArg(0)); op("isinst [System.Runtime]System.IDisposable", 1, 1); op("stloc " + std::to_string(slot), 1);
                    auto done = label(); op("ldloc " + std::to_string(slot), 0, 1); op("brfalse " + done, 1);
                    op("ldloc " + std::to_string(slot), 0, 1); op("callvirt instance void [System.Runtime]System.IDisposable::Dispose()", 1);
                    mark(done);
                }
                return;
            }
            if (!element.isNull()) {
                if (name == "__cli_each_get") { expr(c->getArg(0)); return; }
                if (name == "__cli_each_current") {
                    expr(c->getArg(0)); expr(c->getArg(1)); op("ldelem " + type(element), 2, 1); return;
                }
                if (name == "__cli_each_next") {
                    address(c->getArg(1)); op("dup", 1, 2); op("ldind.i4", 1, 1); op("ldc.i4.1", 0, 1); op("add", 2, 1); op("stind.i4", 2);
                    expr(c->getArg(1)); expr(c->getArg(0)); op("ldlen", 1, 1); op("conv.i4", 1, 1); op("clt", 2, 1); return;
                }
            }
            auto* result = dyn_cast<DecltypeType>(f->getReturnType().getTypePtr());
            auto* selected = result ? dyn_cast<CXXMemberCallExpr>(result->getUnderlyingExpr()->IgnoreParenImpCasts()) : nullptr;
            auto* method = selected ? selected->getMethodDecl() : nullptr;
            if (!method || method->getNumParams() || method->isStatic()) fail(c, "enumeration intrinsic lacks a Clang-selected instance method");
            if (name == "__cli_each_get" && !handle_target(method->getReturnType())) fail(c, "value enumerators are not enabled yet");
            if (name == "__cli_each_next" && !method->getReturnType()->isBooleanType()) fail(c, "enumerator MoveNext must return bool");
            expr(c->getArg(0)); op("callvirt " + signature(method), 1, 1); return;
        }
        if (auto* opcall = dyn_cast<CXXOperatorCallExpr>(c)) {
            if (opcall->getOperator() == OO_Arrow) { expr(opcall->getArg(0)); return; }
            if (opcall->getOperator() == OO_Equal && opcall->getNumArgs() == 2 && (handle_target(opcall->getArg(0)->getType()) || value_target(opcall->getArg(0)->getType()) || native_pod(opcall->getArg(0)->getType()))) {
                expr(opcall->getArg(1)); store(opcall->getArg(0), true); return;
            }
        }
        if (f->getNameAsString() == "__cli_safe_cast" || f->getNameAsString() == "__cli_dynamic_cast") {
            if (c->getNumArgs() != 1) fail(c, "managed casts require one argument");
            if (!native_value_pointer(c->getArg(0)).isNull()) fail(c, "native value pointers cannot be used as managed cast operands");
            auto* target = handle_target(c->getType());
            bool safe = f->getNameAsString() == "__cli_safe_cast";
            QualType source_type = c->getArg(0)->getType();
            if (safe && type(source_type) == type(c->getType())) { expr(c->getArg(0)); return; }
            if (safe && (source_type->isArithmeticType() || source_type->isEnumeralType()) &&
                (c->getType()->isArithmeticType() || c->getType()->isEnumeralType())) {
                expr(c->getArg(0));
                if (c->getType()->isBooleanType()) {
                    auto source = numeric_type(source_type);
                    op(source == "int64" || source == "uint64" ? "ldc.i8 0" : source == "float32" ? "ldc.r4 0" : source == "float64" ? "ldc.r8 0" : "ldc.i4.0", 0, 1);
                    op("ceq", 2, 1); op("ldc.i4.0", 0, 1); op("ceq", 2, 1); return;
                }
                if (source_type->isUnsignedIntegerType() && c->getType()->isRealFloatingType()) op("conv.r.un", 1, 1);
                numeric_conversion(c->getType()); return;
            }
            if (safe && target && boxable(source_type)) {
                if (target != value_target(source_type) && record_name(target) != "[System.Runtime]'System.Object'") fail(c, "boxing requires the matching value handle or an Object handle target");
                expr(c->getArg(0)); op("box " + type(source_type), 1, 1); return;
            }
            if (safe && boxable(c->getType()) && handle_target(source_type)) {
                expr(c->getArg(0)); op("unbox.any " + type(c->getType()), 1, 1); return;
            }
            if (!target || !handle_target(source_type)) fail(c, "unsupported managed cast types");
            expr(c->getArg(0));
            op(std::string(safe ? "castclass " : "isinst ") + reference_token(c->getType()), 1, 1);
            return;
        }
        if (f->getNameAsString() == "__cli_alloc") {
            auto* target = handle_target(c->getType());
            if (!target) fail(c, "allocation marker has no managed type");
            auto* allocation = c->getNumArgs() == 1 ? dyn_cast<CXXNewExpr>(c->getArg(0)->IgnoreParenImpCasts()) : nullptr;
            auto* construction = allocation ? allocation->getConstructExpr() : nullptr;
            if (!construction || allocation->isArray()) fail(c, "allocation has no Clang-resolved managed constructor");
            auto* selected = construction->getConstructor();
            const unsigned count = construction->getNumArgs();
            auto argument = [&](unsigned i) { return construction->getArg(i); };
            if (auto* invoke = delegate_invoke(target)) {
                if (count != 1 && count != 2) fail(c, "delegate construction requires a method and optional target");
                auto* address = argument(count-1)->IgnoreParenImpCasts();
                if (auto* unary = dyn_cast<UnaryOperator>(address); unary && unary->getOpcode() == UO_AddrOf) address = unary->getSubExpr()->IgnoreParenImpCasts();
                auto* reference = dyn_cast<DeclRefExpr>(address);
                auto* function = reference ? dyn_cast<FunctionDecl>(reference->getDecl()) : nullptr;
                if (!function) fail(c, "delegate binding requires a named managed method");
                auto* method = dyn_cast<CXXMethodDecl>(function);
                bool instance = method && !method->isStatic();
                if (instance != (count == 2)) fail(c, "delegate target does not match static/instance method binding");
                if (type(function->getReturnType()) != type(invoke->getReturnType()) || function->getNumParams() != invoke->getNumParams()) fail(c, "delegate method signature mismatch");
                for (unsigned i = 0; i < function->getNumParams(); ++i)
                    if (type(function->getParamDecl(i)->getType()) != type(invoke->getParamDecl(i)->getType())) fail(c, "delegate method parameter signature mismatch");
                if (instance) {
                    auto* owner = handle_target(argument(0)->getType());
                    if (!owner || record_annotated(owner, "cli.value") || (owner->getCanonicalDecl() != method->getParent()->getCanonicalDecl() && !owner->isDerivedFrom(method->getParent())))
                        fail(c, "delegate instance target requires a compatible managed reference class");
                    expr(argument(0));
                    if (method->isVirtual()) { op("dup", 1, 2); op("ldvirtftn " + signature(function), 1, 1); }
                    else op("ldftn " + signature(function), 0, 1);
                } else { op("ldnull", 0, 1); op("ldftn " + signature(function), 0, 1); }
                if (annotation(function, "cli.import:").empty() && !seen.count(function->getCanonicalDecl())) fail(c, "delegate target has no emitted definition");
                op("newobj instance void " + record_token(target) + "::.ctor(object, native int)", 2, 1); return;
            }
            if (is_interface(target)) fail(c, "cannot allocate a managed interface");
            QualType element = array_element(target);
            if (!element.isNull()) {
                if (count != 1 || !argument(0)->getType()->isIntegerType()) fail(c, "array allocation requires one integer length");
                expr(argument(0)); op("newarr " + type(element), 1, 1); return;
            }
            if (record_name(target) == "[System.Runtime]'System.String'") {
                if (count != 1 || !isa<StringLiteral>(argument(0)->IgnoreParenImpCasts()))
                    fail(c, "String allocation currently requires a literal");
                expr(argument(0));
                op("callvirt instance char[] [System.Runtime]System.String::ToCharArray()", 1, 1);
                op("newobj instance void [System.Runtime]System.String::.ctor(char[])", 1, 1);
                return;
            }
            if (target->getDefinition() && annotated(target->getDefinition(), "cli.abstract")) fail(c, "cannot allocate an abstract managed class");
            if (record_annotated(target, "cli.value") && annotated(selected, "cli.value.zero")) {
                auto value_type = "valuetype " + record_name(target);
                unsigned slot = temporary(value_type);
                op("ldloca " + std::to_string(slot), 0, 1); op("initobj " + record_token(target), 1);
                op("ldloc " + std::to_string(slot), 0, 1); op("box " + value_type, 1, 1); return;
            }
            if (!record_annotation(target, "cli.external:").empty() && annotation(selected, "cli.import:").empty())
                fail(c, "imported class has no public metadata constructor");
            if (selected->getNumParams() != count) fail(c, "default constructor arguments are not supported yet");
            for (unsigned i = 0; i < count; ++i) {
                if (type(argument(i)->getType()) != type(selected->getParamDecl(i)->getType())) fail(c, "constructor argument conversion is not enabled yet");
                if (selected->getParamDecl(i)->getType()->isReferenceType()) address(argument(i));
                else pointer_value(argument(i), selected->getParamDecl(i));
            }
            op("newobj " + signature(selected), count, 1);
            if (record_annotated(target, "cli.value")) op("box valuetype " + record_name(target), 1, 1);
            return;
        }
        auto* m = dyn_cast<CXXMethodDecl>(f);
        bool instance = m && !m->isStatic();
        if (instance) {
            auto* member = dyn_cast<CXXMemberCallExpr>(c);
            if (!member) fail(c, "unsupported instance call shape");
            emit_receiver(member->getImplicitObjectArgument());
        }
        for (unsigned i = 0; i < c->getNumArgs(); ++i) {
            if (native_reference(f->getParamDecl(i))) native_address(c->getArg(i));
            else if (f->getParamDecl(i)->getType()->isReferenceType()) address(c->getArg(i)); else pointer_value(c->getArg(i), f->getParamDecl(i));
        }
        std::string id = annotation(f, "cli.import:");
        bool delegate_call = m && delegate_invoke(m->getParent()) == m;
        if (id.empty() && !native_function(f) && !seen.count(f->getCanonicalDecl()) && !delegate_call)
            fail(c, "calling a function without an emitted definition; native calls require a separately compiled bridge: " + f->getQualifiedNameAsString());
        std::string sig;
        if (!id.empty()) {
            auto imported = metadata.methods.find(id);
            if (imported == metadata.methods.end()) fail(c, "imported member is absent from metadata");
            sig = imported_signature(imported->second, m);
        } else {
            if (!f->hasBody() && !(m && m->isPureVirtual()) && !native_function(f) && !delegate_call) fail(c, "calling a function without an emitted definition");
            sig = signature(f);
        }
        bool qualified = false;
        if (auto* member = dyn_cast<CXXMemberCallExpr>(c)) if (auto* callee = dyn_cast<MemberExpr>(member->getCallee()->IgnoreParenImpCasts())) qualified = callee->hasQualifier();
        if (qualified && m && m->isPureVirtual()) fail(c, "a qualified base call cannot target an abstract method");
        op(std::string(instance && !qualified && !record_annotated(m->getParent(), "cli.value") ? "callvirt " : "call ") + sig, c->getNumArgs() + unsigned(instance), f->getReturnType()->isVoidType() ? 0 : 1);
    }
    void expr(const Expr* original) {
        const Expr* e = strip(original);
        if (auto* allocation = dyn_cast<CXXNewExpr>(e)) {
            auto element = allocation->getAllocatedType();
            if (allocation->isArray() || allocation->getNumPlacementArgs() ||
                (!element->isArithmeticType() && !element->isEnumeralType() && !(element->isPointerType() && !handle_target(element)) && !native_pod(element)))
                fail(allocation, "native allocation currently requires a single scalar or trivial native POD; arrays, placement and native objects need a native implementation");
            auto* allocator = allocation->getOperatorNew();
            if (!allocator || !allocator->isReplaceableGlobalAllocationFunction() || allocator->hasBody() ||
                (!allocator->isImplicit() && !ci.getSourceManager().isInSystemHeader(allocator->getLocation())))
                fail(allocation, "custom native allocation operators require a native implementation");
            if (context.getTypeAlignInChars(element).getQuantity() > 8)
                fail(allocation, "native allocation exceeds supported scalar/POD alignment");
            unsigned pointer = temporary("void*");
            op("ldc.i8 " + std::to_string(context.getTypeSizeInChars(element).getQuantity()), 0, 1); op("conv.u", 1, 1);
            op("call void* [System.Runtime.InteropServices]System.Runtime.InteropServices.NativeMemory::Alloc(native unsigned int)", 1, 1);
            op("stloc " + std::to_string(pointer), 1);
            if (auto* initializer = allocation->getInitializer()) {
                if (!initializer->HasSideEffects(context)) {
                    op("ldloc " + std::to_string(pointer), 0, 1); expr(initializer); op("stobj " + type(element), 2);
                    op("ldloc " + std::to_string(pointer), 0, 1); return;
                }
                if (stack) fail(allocation, "native allocation with a side-effectful initializer requires an empty evaluation stack");
                unsigned initialized = temporary("bool");
                auto done = label(), cleanup_done = label();
                op("ldc.i4.0", 0, 1); op("stloc " + std::to_string(initialized), 1);
                code << "    .try {\n"; ++exception_depth;
                op("ldloc " + std::to_string(pointer), 0, 1); expr(initializer); op("stobj " + type(element), 2);
                op("ldc.i4.1", 0, 1); op("stloc " + std::to_string(initialized), 1);
                op("leave " + done); --exception_depth;
                code << "    } finally {\n";
                op("ldloc " + std::to_string(initialized), 0, 1); op("brtrue " + cleanup_done, 1);
                op("ldloc " + std::to_string(pointer), 0, 1); op("call void [System.Runtime.InteropServices]System.Runtime.InteropServices.NativeMemory::Free(void*)", 1);
                mark(cleanup_done); op("endfinally"); code << "    }\n"; mark(done);
            }
            op("ldloc " + std::to_string(pointer), 0, 1); return;
        }
        if (isa<CXXScalarValueInitExpr>(e) || isa<ImplicitValueInitExpr>(e)) {
            auto value = e->getType();
            auto token = type(value);
            if (native_pod(value)) {
                auto slot = temporary(token);
                op("ldloca " + std::to_string(slot), 0, 1); op("initobj " + token, 1);
                op("ldloc " + std::to_string(slot), 0, 1);
            } else if (value->isPointerType() && !handle_target(value)) { op("ldc.i4.0", 0, 1); op("conv.u", 1, 1); }
            else if (value->isRealFloatingType()) op(token == "float32" ? "ldc.r4 0" : "ldc.r8 0", 0, 1);
            else if (value->isIntegerType() || value->isEnumeralType()) op(context.getTypeSize(value) > 32 ? "ldc.i8 0" : "ldc.i4.0", 0, 1);
            else fail(e, "unsupported value initialization");
            return;
        }
        if (auto* cast = dyn_cast<CastExpr>(e); cast &&
            (cast->getCastKind() == CK_DerivedToBase || cast->getCastKind() == CK_UncheckedDerivedToBase) &&
            cast->getType()->isPointerType() && cast->getSubExpr()->getType()->isPointerType() &&
            native_com_interface(cast->getType()->getPointeeType()->getAsCXXRecordDecl()) &&
            native_com_interface(cast->getSubExpr()->getType()->getPointeeType()->getAsCXXRecordDecl())) {
            // The supported COM interface chain has one nonvirtual base at
            // offset zero, so the already type-checked upcast preserves bits.
            expr(cast->getSubExpr()); return;
        }
        if (auto* unary = dyn_cast<UnaryOperator>(e); unary && unary->getOpcode() == UO_Deref && !native_value_pointer(unary->getSubExpr()).isNull()) {
            expr(unary->getSubExpr()); op("ldobj " + type(unary->getType()), 1, 1); return;
        }
        if (auto* cast = dyn_cast<ExplicitCastExpr>(e); cast && !native_value_pointer(cast).isNull()) {
            if (cast->getCastKind() != CK_BitCast && cast->getCastKind() != CK_NoOp && cast->getCastKind() != CK_NullToPointer)
                fail(cast, "unsupported native value pointer cast");
            if (boxed_target(cast->getSubExpr()->getType()) && native_value_pointer(cast->getSubExpr()).isNull() &&
                !cast->getSubExpr()->isNullPointerConstant(context, Expr::NPC_ValueDependentIsNotNull))
                fail(cast, "boxed value handles cannot be cast to untracked native value pointers");
            if (cast->getSubExpr()->isNullPointerConstant(context, Expr::NPC_ValueDependentIsNotNull)) { op("ldc.i4.0", 0, 1); op("conv.u", 1, 1); }
            else expr(cast->getSubExpr());
            return;
        }
        if (auto* cast = dyn_cast<ExplicitCastExpr>(e); cast && handle_target(cast->getType()) && !native_value_pointer(cast->getSubExpr()).isNull())
            fail(cast, "native value pointers cannot be cast to boxed value handles");
        if (auto* unary = dyn_cast<UnaryOperator>(e); unary && unary->getOpcode() == UO_AddrOf && !native_value_pointer(unary).isNull()) {
            if (!native_storage(unary->getSubExpr()->getType())) fail(unary, "native value pointers require proven GC-free element storage");
            native_address(unary->getSubExpr()); return;
        }
        if (auto* deletion = dyn_cast<CXXDeleteExpr>(e)) {
            auto argument = deletion->getArgument()->getType();
            if (argument->isPointerType() && !handle_target(argument)) {
                auto element = argument->getPointeeType();
                if (deletion->isArrayForm() || (!element->isVoidType() && !element->isArithmeticType() && !element->isEnumeralType() && !(element->isPointerType() && !handle_target(element)) && !native_pod(element)))
                    fail(deletion, "native deletion requires a single scalar, trivial native POD or erased void pointer; arrays and native objects need a native implementation");
                auto* deallocator = deletion->getOperatorDelete();
                if (!deallocator || !deallocator->isReplaceableGlobalAllocationFunction() || deallocator->hasBody() ||
                    (!deallocator->isImplicit() && !ci.getSourceManager().isInSystemHeader(deallocator->getLocation())))
                    fail(deletion, "custom native deletion operators require a native implementation");
                expr(deletion->getArgument()); op("call void [System.Runtime.InteropServices]System.Runtime.InteropServices.NativeMemory::Free(void*)", 1); return;
            }
            if (deletion->isArrayForm() || !handle_target(deletion->getArgument()->getType()) || !native_value_pointer(deletion->getArgument()).isNull())
                fail(e, "native deletion and delete[] require a native implementation; managed delete requires a single handle");
            dispose_handle(deletion->getArgument()); return;
        }
        if (auto* c = dyn_cast<ImplicitCastExpr>(e)) {
            auto* conversion_call = dyn_cast<CXXMemberCallExpr>(strip(c->getSubExpr()));
            auto* conversion_method = conversion_call ? dyn_cast_or_null<CXXMethodDecl>(conversion_call->getDirectCallee()) : nullptr;
            bool static_property_conversion = conversion_method && record_annotated(conversion_method->getParent(), "cli.static.property.proxy");
            if (c->getCastKind() == CK_NullToPointer) {
                if (handle_target(c->getType())) op("ldnull", 0, 1);
                else { op("ldc.i4.0", 0, 1); op("conv.u", 1, 1); }
                return;
            }
            expr(c->getSubExpr());
            if (c->getCastKind() == CK_PointerToBoolean && (!handle_target(c->getSubExpr()->getType()) || !native_value_pointer(c->getSubExpr()).isNull())) {
                op("ldc.i4.0", 0, 1); op("conv.u", 1, 1); op("ceq", 2, 1); op("ldc.i4.0", 0, 1); op("ceq", 2, 1); return;
            }
            if (c->getCastKind() == CK_PointerToBoolean && handle_target(c->getSubExpr()->getType())) {
                op("ldnull", 0, 1); op("ceq", 2, 1); op("ldc.i4.0", 0, 1); op("ceq", 2, 1); return;
            }
            if (c->getCastKind() == CK_IntegralToBoolean) { op("ldc.i4.0", 0, 1); op("ceq", 2, 1); op("ldc.i4.0", 0, 1); op("ceq", 2, 1); }
            else if (c->getCastKind() == CK_IntegralCast || c->getCastKind() == CK_IntegralToFloating || c->getCastKind() == CK_FloatingCast || c->getCastKind() == CK_FloatingToIntegral) {
                numeric_conversion(c->getType());
            } else if (c->getCastKind() != CK_LValueToRValue && c->getCastKind() != CK_NoOp && c->getCastKind() != CK_ArrayToPointerDecay && c->getCastKind() != CK_FunctionToPointerDecay &&
                       !(c->getCastKind() == CK_ConstructorConversion && (handle_target(c->getType()) || !interior_element(c->getType()).isNull())) &&
                       !(c->getCastKind() == CK_UserDefinedConversion && (c->getType()->isPointerType() || static_property_conversion)) &&
                       !(c->getCastKind() == CK_BitCast && c->getType()->isPointerType() && !handle_target(c->getType())) &&
                       !((c->getCastKind() == CK_DerivedToBase || c->getCastKind() == CK_UncheckedDerivedToBase || c->getCastKind() == CK_NullToPointer) && handle_target(c->getType())))
                fail(c, std::string("unsupported implicit conversion: ") + c->getCastKindName());
            return;
        }
        if (auto* i = dyn_cast<IntegerLiteral>(e)) {
            llvm::SmallString<32> digits;
            i->getValue().toString(digits, 10, i->getType()->isSignedIntegerType());
            op(std::string(context.getTypeSize(i->getType()) > 32 ? "ldc.i8 " : "ldc.i4 ") + digits.str().str(), 0, 1); return;
        }
        if (auto* b = dyn_cast<CXXBoolLiteralExpr>(e)) { op(b->getValue() ? "ldc.i4.1" : "ldc.i4.0", 0, 1); return; }
        if (auto* c = dyn_cast<CharacterLiteral>(e)) { op("ldc.i4 " + std::to_string(c->getValue()), 0, 1); return; }
        if (auto* f = dyn_cast<FloatingLiteral>(e)) {
            llvm::SmallString<32> digits;
            f->getValue().toString(digits);
            op(std::string(context.getTypeSize(f->getType()) == 32 ? "ldc.r4 " : "ldc.r8 ") + digits.str().str(), 0, 1); return;
        }
        if (auto* s = dyn_cast<StringLiteral>(e)) { string_literal(s); return; }
        if (auto* thrown = dyn_cast<CXXThrowExpr>(e)) {
            if (const Expr* value = thrown->getSubExpr()) {
                if (!handle_target(value->getType())) fail(e, "only managed handle exceptions are supported");
                expr(value); op("throw", 1);
            } else {
                if (!catch_depth || finally_depth) fail(e, "rethrow requires a managed catch handler");
                op("rethrow");
            }
            return;
        }
        if (isa<CXXNullPtrLiteralExpr>(e)) { op("ldnull", 0, 1); return; }
        if (isa<CXXThisExpr>(e)) {
            auto* method = dyn_cast<CXXMethodDecl>(current);
            if (!method || method->isStatic()) fail(e, "this outside a managed instance method");
            if (record_annotated(method->getParent(), "cli.value")) fail(e, "value-class this is a managed address, not a boxed handle");
            op("ldarg.0", 0, 1); return;
        }
        if (auto* member = dyn_cast<MemberExpr>(e)) {
            auto* field = dyn_cast<FieldDecl>(member->getMemberDecl());
            if (!field) fail(e, "unsupported managed member expression");
            if (native_field(field)) { native_field_address(member); native_field_load(field); return; }
            emit_receiver(member->getBase()); op("ldfld " + field_signature(field), 1, 1); return;
        }
        if (auto* d = dyn_cast<DeclRefExpr>(e)) { load_decl(d->getDecl(), e); return; }
        if (auto* opaque = dyn_cast<OpaqueValueExpr>(e)) {
            auto found = opaque_values.find(opaque);
            if (found == opaque_values.end()) fail(e, "unbound opaque property value");
            op("ldloc " + std::to_string(found->second), 0, 1); return;
        }
        if (auto* pseudo = dyn_cast<PseudoObjectExpr>(e)) {
            const Expr* result_expr = pseudo->getResultExpr();
            unsigned result = 0; bool has_result = false;
            for (const Expr* semantic : pseudo->semantics()) {
                if (auto* opaque = dyn_cast<OpaqueValueExpr>(semantic)) {
                    unsigned slot = temporary(expression_type(opaque));
                    expr(opaque->getSourceExpr()); op("stloc " + std::to_string(slot), 1);
                    opaque_values[opaque] = slot;
                    if (semantic == result_expr) { result = slot; has_result = true; }
                } else {
                    expr(semantic);
                    if (!semantic->getType()->isVoidType()) {
                        if (semantic == result_expr) {
                            result = temporary(type(semantic->getType()));
                            op("stloc " + std::to_string(result), 1); has_result = true;
                        } else op("pop", 1);
                    }
                }
            }
            if (!pseudo->getType()->isVoidType()) {
                if (!has_result) fail(e, "property expression lacks a result");
                op("ldloc " + std::to_string(result), 0, 1);
            }
            return;
        }
        if (auto* s = dyn_cast<SubstNonTypeTemplateParmExpr>(e)) { expr(s->getReplacement()); return; }
        if (auto* d = dyn_cast<CXXDefaultArgExpr>(e)) { expr(d->getExpr()); return; }
        if (auto* d = dyn_cast<CXXDefaultInitExpr>(e)) { expr(d->getExpr()); return; }
        if (auto* b = dyn_cast<BinaryOperator>(e)) { binary(b); return; }
        if (auto* conditional = dyn_cast<ConditionalOperator>(e)) {
            int base = stack; auto otherwise = label(), done = label();
            expr(conditional->getCond()); op("brfalse " + otherwise, 1);
            bool native = !native_value_pointer(conditional).isNull();
            auto branch_value = [&](const Expr* branch) {
                if (native && branch->isNullPointerConstant(context, Expr::NPC_ValueDependentIsNotNull)) { op("ldc.i4.0", 0, 1); op("conv.u", 1, 1); }
                else expr(branch);
            };
            branch_value(conditional->getTrueExpr());
            if (stack != base + 1) fail(e, "conditional operand did not produce one value");
            op("br " + done); stack = base; code << otherwise << ":\n";
            branch_value(conditional->getFalseExpr());
            if (stack != base + 1) fail(e, "conditional branch stack mismatch");
            code << done << ":\n"; return;
        }
        if (auto* init = dyn_cast<InitListExpr>(e); init && native_pod(init->getType())) {
            auto* semantic = init->isSemanticForm() ? init : init->getSemanticForm();
            if (!semantic) fail(init, "native POD initializer lacks semantic field mapping");
            auto* record = init->getType()->getAsCXXRecordDecl()->getDefinition();
            unsigned slot = temporary(type(init->getType()));
            op("ldloca " + std::to_string(slot), 0, 1); op("initobj " + type(init->getType()), 1);
            unsigned index = 0;
            for (auto* field : record->fields()) {
                if (record->isUnion() && field != semantic->getInitializedFieldInUnion()) continue;
                if (field->isUnnamedBitField()) continue;
                if (index >= semantic->getNumInits()) break;
                auto* value = semantic->getInit(index++);
                if (isa<ImplicitValueInitExpr>(value)) continue; // initobj already zeroed this field.
                auto offset = native_field_offset(field, init);
                op("ldloca " + std::to_string(slot), 0, 1); op("conv.u", 1, 1); native_offset(offset);
                expr(value); native_field_store(field);
            }
            op("ldloc " + std::to_string(slot), 0, 1); return;
        }
        if (auto* c = dyn_cast<CallExpr>(e)) { call(c); return; }
        if (auto* c = dyn_cast<CXXConstructExpr>(e)) {
            if (native_pod(c->getType())) {
                if (!c->getConstructor()->isTrivial()) fail(c, "native POD storage cannot execute a nontrivial native constructor");
                if (c->getConstructor()->isCopyOrMoveConstructor() && c->getNumArgs() == 1) { expr(c->getArg(0)); return; }
                if (c->getNumArgs()) fail(c, "native POD storage only supports trivial default/copy construction");
                unsigned slot = temporary(type(c->getType()));
                op("ldloca " + std::to_string(slot), 0, 1); op("initobj " + type(c->getType()), 1);
                op("ldloc " + std::to_string(slot), 0, 1); return;
            }
            if (!interior_element(c->getType()).isNull()) {
                if (!c->getNumArgs() && pinned(c->getType())) { op("ldc.i4.0", 0, 1); op("conv.u", 1, 1); return; }
                if (c->getNumArgs() != 1) fail(c, "managed pointer construction requires one address");
                auto* arg = strip(c->getArg(0))->IgnoreParenImpCasts();
                if (isa<CXXNullPtrLiteralExpr>(arg)) { op("ldc.i4.0", 0, 1); op("conv.u", 1, 1); }
                else if (!interior_element(arg->getType()).isNull()) expr(arg);
                else if (auto* unary = dyn_cast<UnaryOperator>(arg); unary && unary->getOpcode() == UO_AddrOf) address(unary->getSubExpr());
                else fail(c, "pin_ptr/interior_ptr requires a managed address, not an untracked native pointer");
                return;
            }
            if (value_target(c->getType())) {
                if (annotated(c->getConstructor(), "cli.value.zero")) {
                    unsigned slot = temporary(type(c->getType()));
                    op("ldloca " + std::to_string(slot), 0, 1); op("initobj " + record_token(value_target(c->getType())), 1);
                    op("ldloc " + std::to_string(slot), 0, 1); return;
                }
                if (c->getConstructor()->isCopyOrMoveConstructor()) {
                    if (c->getNumArgs() != 1) fail(c, "unsupported value copy");
                    expr(c->getArg(0)); return;
                }
                for (auto* arg : c->arguments()) expr(arg);
                op("newobj " + signature(c->getConstructor()), c->getNumArgs(), 1); return;
            }
            auto* target = handle_target(c->getType());
            if (!target) fail(c, "native construction cannot be emitted as a managed object");
            if (c->getNumArgs() == 0) { op("ldnull", 0, 1); return; }
            if (c->getNumArgs() != 1) fail(c, "unsupported handle construction");
            const Expr* arg = c->getArg(0);
            const Expr* stripped = arg->IgnoreParenImpCasts();
            if (isa<StringLiteral>(stripped) && record_name(target) != "[System.Runtime]'System.String'")
                fail(c, "string literals can only initialize String handles");
            if (auto* other = handle_target(arg->getType()))
                if (record_name(target) != record_name(other) && !other->isDerivedFrom(target)) fail(c, "invalid managed handle conversion");
            expr(arg); return;
        }
        if (auto* u = dyn_cast<UnaryOperator>(e)) {
            if (u->getOpcode() == UO_AddrOf && u->getType()->isPointerType() && !handle_target(u->getType())) {
                if (auto* member = dyn_cast<MemberExpr>(u->getSubExpr()->IgnoreParenImpCasts()); member && native_field(dyn_cast<FieldDecl>(member->getMemberDecl()))) {
                    native_field_address(member); return;
                }
                auto* reference = dyn_cast<DeclRefExpr>(u->getSubExpr()->IgnoreParenImpCasts());
                auto* variable = reference ? dyn_cast<VarDecl>(reference->getDecl()) : nullptr;
                auto element = u->getSubExpr()->getType();
                if (variable && !annotation(variable, "cli.import.constant:").empty())
                    fail(u, "imported literals have no addressable CLR storage");
                if (variable && variable->hasLocalStorage() &&
                    (element->isArithmeticType() || native_pod(element) || (element->isPointerType() && !handle_target(element)))) {
                    address(u->getSubExpr()); op("conv.u", 1, 1); return;
                }
            }
            if (u->getOpcode() == UO_Deref && boxed_target(u->getSubExpr()->getType())) {
                auto value_type = "valuetype " + record_name(boxed_target(u->getSubExpr()->getType()));
                if (isa<CXXThisExpr>(u->getSubExpr()->IgnoreParenImpCasts())) { op("ldarg.0", 0, 1); op("ldobj " + value_type, 1, 1); }
                else { expr(u->getSubExpr()); op("unbox.any " + value_type, 1, 1); }
                return;
            }
            if (u->getOpcode() == UO_Deref && !handle_target(u->getSubExpr()->getType())) {
                expr(u->getSubExpr()); op("ldobj " + type(u->getType()), 1, 1); return;
            }
            if (u->isIncrementDecrementOp()) {
                if (handle_target(u->getSubExpr()->getType())) fail(u, "managed handles cannot be incremented");
                if (u->getSubExpr()->getType()->isPointerType()) fail(u, "unmanaged pointer arithmetic and ordering are not enabled yet");
                if (auto* subscript = array_subscript(u->getSubExpr())) {
                    auto slots = capture_array(subscript); load_array_location(slots);
                    std::string element = type(subscript->getType());
                    op("ldelem " + element, 2, 1);
                    unsigned old = temporary(element), result = temporary(element);
                    op("stloc " + std::to_string(old), 1); op("ldloc " + std::to_string(old), 0, 1);
                    op(element == "int64" || element == "uint64" ? "ldc.i8 1" : element == "float32" ? "ldc.r4 1" : element == "float64" ? "ldc.r8 1" : "ldc.i4.1", 0, 1);
                    op(u->isIncrementOp() ? "add" : "sub", 2, 1); op("stloc " + std::to_string(result), 1);
                    load_array_location(slots); op("ldloc " + std::to_string(result), 0, 1); op("stelem " + element, 3);
                    op("ldloc " + std::to_string(u->isPostfix() ? old : result), 0, 1); return;
                }
                if (auto* member = dyn_cast<MemberExpr>(u->getSubExpr()->IgnoreParenImpCasts())) {
                    auto* field = dyn_cast<FieldDecl>(member->getMemberDecl());
                    if (!field) fail(u, "increment target is not an instance field");
                    if (native_field(field)) {
                        auto ft = type(field->getType());
                        unsigned pointer = temporary("native unsigned int"), old = temporary(ft), result = temporary(ft);
                        native_field_address(member); op("stloc " + std::to_string(pointer), 1);
                        op("ldloc " + std::to_string(pointer), 0, 1); native_field_load(field); op("stloc " + std::to_string(old), 1);
                        op("ldloc " + std::to_string(old), 0, 1);
                        op(ft == "int64" || ft == "uint64" ? "ldc.i8 1" : ft == "float32" ? "ldc.r4 1" : ft == "float64" ? "ldc.r8 1" : "ldc.i4.1", 0, 1);
                        op(u->isIncrementOp() ? "add" : "sub", 2, 1); op("stloc " + std::to_string(result), 1);
                        op("ldloc " + std::to_string(pointer), 0, 1); op("ldloc " + std::to_string(result), 0, 1); native_field_store(field);
                        op("ldloc " + std::to_string(u->isPostfix() ? old : result), 0, 1); return;
                    }
                    unsigned receiver = temporary(native_value_pointer(member->getBase()).isNull() ? receiver_type(field) : expression_type(member->getBase()));
                    unsigned old = temporary(type(field->getType())), result = temporary(type(field->getType()));
                    emit_receiver(member->getBase()); op("stloc " + std::to_string(receiver), 1);
                    op("ldloc " + std::to_string(receiver), 0, 1); op("ldfld " + field_signature(field), 1, 1);
                    op("stloc " + std::to_string(old), 1); op("ldloc " + std::to_string(old), 0, 1);
                    std::string ft = type(field->getType());
                    op(ft == "int64" || ft == "uint64" ? "ldc.i8 1" : ft == "float32" ? "ldc.r4 1" : ft == "float64" ? "ldc.r8 1" : "ldc.i4.1", 0, 1);
                    op(u->isIncrementOp() ? "add" : "sub", 2, 1); op("stloc " + std::to_string(result), 1);
                    op("ldloc " + std::to_string(receiver), 0, 1); op("ldloc " + std::to_string(result), 0, 1);
                    op("stfld " + field_signature(field), 2); op("ldloc " + std::to_string(u->isPostfix() ? old : result), 0, 1);
                    return;
                }
                expr(u->getSubExpr());
                if (u->isPostfix()) op("dup", 1, 2);
                std::string increment_type = type(u->getSubExpr()->getType());
                op(increment_type == "int64" || increment_type == "uint64" ? "ldc.i8 1" : increment_type == "float32" ? "ldc.r4 1" : increment_type == "float64" ? "ldc.r8 1" : "ldc.i4.1", 0, 1);
                op(u->isIncrementOp() ? "add" : "sub", 2, 1);
                store(u->getSubExpr(), !u->isPostfix()); return;
            }
            expr(u->getSubExpr());
            if (u->getOpcode() == UO_Minus) op("neg", 1, 1);
            else if (u->getOpcode() == UO_Not) op("not", 1, 1);
            else if (u->getOpcode() == UO_LNot) { op("ldc.i4.0", 0, 1); op("ceq", 2, 1); }
            else if (u->getOpcode() != UO_Plus) fail(u, "unsupported unary operator");
            return;
        }
        if (auto* c = dyn_cast<ExplicitCastExpr>(e)) {
            if (c->getCastKind() == CK_IntegralToPointer && !handle_target(c->getType())) {
                expr(c->getSubExpr());
                if (context.getTypeSize(c->getSubExpr()->getType()) < context.getTypeSize(c->getType()))
                    op(c->getSubExpr()->getType()->isUnsignedIntegerType() ? "conv.u8" : "conv.i8", 1, 1);
                op("conv.u", 1, 1); return;
            }
            if (c->getCastKind() == CK_PointerToIntegral && (!handle_target(c->getSubExpr()->getType()) || !native_value_pointer(c->getSubExpr()).isNull())) {
                expr(c->getSubExpr()); numeric_conversion(c->getType()); return;
            }
            if (c->getCastKind() == CK_ConstructorConversion && value_target(c->getType())) { expr(c->getSubExpr()); return; }
            if (c->getCastKind() == CK_UserDefinedConversion && c->getType()->isPointerType() && !handle_target(c->getType())) {
                auto* conversion = dyn_cast<CXXMemberCallExpr>(strip(c->getSubExpr()));
                auto* method = conversion ? dyn_cast_or_null<CXXConversionDecl>(conversion->getDirectCallee()) : nullptr;
                if (!method || !record_annotated(method->getParent(), "cli.pin")) fail(c, "explicit native pointer conversion requires a pin_ptr intrinsic");
                expr(c->getSubExpr()); return;
            }
            if (c->getCastKind() == CK_BitCast && c->getType()->isPointerType() && !handle_target(c->getType()) &&
                c->getSubExpr()->getType()->isPointerType() && (!handle_target(c->getSubExpr()->getType()) || !native_value_pointer(c->getSubExpr()).isNull())) { expr(c->getSubExpr()); return; }
            if (handle_target(c->getType())) {
                const Expr* source = strip(c->getSubExpr());
                if (auto* ctor = dyn_cast<CXXConstructExpr>(source)) {
                    if (ctor->getNumArgs() != 1) fail(c, "invalid explicit handle cast");
                    source = ctor->getArg(0);
                }
                if (!handle_target(source->getType())) fail(c, "explicit handle cast requires a managed reference");
                if (isa<CXXReinterpretCastExpr>(c)) fail(c, "reinterpret_cast of managed handles is unsupported");
                expr(source); op(std::string(isa<CXXDynamicCastExpr>(c) ? "isinst " : "castclass ") + reference_token(c->getType()), 1, 1); return;
            }
            if (c->getCastKind() == CK_IntegralCast || c->getCastKind() == CK_IntegralToFloating || c->getCastKind() == CK_FloatingCast || c->getCastKind() == CK_FloatingToIntegral) {
                expr(c->getSubExpr()); numeric_conversion(c->getType()); return;
            }
            if (c->getCastKind() != CK_NoOp) fail(c, "explicit conversions are not enabled yet");
            expr(c->getSubExpr()); return;
        }
        fail(e, std::string("unsupported AST expression: ") + e->getStmtClassName());
    }
    bool finally_handler(const CXXCatchStmt* handler) const {
        auto* variable = handler->getExceptionDecl();
        return variable && variable->getType()->isPointerType() &&
            record_annotated(variable->getType()->getPointeeType()->getAsCXXRecordDecl(), "cli.finally");
    }
    void collect_locals(const Stmt* s) {
        if (!s) return;
        if (auto* handler = dyn_cast<CXXCatchStmt>(s)) if (!finally_handler(handler)) {
            if (auto* variable = handler->getExceptionDecl()) {
                if (!handle_target(variable->getType())) fail(s, "only managed handle catch types are supported");
                validate_declarator(variable->getTypeSourceInfo());
                locals.emplace(variable, locals.size());
            }
        }
        if (auto* ds = dyn_cast<DeclStmt>(s)) for (auto* d : ds->decls()) {
            auto* v = dyn_cast<VarDecl>(d);
            if (!v || v->isStaticLocal()) fail(s, "unsupported local declaration");
            validate_interop_attributes(v, 0);
            variable_type(v);
            validate_declarator(v->getTypeSourceInfo());
            locals.emplace(v, locals.size());
        }
        for (auto* child : s->children()) collect_locals(child);
    }
    void statement(const Stmt* s) {
        if (!s || isa<NullStmt>(s)) return;
        int before = stack;
        if (auto* block = dyn_cast<CompoundStmt>(s)) {
            std::vector<const VarDecl*> pins;
            for (auto* child : block->body()) if (auto* declarations = dyn_cast<DeclStmt>(child))
                for (auto* d : declarations->decls()) if (auto* v = dyn_cast<VarDecl>(d); v && pinned(v->getType())) pins.push_back(v);
            std::string done;
            if (!pins.empty()) { done = label(); code << "    .try {\n"; ++exception_depth; }
            for (auto* child : block->body()) statement(child);
            if (!pins.empty()) {
                op("leave " + done); --exception_depth;
                code << "    } finally {\n";
                for (auto* pin : pins) { op("ldc.i4.0", 0, 1); op("conv.u", 1, 1); op("stloc " + std::to_string(locals.at(pin)), 1); }
                op("endfinally"); code << "    }\n"; mark(done);
            }
        }
        else if (auto* d = dyn_cast<DeclStmt>(s)) {
            for (auto* decl : d->decls()) {
                auto* v = cast<VarDecl>(decl);
                // Every emitted method already uses .locals init, matching
                // WPF's /clr:initLocals: zero once on method entry. A declaration
                // without an initializer does not reset storage on loop re-entry.
                if (!v->hasInit()) continue;
                if (native_reference(v)) native_address(v->getInit());
                else if (v->getType()->isReferenceType()) address(v->getInit()); else pointer_value(v->getInit(), v); op("stloc " + std::to_string(locals.at(v)), 1);
            }
        } else if (auto* r = dyn_cast<ReturnStmt>(s)) {
            if (finally_depth) fail(s, "return cannot leave a finally handler");
            if (!interior_element(current->getReturnType()).isNull() &&
                (!r->getRetValue() || !heap_interior_return(r->getRetValue())))
                fail(s, "interior pointer return must refer directly to managed heap storage; local aliases and stack addresses cannot escape");
            if (r->getRetValue()) pointer_value(r->getRetValue(), current);
            if (exception_depth) {
                if (return_label.empty()) return_label = label();
                if (r->getRetValue()) {
                    if (!return_slot_set) { return_slot = temporary(declared_type(current)); return_slot_set = true; }
                    op("stloc " + std::to_string(return_slot), 1);
                }
                op("leave " + return_label);
            } else op("ret", current->getReturnType()->isVoidType() ? 0 : 1);
        } else if (auto* region = dyn_cast<CXXTryStmt>(s)) {
            auto done = label();
            code << "    .try {\n"; ++exception_depth;
            statement(region->getTryBlock()); op("leave " + done); --exception_depth;
            code << "    }\n";
            for (unsigned i = 0; i < region->getNumHandlers(); ++i) {
                auto* handler = region->getHandler(i);
                bool final = finally_handler(handler);
                if (final && region->getNumHandlers() != 1) fail(s, "finally must wrap the catch region");
                auto* variable = handler->getExceptionDecl();
                code << (final ? "    finally {\n" : "    catch " + (variable ? reference_token(variable->getType()) : "[System.Runtime]System.Object") + " {\n");
                ++exception_depth;
                if (final) ++finally_depth;
                else {
                    ++catch_depth; stack = 1; max_stack = std::max(max_stack, 1u);
                    if (variable) op("stloc " + std::to_string(locals.at(variable)), 1);
                    else op("pop", 1);
                }
                statement(handler->getHandlerBlock());
                op(final ? "endfinally" : "leave " + done);
                if (final) --finally_depth; else --catch_depth;
                --exception_depth; code << "    }\n";
            }
            mark(done);
        } else if (auto* i = dyn_cast<IfStmt>(s)) {
            if (i->getInit() || i->getConditionVariable()) fail(s, "if initializer is not supported yet");
            auto otherwise = label(), done = label();
            expr(i->getCond()); op("brfalse " + otherwise, 1);
            statement(i->getThen()); op("br " + done); mark(otherwise);
            statement(i->getElse()); mark(done);
        } else if (auto* selection = dyn_cast<SwitchStmt>(s)) {
            if (selection->getInit() || selection->getConditionVariable()) fail(s, "switch initializer is not supported yet");
            auto done = label(), otherwise = done;
            unsigned slot = temporary(type(selection->getCond()->getType()));
            expr(selection->getCond()); op("stloc " + std::to_string(slot), 1);
            std::vector<const SwitchCase*> cases;
            for (auto* item = selection->getSwitchCaseList(); item; item = item->getNextSwitchCase()) {
                auto target = label(); cases.push_back(item);
                switch_labels.emplace(item, std::make_pair(target, exception_depth));
                if (isa<DefaultStmt>(item)) { otherwise = target; continue; }
                auto* branch = cast<CaseStmt>(item);
                if (branch->getRHS()) fail(item, "GNU case ranges are not supported");
                Expr::EvalResult value;
                if (!branch->getLHS()->EvaluateAsInt(value, context)) fail(item, "case label must be an integer constant");
                auto integer = value.Val.getInt();
                unsigned bits = context.getTypeSize(selection->getCond()->getType()) > 32 ? 64 : 32;
                auto constant = integer.isSigned() ? integer.sextOrTrunc(bits) : integer.zextOrTrunc(bits);
                op("ldloc " + std::to_string(slot), 0, 1);
                op(std::string(bits == 64 ? "ldc.i8 " : "ldc.i4 ") + std::to_string(constant.getSExtValue()), 0, 1);
                op("beq " + target, 2);
            }
            op("br " + otherwise);
            control_labels.push_back({done, "", exception_depth});
            statement(selection->getBody()); control_labels.pop_back();
            for (auto* item : cases) switch_labels.erase(item);
            mark(done);
        } else if (auto* branch = dyn_cast<SwitchCase>(s)) {
            auto found = switch_labels.find(branch);
            if (found == switch_labels.end()) fail(s, "case label has no active switch");
            if (exception_depth != found->second.second) fail(s, "switch labels cannot enter a protected or pinned scope");
            mark(found->second.first);
            statement(branch->getSubStmt());
        } else if (auto* w = dyn_cast<WhileStmt>(s)) {
            if (w->getConditionVariable()) fail(s, "while condition variable is not supported yet");
            auto check = label(), done = label(); mark(check);
            expr(w->getCond()); op("brfalse " + done, 1);
            control_labels.push_back({done, check, exception_depth}); statement(w->getBody()); control_labels.pop_back();
            op("br " + check); mark(done);
        } else if (auto* f = dyn_cast<ForStmt>(s)) {
            if (f->getConditionVariable()) fail(s, "for condition variable is not supported yet");
            statement(f->getInit()); auto check = label(), inc = label(), done = label(); mark(check);
            if (f->getCond()) { expr(f->getCond()); op("brfalse " + done, 1); }
            control_labels.push_back({done, inc, exception_depth}); statement(f->getBody()); control_labels.pop_back();
            mark(inc); statement(f->getInc()); op("br " + check); mark(done);
        } else if (isa<BreakStmt>(s) || isa<ContinueStmt>(s)) {
            auto target_it = control_labels.rbegin();
            if (isa<ContinueStmt>(s)) while (target_it != control_labels.rend() && target_it->next.empty()) ++target_it;
            if (target_it == control_labels.rend()) fail(s, "break/continue outside a loop or switch");
            auto& target = *target_it;
            if (finally_depth && exception_depth > target.depth) fail(s, "loop control cannot leave a finally handler");
            op(std::string(exception_depth > target.depth ? "leave " : "br ") + (isa<BreakStmt>(s) ? target.stop : target.next));
        } else if (auto* e = dyn_cast<Expr>(s)) {
            expr(e); if (!e->getType()->isVoidType()) op("pop", 1);
        } else fail(s, std::string("unsupported AST statement: ") + s->getStmtClassName());
        if (stack != before) fail(s, "internal IL stack mismatch at statement boundary");
    }
    bool native_function(const FunctionDecl* f) const {
        return selected_native_imports.count(f->getCanonicalDecl()) ||
            (!native_library.empty() && f->isExternC() && !isa<CXXMethodDecl>(f) && !f->hasBody() && f->isUsed() &&
            !ci.getSourceManager().isInSystemHeader(f->getLocation()));
    }
    void validate_native_signature(const FunctionDecl* f) const {
        auto valid = [&](const ValueDecl* declaration, QualType t, bool result) {
            if (result && t->isVoidType()) return;
            if ((!t->isIntegerType() && !t->isRealFloatingType() && !t->isPointerType()) || t->isBooleanType() || (handle_target(t) && native_value_pointer(declaration).isNull()))
                throw std::runtime_error("native bridge signatures require scalars or unmanaged pointers; bool and managed/pointer marshalling are not enabled");
            declared_type(declaration);
        };
        valid(f, f->getReturnType(), true);
        for (auto* parameter : f->parameters()) valid(parameter, parameter->getType(), false);
        auto convention = f->getType()->castAs<FunctionType>()->getCallConv();
        if (f->isVariadic() || f->hasAttr<AsmLabelAttr>() || (convention != CC_C && convention != CC_Win64))
            throw std::runtime_error("native bridge requires a non-variadic extern-C function using the platform C ABI");
    }
    FunctionIR function(FunctionDecl* f) {
        if (pinned(f->getReturnType())) throw std::runtime_error("pin_ptr is restricted to block-scoped locals");
        for (auto* p : f->parameters()) if (pinned(p->getType())) throw std::runtime_error("pin_ptr is restricted to block-scoped locals");
        if (native_function(f)) {
            validate_native_signature(f);
            return {method_name(f), "", declared_type(f), false, 0, "", ""};
        }
        current = f; locals.clear(); parameters.clear(); temporary_types.clear(); opaque_values.clear(); code.str(""); code.clear(); stack = 0; max_stack = 0;
        label_count = 0; control_labels.clear(); switch_labels.clear(); exception_depth = catch_depth = finally_depth = 0; return_label.clear(); return_slot_set = false;
        auto* method = dyn_cast<CXXMethodDecl>(f);
        bool instance = method && !method->isStatic();
        if (method && method->isPureVirtual()) return {method_name(f), record_name(method->getParent()), declared_type(f), instance, 0, "", ""};
        if (f->isVariadic()) throw std::runtime_error("variadic managed functions are unsupported");
        for (unsigned i = 0; i < f->getNumParams(); ++i) parameters[f->getParamDecl(i)] = i + unsigned(instance);
        collect_locals(f->getBody());
        std::set<const VarDecl*> block_pins;
        std::function<void(const Stmt*)> find_pins = [&](const Stmt* s) {
            if (!s) return;
            if (auto* block = dyn_cast<CompoundStmt>(s)) for (auto* child : block->body())
                if (auto* declarations = dyn_cast<DeclStmt>(child)) for (auto* d : declarations->decls())
                    if (auto* v = dyn_cast<VarDecl>(d); v && pinned(v->getType())) block_pins.insert(v);
            for (auto* child : s->children()) find_pins(child);
        };
        find_pins(f->getBody());
        for (auto& entry : locals) if (pinned(entry.first->getType()) && !block_pins.count(entry.first))
            fail(f->getBody(), "pin_ptr is restricted to block-scoped locals");
        if (auto* constructor = dyn_cast<CXXConstructorDecl>(f)) {
            bool base_initialized = false;
            for (auto* init : constructor->inits()) {
                if (init->isBaseInitializer() && !is_interface(init->getBaseClass()->getAsCXXRecordDecl())) {
                    auto* construct = dyn_cast<CXXConstructExpr>(init->getInit());
                    if (!construct) fail(init->getInit(), "unsupported managed base initializer");
                    op("ldarg.0", 0, 1);
                    for (auto* arg : construct->arguments()) expr(arg);
                    op("call " + signature(construct->getConstructor()), construct->getNumArgs() + 1);
                    base_initialized = true;
                }
            }
            if (record_annotated(method->getParent(), "cli.value")) {
                op("ldarg.0", 0, 1); op("initobj " + record_token(method->getParent()), 1);
            } else if (!base_initialized) { op("ldarg.0", 0, 1); op("call instance void [System.Runtime]System.Object::.ctor()", 1); }
            for (auto* init : constructor->inits()) if (init->isMemberInitializer()) {
                op("ldarg.0", 0, 1); pointer_value(init->getInit(), init->getMember()); op("stfld " + field_signature(init->getMember()), 2);
            }
        }
        statement(f->getBody());
        if (f->getReturnType()->isVoidType()) op("ret");
        else if (value_target(f->getReturnType()) || native_pod(f->getReturnType())) {
            unsigned slot = temporary(type(f->getReturnType()));
            op("ldloca " + std::to_string(slot), 0, 1); op("initobj " + type(f->getReturnType()), 1);
            op("ldloc " + std::to_string(slot), 0, 1); op("ret", 1);
        } else {
            std::string result = type(f->getReturnType());
            op(result == "int64" || result == "uint64" ? "ldc.i8 0" : result == "float32" ? "ldc.r4 0" : result == "float64" ? "ldc.r8 0" :
               handle_target(f->getReturnType()) && native_value_pointer(f).isNull() ? "ldnull" : "ldc.i4.0", 0, 1);
            if (!native_value_pointer(f).isNull()) op("conv.u", 1, 1);
            if (!interior_element(f->getReturnType()).isNull()) op("conv.u", 1, 1);
            op("ret", 1);
        } // Unreachable for correctly-returning non-void functions except main.
        if (!return_label.empty()) {
            mark(return_label);
            if (return_slot_set) op("ldloc " + std::to_string(return_slot), 0, 1);
            op("ret", return_slot_set ? 1 : 0);
        }
        std::string declarations;
        std::vector<const VarDecl*> ordered(locals.size());
        for (auto& entry : locals) ordered[entry.second] = entry.first;
        for (unsigned i = 0; i < ordered.size(); ++i) {
            if (i) declarations += ", ";
            declarations += "[" + std::to_string(i) + "] " + variable_type(ordered[i]) + (pinned(ordered[i]->getType()) ? " pinned" : "") + " 'local" + std::to_string(i) + "'";
        }
        for (unsigned i = 0; i < temporary_types.size(); ++i) {
            if (!declarations.empty()) declarations += ", ";
            declarations += "[" + std::to_string(ordered.size() + i) + "] " + temporary_types[i] + " 'temporary" + std::to_string(i) + "'";
        }
        return {method_name(f), method ? record_name(method->getParent()) : "", declared_type(f), instance,
            std::max(1u, max_stack), declarations, code.str()};
    }
public:
    Emitter(CompilerInstance& compiler, const Metadata& md) : ci(compiler), context(ci.getASTContext()), metadata(md) {}
    bool VisitFunctionDecl(FunctionDecl* f) {
        if (f->isDependentContext() || f->getType()->isDependentType()) return true;
        auto* method = dyn_cast<CXXMethodDecl>(f);
        for (auto* previous = f->getPreviousDecl(); previous; previous = previous->getPreviousDecl()) {
            bool conflict = native_value_pointer(f).isNull() != native_value_pointer(previous).isNull();
            for (unsigned i = 0; i < f->getNumParams() && i < previous->getNumParams(); ++i)
                conflict |= native_value_pointer(f->getParamDecl(i)).isNull() != native_value_pointer(previous->getParamDecl(i)).isNull();
            if (conflict) throw std::runtime_error("native-pointer and boxed-handle signatures cannot share one canonical Clang declaration");
        }
        if (method && record_annotated(method->getParent(), "cli.delegate")) return true;
        if (ci.getSourceManager().isInSystemHeader(f->getLocation()) &&
            !(method && (record_annotated(method->getParent(), "cli.ref") || record_annotated(method->getParent(), "cli.value"))) && !native_function(f)) return true;
        validate_interop_attributes(f, 2);
        if (annotated(f, "cli.reliability")) reliability_contract(f);
        if (annotated(f, "cli.com.visible") && layout_constant(f, "cli.com.visible", -1) != 0 && layout_constant(f, "cli.com.visible", -1) != 1)
            throw std::runtime_error("ComVisibleAttribute requires a boolean value");
        if (annotated(f, "cli.cctor") && (!method || !method->isStatic() || method->getNumParams() || !method->getReturnType()->isVoidType() || is_interface(method->getParent())))
            throw std::runtime_error("invalid or unsupported managed static constructor");
        if (annotated(f, "cli.field.offset")) throw std::runtime_error("FieldOffset requires a managed instance field");
        if (annotated(f, "cli.value.zero")) return true;
        if (isa<CXXConstructorDecl>(f)) for (auto* parameter : f->parameters())
            if (parameter->getType()->isReferenceType() && !f->isImplicit()) throw std::runtime_error("tracking references in constructors are not enabled yet");
        if (isa<CXXConstructorDecl>(f) && method && is_interface(method->getParent())) return true;
        if (f->isImplicit() && method && annotated(method->getParent(), "cli.abstract") && annotated(method->getParent(), "cli.sealed")) return true;
        if ((f->isImplicit() && !(isa<CXXConstructorDecl>(f) && cast<CXXConstructorDecl>(f)->isDefaultConstructor())) || (!f->doesThisDeclarationHaveABody() && !(method && method->isPureVirtual()) && !native_function(f)) || llvm::StringRef(f->getNameAsString()).starts_with("__cli_")) return true;
        if (auto* m = dyn_cast<CXXMethodDecl>(f)) if (!record_annotation(m->getParent(), "cli.external:").empty()) return true;
        if (isa<CXXDestructorDecl>(f) && (!method || !record_annotated(method->getParent(), "cli.ref") || is_interface(method->getParent())))
            throw std::runtime_error("destructors currently require source ref classes; value/interface destructor lowering remains unsupported");
        if (method && f->isOverloadedOperator()) throw std::runtime_error("managed operator metadata is not enabled yet");
        if (method) named_overrides(method);
        if (method && !isa<CXXDestructorDecl>(f)) for (auto* base : method->overridden_methods()) {
            if (type(method->getReturnType()) != type(base->getReturnType())) throw std::runtime_error("covariant managed return metadata is not enabled yet");
            if (!is_interface(base->getParent()) && !method->hasAttr<OverrideAttr>()) throw std::runtime_error("managed class overrides require an explicit override specifier");
            if (is_interface(base->getParent()) && method->getAccess() != AS_public) throw std::runtime_error("implicit interface implementations must be public");
        }
        if (f->getReturnType()->isReferenceType()) throw std::runtime_error("returning tracking references is not enabled yet");
        if (pinned(f->getReturnType())) throw std::runtime_error("pin_ptr is restricted to block-scoped locals");
        validate_declarator(f->getTypeSourceInfo());
        for (auto* p : f->parameters()) validate_declarator(p->getTypeSourceInfo());
        if (f->getType()->isDependentType() || f->getTemplatedKind() == FunctionDecl::TK_FunctionTemplate)
            throw std::runtime_error("unspecialized dependent function reached IL emission");
        if (seen.insert(f->getCanonicalDecl()).second) functions.push_back(f);
        return true;
    }
    bool VisitCXXRecordDecl(CXXRecordDecl* r) {
        if (r->isDependentContext()) return true;
        if (r->isThisDeclarationADefinition() && !seen_records.insert(r->getCanonicalDecl()).second) return true;
        if (r->isThisDeclarationADefinition() && (annotated(r, "cli.ref") || annotated(r, "cli.value")) && !annotated(r, "cli.array") && annotation(r,"cli.external:").empty()) {
            if (auto* invoke = delegate_invoke(r)) {
                for (auto* attr : r->specific_attrs<AnnotateAttr>()) if (attr->getAnnotation().starts_with("cli.com."))
                    throw std::runtime_error("interop attributes on delegate declarations are not enabled yet");
                if (isa<CXXRecordDecl>(r->getDeclContext()) || isa<ClassTemplateSpecializationDecl>(r)) throw std::runtime_error("nested/generic delegate declarations are not enabled yet");
                if (invoke->isVariadic() || invoke->getReturnType()->isReferenceType()) throw std::runtime_error("variadic/byref-return delegates are not enabled yet");
                type(invoke->getReturnType()); validate_declarator(invoke->getTypeSourceInfo());
                for (auto* p : invoke->parameters()) { parameter_signature(p); validate_declarator(p->getTypeSourceInfo()); }
                records.push_back(r); return true;
            }
            validate_interop_attributes(r, 1);
            guid_attribute(r);
            if (annotated(r, "cli.com.import") && (!is_interface(r) || guid_attribute(r).empty()))
                throw std::runtime_error("ComImport currently requires an interface with a GuidAttribute");
            if (annotated(r, "cli.com.interface")) {
                auto mode = layout_constant(r, "cli.com.interface", -1);
                if (!is_interface(r) || mode < 0 || mode > 3) throw std::runtime_error("invalid InterfaceTypeAttribute target or value");
            }
            if (annotated(r, "cli.com.class")) {
                auto mode = layout_constant(r, "cli.com.class", -1);
                if (is_interface(r) || record_annotated(r, "cli.value") || mode < 0 || mode > 2) throw std::runtime_error("invalid ClassInterfaceAttribute target or value");
            }
            if (annotated(r, "cli.com.visible")) {
                auto value = layout_constant(r, "cli.com.visible", -1);
                if (value != 0 && value != 1) throw std::runtime_error("ComVisibleAttribute requires a boolean value");
            }
            if (!is_interface(r) && !annotated(r, "cli.abstract") && !r->isAbstract()) {
                std::set<const CXXRecordDecl*> checked;
                std::function<void(const CXXRecordDecl*)> validate_slots = [&](const CXXRecordDecl* base) {
                    if (!base || !checked.insert(base->getCanonicalDecl()).second) return;
                    // An imported CLR class supplies its own interface table;
                    // omitted facade members do not make that real class abstract.
                    if (!is_interface(base) && !record_annotation(base, "cli.external:").empty()) return;
                    for (auto* expected : base->methods()) if (is_interface(base)) {
                        auto imported = metadata.methods.find(annotation(expected, "cli.import:"));
                        if (imported == metadata.methods.end() || !imported->second.is_abstract || imported->second.is_static) continue;
                        bool implemented = false;
                        std::function<void(const CXXRecordDecl*)> find_implementation = [&](const CXXRecordDecl* owner) {
                            if (source_destructor(owner) && imported->second.owner == "System.IDisposable" &&
                                imported->second.name == "Dispose" && expected->getNumParams() == 0) implemented = true;
                            for (auto* method : owner->methods()) for (auto* target : named_overrides(method))
                                implemented |= target->getCanonicalDecl() == expected->getCanonicalDecl();
                            for (auto* method : owner->methods()) if (method->getNameAsString() == imported->second.name && method->isVirtual() &&
                                method->getAccess() == AS_public && method->getNumParams() == expected->getNumParams() && type(method->getReturnType()) == type(expected->getReturnType())) {
                                bool match = true;
                                for (unsigned i = 0; i < method->getNumParams(); ++i) match &= type(method->getParamDecl(i)->getType()) == type(expected->getParamDecl(i)->getType());
                                implemented |= match;
                            }
                            for (auto& parent : owner->bases()) if (!is_interface(parent.getType()->getAsCXXRecordDecl())) find_implementation(parent.getType()->getAsCXXRecordDecl());
                        };
                        find_implementation(r);
                        if (!implemented) throw std::runtime_error("concrete managed class is missing interface slot: " + imported->second.owner + "::" + imported->second.name + " in " + r->getQualifiedNameAsString());
                    }
                    for (auto& parent : base->bases()) validate_slots(parent.getType()->getAsCXXRecordDecl());
                };
                for (auto& base : r->bases()) validate_slots(base.getType()->getAsCXXRecordDecl());
            }
            auto layout = layout_kind(r);
            if (auto* destructor = source_destructor(r)) {
                if (!record_annotated(r, "cli.ref") || is_interface(r)) throw std::runtime_error("destructors currently require source ref classes");
                if (!destructor->hasBody() || destructor->isDefaulted()) throw std::runtime_error("managed destructor requires an explicit source body");
                for (auto* method : r->methods()) if (method->getNameAsString() == "Dispose" || method->getNameAsString() == "Finalize")
                    throw std::runtime_error("managed destructor conflicts with an explicit Dispose/Finalize declaration");
            }
            auto pack = layout_constant(r, "cli.layout.Pack", 0), size = layout_constant(r, "cli.layout.Size", 0);
            auto charset = layout_constant(r, "cli.layout.CharSet", 1);
            if (pack < 0 || pack > 128 || (pack && (pack & (pack-1)))) throw std::runtime_error("StructLayout Pack must be zero or a power of two up to 128");
            if (size < 0 || size > INT32_MAX) throw std::runtime_error("StructLayout Size must be a nonnegative Int32");
            if (charset < 1 || charset > 4) throw std::runtime_error("invalid StructLayout CharSet");
            if (layout != 3) {
                if (is_interface(r)) throw std::runtime_error("layout on interfaces is not enabled yet");
                for (auto& base : r->bases()) if (!is_interface(base.getType()->getAsCXXRecordDecl()) &&
                    record_name(base.getType()->getAsCXXRecordDecl()) != "[System.Runtime]'System.Object'")
                    throw std::runtime_error("layout on inherited managed classes is not enabled yet");
            }
            for (auto* field : r->fields()) {
                validate_interop_attributes(field, 0);
                auto offset = layout_constant(field, "cli.field.offset", -1);
                if (layout == 2 && !annotated(field, "cli.field.offset")) throw std::runtime_error("explicit layout requires FieldOffset on every instance field");
                if (annotated(field, "cli.field.offset") && (layout != 2 || offset < 0 || offset > INT32_MAX)) throw std::runtime_error("FieldOffset requires explicit layout and a nonnegative Int32 offset");
                if (layout != 3 && field->isBitField()) throw std::runtime_error("managed layout bitfields are not enabled yet");
            }
            for (auto* member : r->decls()) if (auto* field = dyn_cast<VarDecl>(member); field && annotated(field, "cli.field.offset"))
                throw std::runtime_error("FieldOffset requires a managed instance field");
            for (auto* member : r->decls()) if (auto* property = dyn_cast<MSPropertyDecl>(member))
                if (!native_value_pointer(property->getType(), property->getTypeSourceInfo()).isNull())
                    throw std::runtime_error("native value-pointer properties are not supported yet");
            // A CLR property must name actual accessor methods even when C++
            // never calls it. Instantiate those closed-template definitions
            // before collecting method bodies, rather than emitting dangling
            // property references for otherwise unused accessors.
            if (isa<ClassTemplateSpecializationDecl>(r)) {
                for (auto* member : r->decls()) if (auto* property = dyn_cast<MSPropertyDecl>(member)) {
                    for (auto* method : r->methods()) {
                        auto* name = method->getIdentifier();
                        if (!name || (name != property->getGetterId() && name != property->getSetterId()) ||
                            method->hasBody() || method->isPureVirtual() || !annotation(method, "cli.import:").empty()) continue;
                        ci.getSema().InstantiateFunctionDefinition(method->getLocation(), method, true, true);
                        if (ci.getDiagnostics().hasErrorOccurred()) throw std::runtime_error("Clang rejected a closed managed template property definition");
                    }
                }
            }
            for (auto* method : r->methods()) if (annotated(method, "cli.cctor") && !method->hasBody())
                throw std::runtime_error("managed static constructor requires a definition in this translation unit");
            if (record_annotated(r, "cli.value")) {
                if (r->getNumBases() || r->isPolymorphic()) throw std::runtime_error("value types with bases or virtual methods are not enabled yet");
                for (auto* field : r->fields()) if (field->getType()->isPointerType() && !handle_target(field->getType())) throw std::runtime_error("native pointer fields in value types are not enabled yet");
            }
            unsigned classes = 0;
            for (auto& b : r->bases()) if (!is_interface(b.getType()->getAsCXXRecordDecl())) ++classes;
            if (classes > 1 || (is_interface(r) && classes)) throw std::runtime_error("a managed class permits one class base and interfaces only");
            if (is_interface(r) && !r->field_empty()) throw std::runtime_error("interface instance fields are forbidden");
            for (const auto& base : r->bases()) record_name(base.getType()->getAsCXXRecordDecl());
            for (auto* field : r->fields()) { if (field->getType()->isReferenceType() || !interior_element(field->getType()).isNull()) throw std::runtime_error("tracking reference and managed pointer fields are not enabled yet"); declared_type(field); validate_declarator(field->getTypeSourceInfo()); }
            if (!is_interface(r) && !(annotated(r, "cli.abstract") && annotated(r, "cli.sealed"))) {
                auto* ctor = ci.getSema().LookupDefaultConstructor(r);
                if (ctor && ctor->isImplicit() && !ctor->isDeleted() && !ctor->hasBody())
                    ci.getSema().DefineImplicitDefaultConstructor(r->getLocation(), ctor);
                if (ci.getDiagnostics().hasErrorOccurred()) throw std::runtime_error("Clang rejected an implicit managed constructor");
            }
            records.push_back(r);
        }
        return true;
    }
    void collect(DeclContext* scope) {
        for (auto* decl : scope->decls()) {
            if (!isa<FunctionDecl>(decl) && !isa<CXXRecordDecl>(decl)) validate_interop_attributes(decl, 0);
            if (auto* f = dyn_cast<FunctionDecl>(decl)) VisitFunctionDecl(f);
            else if (auto* r = dyn_cast<CXXRecordDecl>(decl)) {
                if (r->isThisDeclarationADefinition() && (record_annotated(r, "cli.ref") || record_annotated(r, "cli.value") || !record_annotation(r, "cli.external:").empty())) { VisitCXXRecordDecl(r); collect(r); }
            } else if (auto* ns = dyn_cast<NamespaceDecl>(decl)) collect(ns);
            else if (auto* linkage = dyn_cast<LinkageSpecDecl>(decl)) collect(linkage);
            else if (auto* tmpl = dyn_cast<FunctionTemplateDecl>(decl)) {
                if (llvm::StringRef(tmpl->getNameAsString()).starts_with("__cli_")) continue;
                for (auto* specialization : tmpl->specializations()) if (specialization->isUsed()) VisitFunctionDecl(specialization);
            }
            else if (auto* tmpl = dyn_cast<ClassTemplateDecl>(decl)) {
                // C++ managed templates emit closed ordinary CLR types.
                // Primary templates and uninstantiated members have no IL body.
                for (auto* specialization : tmpl->specializations())
                    if (!specialization->isDependentContext() && specialization->isThisDeclarationADefinition() &&
                        (record_annotated(specialization, "cli.ref") || record_annotated(specialization, "cli.value")) &&
                        record_annotation(specialization, "cli.external:").empty()) {
                        VisitCXXRecordDecl(specialization); collect(specialization);
                    }
            }
            else if (auto* enumeration = dyn_cast<EnumDecl>(decl)) {
                if (enumeration->isThisDeclarationADefinition() && annotated(enumeration, "cli.enum") && annotation(enumeration, "cli.external:").empty()) {
                    if (isa<CXXRecordDecl>(enumeration->getDeclContext())) throw std::runtime_error("nested managed enum metadata is not enabled yet");
                    enums.push_back(enumeration);
                }
            }
            else if (auto* alias = dyn_cast<TypedefNameDecl>(decl)) validate_declarator(alias->getTypeSourceInfo());
        }
    }
    std::string emit(const Options& options) {
        native_library = options.native_library;
        if (!options.native_imports.empty()) {
            auto json = llvm::json::parse(read_file(options.native_imports));
            auto* root = json ? json->getAsObject() : nullptr;
            if (!root || root->getInteger("version") != 1 || !root->getObject("imports")) throw std::runtime_error("invalid native import manifest");
            for (const auto& entry : *root->getObject("imports")) {
                auto* value = entry.second.getAsObject();
                auto library = value ? value->getString("library") : std::nullopt;
                auto symbol = value ? value->getString("entry") : std::nullopt;
                if (!library || !symbol) throw std::runtime_error("incomplete native import binding");
                native_imports.emplace(entry.first.str(), NativeImport{library->str(), symbol->str()});
            }
        }
        collect(context.getTranslationUnitDecl());
        bool entry = false;
        // Managed calls discover native imports while generating their bodies.
        // Index traversal keeps those appended declarations stable and emits them.
        for (size_t i = 0; i < functions.size(); ++i) {
            auto* f = functions[i];
            for (auto* parameter : f->parameters()) variable_type(parameter);
            if (f->getNameAsString() == "main") {
                if (isa<CXXMethodDecl>(f) || f->getNumParams() != 0 || type(f->getReturnType()) != "int32")
                    throw std::runtime_error("entry point must be global int main()");
                entry = true;
            }
            ir.push_back(function(f));
        }
        if (!entry && options.target == "exe") throw std::runtime_error("missing global int main() entry point");
        std::ostringstream il;
        il << metadata.externs << ".assembly '" << options.assembly << "' {";
        if (!options.public_key.empty()) {
            if (options.public_key.size() % 2 || options.public_key.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
                throw std::runtime_error("invalid assembly public key");
            il << " .publickey = (";
            for (size_t i = 0; i < options.public_key.size(); i += 2) il << options.public_key.substr(i, 2) << ' ';
            il << ") .hash algorithm 0x8004";
        }
        il << " }\n.module '" << options.assembly << ".dll'\n";
        if (!native_library.empty()) il << ".module extern '" << native_library << "'\n";
        std::set<std::string> system_libraries;
        for (auto* function : selected_native_imports) system_libraries.insert(native_imports.at(function->getNameAsString()).library);
        for (const auto& library : system_libraries) il << ".module extern '" << library << "'\n";
        for (const auto& entry : native_records) {
            const auto& layout = context.getASTRecordLayout(entry.second);
            il << ".class public sequential ansi sealed " << entry.first << " extends [System.Runtime]System.ValueType {\n"
               << "  .pack " << layout.getAlignment().getQuantity() << "\n  .size " << layout.getSize().getQuantity() << "\n"
               << "  .custom instance void [System.Runtime.CompilerServices.VisualC]System.Runtime.CompilerServices.NativeCppClassAttribute::.ctor() = (01 00 00 00)\n"
               << "  .field private uint" << layout.getAlignment().getQuantity() * 8 << " '__storage'\n}\n";
        }
        auto emit_static_initializers = [&](const CXXRecordDecl* r) {
            for (auto* method : r->methods()) {
                auto name = annotation(method, "cli.static.init:");
                if (name.empty()) continue;
                const VarDecl* field = nullptr;
                for (auto* member : r->decls()) if (auto* variable = dyn_cast<VarDecl>(member); variable && variable->isStaticDataMember() && variable->getNameAsString() == name) field = variable;
                if (!field || type(field->getType()) != type(method->getReturnType())) throw std::runtime_error("invalid static field initializer");
                il << "    call " << signature(method) << "\n    stsfld " << static_field_signature(field) << "\n";
            }
        };
        auto emit_function = [&](size_t i) {
            auto* f = functions[i]; auto& body = ir[i];
            auto* method = dyn_cast<CXXMethodDecl>(f);
            bool cctor = annotated(f, "cli.cctor");
            if (native_function(f)) {
                bool system = selected_native_imports.count(f->getCanonicalDecl());
                auto binding = system ? native_imports.at(f->getNameAsString()) : NativeImport{native_library, f->getNameAsString()};
                il << "  .method public hidebysig static pinvokeimpl(" << quote(binding.library) << " as " << quote(binding.entry)
                   << (system ? " winapi nomangle) " : " cdecl) ") << declared_type(f) << " '" << f->getNameAsString() << "'(";
                for (unsigned j = 0; j < f->getNumParams(); ++j) { if (j) il << ", "; il << parameter_signature(f->getParamDecl(j)); }
                il << ") cil managed preservesig {}\n"; return;
            }
            std::string flags;
            bool destructor = isa<CXXDestructorDecl>(f);
            if (method && method->isVirtual() && !destructor) {
                bool class_override = false;
                for (auto* base : method->overridden_methods()) if (!is_interface(base->getParent())) class_override = true;
                flags = "virtual " + std::string(class_override ? "" : "newslot ") + (method->isPureVirtual() ? "abstract " : "");
                if (method->hasAttr<FinalAttr>()) flags += "final ";
            }
            il << "  .method " << (destructor || cctor || (isa<CXXMethodDecl>(f) && f->getAccess() == AS_private) ? "private" : f->getAccess() == AS_protected ? "family" : annotation(f, "cli.access:") == "assembly" ? "assembly" : "public") << " hidebysig " << flags
               << (cctor || isa<CXXConstructorDecl>(f) ? "specialname rtspecialname " : !annotation(f, "cli.accessor:").empty() ? "specialname " : "") << (body.instance ? "instance " : "static ")
               << declared_type(f) << " '" << body.name << "'(";
            for (unsigned j = 0; j < f->getNumParams(); ++j) { if (j) il << ", "; il << parameter_signature(f->getParamDecl(j)); }
            il << ") cil managed " << (annotated(f, "cli.preservesig") ? "preservesig " : "") << "{\n";
            if (annotated(f, "cli.reliability")) {
                auto values = reliability_contract(f);
                il << "  .custom " << attribute_constructor("System.Runtime.ConstrainedExecution.ReliabilityContractAttribute", {
                    imported_value_type("System.Runtime.ConstrainedExecution.Consistency"), imported_value_type("System.Runtime.ConstrainedExecution.Cer")})
                   << " = (01 00 0" << values.first << " 00 00 00 0" << values.second << " 00 00 00 00 00)\n";
            }
            if (annotated(f, "cli.com.visible"))
                il << "  .custom " << attribute_constructor("System.Runtime.InteropServices.ComVisibleAttribute", {"bool"}) << " = (01 00 0" << layout_constant(f, "cli.com.visible", -1) << " 00 00)\n";
            if (f->getNameAsString() == "main" && body.owner.empty() && options.target == "exe") il << "    .entrypoint\n";
            if (method) for (auto* target : named_overrides(method)) il << "    .override method " << signature(target) << "\n";
            if (method && method->isPureVirtual()) { il << "  }\n"; return; }
            il << "    .maxstack " << body.max_stack << "\n";
            if (!body.locals.empty()) il << "    .locals init (" << body.locals << ")\n";
            if (cctor) emit_static_initializers(method->getParent());
            il << body.code << "  }\n";
        };
        for (auto* enumeration : enums) {
            il << ".class " << (annotation(enumeration, "cli.visibility:") == "private" ? "private" : "public")
               << " auto ansi sealed " << enum_name(enumeration) << " extends [System.Runtime]System.Enum {\n";
            if (annotated(enumeration, "cli.flags")) il << "  .custom instance void [System.Runtime]System.FlagsAttribute::.ctor() = (01 00 00 00)\n";
            il << "  .field public specialname rtspecialname " << type(enumeration->getIntegerType()) << " value__\n";
            for (auto* member : enumeration->enumerators()) {
                llvm::SmallString<32> value; member->getInitVal().toString(value, 10);
                il << "  .field public static literal valuetype " << enum_name(enumeration) << " '" << member->getNameAsString()
                   << "' = " << type(enumeration->getIntegerType()) << "(" << value.str().str() << ")\n";
            }
            il << "}\n";
        }
        auto emit_cleanup = [&](const CXXRecordDecl* r) {
            auto* destructor = source_destructor(r);
            if (!destructor) return;
            auto base = dispose_base(r);
            bool inherited_entry = false;
            for (auto* parent = class_base(r); parent; parent = class_base(parent)) {
                inherited_entry |= source_destructor(parent) != nullptr;
                for (auto* method : parent->methods()) inherited_entry |= method->getNameAsString() == "Dispose" &&
                    !method->isStatic() && !method->getNumParams() && method->getReturnType()->isVoidType() && method->getAccess() == AS_public;
            }
            if (!base.boolean || !inherited_entry) {
                il << "  .method public hidebysig newslot virtual final instance void Dispose() cil managed {\n"
                   << "    .override method instance void [System.Runtime]System.IDisposable::Dispose()\n    .maxstack 2\n"
                   << "    ldarg.0\n    ldc.i4.1\n    callvirt instance void " << record_token(r) << "::Dispose(bool)\n"
                   << "    ldarg.0\n    call void [System.Runtime]System.GC::SuppressFinalize(object)\n    ret\n  }\n";
            }
            il << "  .method family hidebysig virtual " << (base.boolean ? "" : "newslot ")
               << "instance void Dispose(bool marshal(unsigned int8)) cil managed {\n    .maxstack 2\n"
               << "    ldarg.1\n    brfalse DISPOSE_FALSE\n";
            if (base.owner) il << "    .try {\n";
            il << "    ldarg.0\n    call " << signature(destructor) << "\n";
            if (base.owner) {
                il << "    leave DISPOSE_DONE\n    } finally {\n    ldarg.0\n";
                if (base.boolean) il << "    ldc.i4.1\n";
                il << "    call " << (base.generated ? "instance void " + record_token(base.owner) + "::Dispose(bool)" : signature(base.method))
                   << "\n    endfinally\n    }\nDISPOSE_DONE:\n";
            }
            il << "    ret\nDISPOSE_FALSE:\n    ldarg.0\n";
            if (base.boolean) {
                il << "    ldc.i4.0\n    call " << (base.generated ? "instance void " + record_token(base.owner) + "::Dispose(bool)" : signature(base.method)) << "\n";
            } else {
                auto* parent = class_base(r);
                il << "    call instance void " << (parent ? record_token(parent) : "[System.Runtime]System.Object") << "::Finalize()\n";
            }
            il << "    ret\n  }\n";
        };
        std::function<void(const CXXRecordDecl*)> emit_record = [&](const CXXRecordDecl* r) {
            if (auto* invoke = delegate_invoke(r)) {
                il << ".class " << (annotation(r, "cli.visibility:") == "private" ? "private" : "public") << " auto ansi sealed " << record_name(r)
                   << " extends [System.Runtime]System.MulticastDelegate {\n"
                   << "  .method public hidebysig specialname rtspecialname instance void .ctor(object, native int) runtime managed {}\n"
                   << "  .method public hidebysig newslot virtual instance " << type(invoke->getReturnType()) << " Invoke(";
                for (unsigned j = 0; j < invoke->getNumParams(); ++j) { if (j) il << ", "; il << parameter_signature(invoke->getParamDecl(j)); }
                il << ") runtime managed {}\n}\n";
                return;
            }
            auto* parent = class_base(r);
            auto layout = layout_kind(r), charset = layout_constant(r, "cli.layout.CharSet", 1);
            auto* enclosing = dyn_cast<CXXRecordDecl>(r->getDeclContext());
            auto name = record_name(r);
            if (enclosing) name = name.substr(record_name(enclosing).size() + 1);
            auto visibility = enclosing ? (r->getAccess() == AS_private ? "nested private " : r->getAccess() == AS_protected ? "nested family " : "nested public ") :
                (annotation(r, "cli.visibility:") == "private" ? "private " : "public ");
            il << ".class " << visibility << (is_interface(r) ? "interface abstract " :
                std::string(layout == 2 ? "explicit " : layout == 0 ? "sequential " : "auto ") + (charset == 3 ? "unicode " : charset == 4 ? "autochar " : "ansi "))
               << (!is_interface(r) && (annotated(r, "cli.abstract") || r->isAbstract()) ? "abstract " : "")
               << ((annotated(r, "cli.sealed") || annotated(r, "cli.value")) ? "sealed " : "") << (annotated(r, "cli.com.import") ? "import " : "") << name;
            if (!is_interface(r)) il << " extends " << (annotated(r, "cli.value") ? "[System.Runtime]System.ValueType" : parent ? record_token(parent) : "[System.Runtime]System.Object");
            bool first_interface = true;
            for (auto& base : r->bases()) if (is_interface(base.getType()->getAsCXXRecordDecl())) {
                il << (first_interface ? " implements " : ", ") << record_token(base.getType()->getAsCXXRecordDecl()); first_interface = false;
            }
            if (source_destructor(r)) {
                bool explicit_disposable = false;
                for (auto& base : r->bases()) explicit_disposable |= record_annotation(base.getType()->getAsCXXRecordDecl(), "cli.external:") == "System.Runtime|System.IDisposable";
                if (!explicit_disposable) il << (first_interface ? " implements " : ", ") << "[System.Runtime]System.IDisposable";
            }
            il << " {\n";
            if (auto guid = guid_attribute(r); !guid.empty()) {
                static const char hex[] = "0123456789abcdef";
                il << "  .custom " << attribute_constructor("System.Runtime.InteropServices.GuidAttribute", {"string"}) << " = (01 00 24 ";
                for (unsigned char ch : guid) il << hex[ch >> 4] << hex[ch & 15] << ' ';
                il << "00 00)\n";
            }
            if (annotated(r, "cli.com.interface")) {
                auto value = layout_constant(r, "cli.com.interface", -1);
                auto owner = metadata.types.find("System.Runtime.InteropServices.ComInterfaceType");
                if (owner == metadata.types.end()) throw std::runtime_error("select ComInterfaceType metadata for InterfaceTypeAttribute");
                auto parameter = "valuetype " + assembly_reference(owner->second) + "'System.Runtime.InteropServices.ComInterfaceType'";
                il << "  .custom " << attribute_constructor("System.Runtime.InteropServices.InterfaceTypeAttribute", {parameter}) << " = (01 00 0" << value << " 00 00 00 00 00)\n";
            }
            if (annotated(r, "cli.com.class")) {
                auto owner = metadata.types.find("System.Runtime.InteropServices.ClassInterfaceType");
                if (owner == metadata.types.end()) throw std::runtime_error("select ClassInterfaceType metadata");
                auto parameter = "valuetype " + assembly_reference(owner->second) + "'System.Runtime.InteropServices.ClassInterfaceType'";
                il << "  .custom " << attribute_constructor("System.Runtime.InteropServices.ClassInterfaceAttribute", {parameter}) << " = (01 00 0" << layout_constant(r, "cli.com.class", -1) << " 00 00 00 00 00)\n";
            }
            if (annotated(r, "cli.com.visible"))
                il << "  .custom " << attribute_constructor("System.Runtime.InteropServices.ComVisibleAttribute", {"bool"}) << " = (01 00 0" << layout_constant(r, "cli.com.visible", -1) << " 00 00)\n";
            for (auto* member : r->decls()) if (auto* property = dyn_cast<MSPropertyDecl>(member); property && property->getNameAsString() == "__cli_default")
                il << "  .custom instance void [System.Runtime]System.Reflection.DefaultMemberAttribute::.ctor(string) = (01 00 04 49 74 65 6d 00 00)\n";
            if (annotated(r, "cli.layout.Pack")) il << "  .pack " << layout_constant(r, "cli.layout.Pack", 0) << "\n";
            if (annotated(r, "cli.layout.Size")) il << "  .size " << layout_constant(r, "cli.layout.Size", 0) << "\n";
            for (auto* field : r->fields()) {
                il << "  .field ";
                if (layout == 2) il << '[' << layout_constant(field, "cli.field.offset", -1) << "] ";
                il << (field->getAccess() == AS_private ? "private " : field->getAccess() == AS_protected ? "family " : annotation(field, "cli.access:") == "assembly" ? "assembly " : "public ")
                    << declared_type(field) << " '" << field->getNameAsString() << "'\n";
            }
            for (auto* member : r->decls()) if (auto* field = dyn_cast<VarDecl>(member); field && field->isStaticDataMember()) {
                if (!annotation(field, "cli.static.property:").empty()) continue;
                if (field->getType()->isReferenceType() || !interior_element(field->getType()).isNull()) throw std::runtime_error("managed pointer static fields are not enabled yet");
                validate_declarator(field->getTypeSourceInfo());
                il << "  .field " << (field->getAccess() == AS_private ? "private " : field->getAccess() == AS_protected ? "family " : annotation(field, "cli.access:") == "assembly" ? "assembly " : "public ")
                    << "static " << declared_type(field) << " '" << field->getNameAsString() << "'\n";
            }
            bool explicit_cctor = false, field_initializers = false;
            for (auto* method : r->methods()) {
                explicit_cctor |= annotated(method, "cli.cctor");
                field_initializers |= !annotation(method, "cli.static.init:").empty();
            }
            if (!explicit_cctor && field_initializers) {
                il << "  .method private hidebysig specialname rtspecialname static void .cctor() cil managed {\n    .maxstack 1\n";
                emit_static_initializers(r); il << "    ret\n  }\n";
            }
            for (size_t i = 0; i < functions.size(); ++i) if (ir[i].owner == record_name(r)) emit_function(i);
            emit_cleanup(r);
            for (auto* member : r->decls()) if (auto* property = dyn_cast<VarDecl>(member); property && !annotation(property, "cli.static.property:").empty()) {
                auto names = annotation(property, "cli.static.property:"); auto bar = names.find('|');
                il << "  .property " << type(property->getType()) << " '" << property->getNameAsString() << "'() {\n";
                if (bar) il << "    .get " << signature(static_property_method(property, false)) << "\n";
                if (bar + 1 < names.size()) il << "    .set " << signature(static_property_method(property, true)) << "\n";
                il << "  }\n";
            }
            for (auto* member : r->decls()) if (auto* property = dyn_cast<MSPropertyDecl>(member)) {
                auto property_type = property->getType();
                bool indexed = property_type->isIncompleteArrayType();
                if (indexed) property_type = context.getAsArrayType(property_type)->getElementType();
                auto name = property->getNameAsString() == "__cli_default" ? "Item" : property->getNameAsString();
                std::set<std::string> declared_indices;
                if (indexed) for (auto* declaration : r->decls()) if (auto* alias = dyn_cast<TypedefNameDecl>(declaration);
                    alias && annotation(alias, "cli.index:") == name)
                    if (!declared_indices.insert(type(alias->getUnderlyingType())).second)
                        throw std::runtime_error("duplicate indexed property parameter type");
                struct Accessors { const CXXMethodDecl* getter = nullptr; const CXXMethodDecl* setter = nullptr; };
                std::map<std::string, Accessors> overloads;
                std::set<const CXXMethodDecl*> seen;
                for (auto* method : r->methods()) {
                    bool get = property->getGetterId() && method->getIdentifier() == property->getGetterId();
                    bool set = property->getSetterId() && method->getIdentifier() == property->getSetterId();
                    if ((!get && !set) || !seen.insert(method->getCanonicalDecl()).second) continue;
                    if (set && !method->getNumParams()) throw std::runtime_error("property setter requires a value parameter");
                    unsigned indices = method->getNumParams() - (set ? 1 : 0);
                    if (indexed && indices != 1) throw std::runtime_error("indexed properties currently require one index parameter");
                    if (!indexed && indices) throw std::runtime_error("non-indexed property getter cannot have parameters");
                    if (get && type(method->getReturnType()) != type(property_type)) throw std::runtime_error("property getter type does not match its declaration");
                    if (set && (!method->getReturnType()->isVoidType() || type(method->getParamDecl(indices)->getType()) != type(property_type)))
                        throw std::runtime_error("property setter type does not match its declaration");
                    auto key = indexed ? type(method->getParamDecl(0)->getType()) : std::string();
                    if (indexed && !declared_indices.count(key))
                        throw std::runtime_error(get ? "indexed getter parameter does not match its declaration" : "indexed setter parameter does not match its declaration");
                    auto& slot = get ? overloads[key].getter : overloads[key].setter;
                    if (slot) throw std::runtime_error("duplicate indexed property accessor");
                    slot = method;
                }
                if (indexed) for (auto& key : declared_indices) if (!overloads.count(key))
                    throw std::runtime_error("indexed property declaration has no matching accessors");
                for (auto& entry : overloads) {
                    auto& accessors = entry.second;
                    if ((property->getGetterId() && !accessors.getter) || (property->getSetterId() && !accessors.setter))
                        throw std::runtime_error("property overload is missing a declared accessor");
                    il << "  .property instance " << type(property_type) << " '" << name << "'(";
                    if (indexed) il << entry.first;
                    il << ") {\n";
                    if (accessors.getter) il << "    .get " << signature(accessors.getter) << "\n";
                    if (accessors.setter) il << "    .set " << signature(accessors.setter) << "\n";
                    il << "  }\n";
                }
            }
            for (auto* child : records) if (child->getDeclContext() == r) emit_record(child);
            il << "}\n";
        };
        for (auto* r : records) if (!isa<CXXRecordDecl>(r->getDeclContext())) emit_record(r);
        for (size_t i = 0; i < functions.size(); ++i) if (ir[i].owner.empty()) emit_function(i);
        return il.str();
    }
    std::string evidence() const {
        llvm::json::Array list;
        for (auto& f : ir) list.push_back(llvm::json::Object{{"name", f.name}, {"owner", f.owner}, {"maxstack", f.max_stack}});
        llvm::json::Object result{{"clang", CLANG_VERSION_STRING}, {"parser", "clang::Parser with private caret declarator patch"}, {"functions", std::move(list)}};
        std::string text;
        llvm::raw_string_ostream stream(text); stream << llvm::json::Value(std::move(result));
        return text + "\n";
    }
};

class EmitConsumer : public ASTConsumer {
    CompilerInstance& ci;
    Options options;
public:
    EmitConsumer(CompilerInstance& c, const Options& o) : ci(c), options(o) {}
    void HandleTranslationUnit(ASTContext&) override {
        if (ci.getDiagnostics().hasErrorOccurred()) return;
        try {
            Metadata metadata; metadata.load(options.metadata);
            Emitter emitter(ci, metadata);
            std::string il = emitter.emit(options);
            write_file(options.output, il);
            if (!options.evidence.empty()) write_file(options.evidence, emitter.evidence());
        } catch (const std::exception& e) { report(ci, {}, e.what()); }
    }
};
class EmitAction : public PluginASTAction {
    Options options;
protected:
    bool ParseArgs(const CompilerInstance& ci, const std::vector<std::string>& args) override { return options.parse(ci, args); }
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance& ci, llvm::StringRef) override { return std::make_unique<EmitConsumer>(ci, options); }
    void ExecuteAction() override {
        auto& ci = getCompilerInstance();
        if (!ci.hasSema()) ci.createSema(getTranslationUnitKind(), nullptr);
        cppcliParseAST(ci.getSema(), false, false);
    }
};
FrontendPluginRegistry::Add<AdaptAction> adapt("cli-adapt", "Adapt CLI-only syntax using Clang preprocessing tokens");
FrontendPluginRegistry::Add<ShimAction> shim("cli-shim", "Map #using to a reference pragma with Clang raw lexing");
FrontendPluginRegistry::Add<ReferencesAction> references("cli-references", "Discover active #using references with Clang preprocessing");
FrontendPluginRegistry::Add<EmitAction> emit("cli-emit", "Validate managed annotations and emit IL from Clang AST");
} // namespace
