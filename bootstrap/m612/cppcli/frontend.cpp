// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// CLI-only token adaptation, followed by Clang's actual parser and Sema.
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/TypeLoc.h>
#include <clang/AST/Mangle.h>
#include <clang/Basic/Version.h>
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
struct ImportedField { std::string assembly, owner, name, type; };
struct Metadata {
    std::map<std::string, ImportedMethod> methods;
    std::map<std::string, ImportedField> fields;
    std::map<std::string, std::string> types;
    std::set<std::string> short_types;
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
                fields.emplace(f.getString("Id")->str(), ImportedField{assembly, name, f.getString("Name")->str(), f.getObject("Type")->getString("Il")->str()});
            }
        }
    }
};

struct Options {
    std::string metadata, output, evidence;
    std::string assembly = "cli_program", target = "exe", native_library, public_key;
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
            pp.EnterMainSourceFile();
            std::vector<Lexeme> tokens;
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
            // Only managed declarations and allocation markers are rewritten.
            // Expressions/statements remain C++ and are parsed by Clang later.
            for (size_t i = 0; i + 2 < tokens.size(); ++i)
                if ((tokens[i].text == "ref" || tokens[i].text == "interface" || tokens[i].text == "value") && (tokens[i + 1].text == "class" || tokens[i + 1].text == "struct")) metadata.short_types.insert(tokens[i + 2].text);
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
            for (const auto& entry : metadata.methods) {
                auto& m = entry.second;
                if (std::find(m.parameters.begin(), m.parameters.end(), "string") != m.parameters.end())
                    managed_string_calls.insert(m.owner.substr(m.owner.find_last_of('.') + 1) + "::" + m.name);
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
                if (tokens[i].text == "value") value_defaults[open] = "public: __attribute__((annotate(\"cli.value.zero\"))) " + name + "() {} __attribute__((annotate(\"cli.box\"))) operator " + name + "*() const; " + (tokens[i+1].text == "struct" ? "public:" : "private:");
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
                    else if (depth == 1 && tokens[j].text == "(" && j && tokens[j - 1].kind == tok::identifier) {
                        size_t end = j + 1; int parentheses = 1; bool string_parameter = false;
                        while (end < tokens.size() && parentheses) {
                            if (tokens[end].text == "(") ++parentheses;
                            if (tokens[end].text == ")") --parentheses;
                            if (tokens[end].text == "String" && end + 1 < tokens.size() && tokens[end + 1].text == "^") string_parameter = true;
                            ++end;
                        }
                        if (tokens[i].text == "value" && tokens[j-1].text == name && end == j+2 && (j < 2 || tokens[j-2].text != "static"))
                            throw std::runtime_error("user-defined zero-argument value constructors are not supported; default storage is zero initialized");
                        if (string_parameter) managed_string_calls.insert(name + "::" + tokens[j - 1].text);
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
                std::string type;
                for (size_t j = i + 1; j < end; ++j) type += tokens[j].text;
                emit(t, "__cli_new<" + type + ">" + (default_allocation ? "()" : ""));
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
                    (tokens[i+2].text == "get" || tokens[i+2].text == "set") && property_names.count({tokens[i-2].text, t.text})) {
                    emit(t, tokens[i+2].text + "_" + (t.text == "default" ? "Item" : t.text)); i += 2; continue;
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
                            if (adapt_allocation(j)) continue;
                            if (tokens[j].text == "safe_cast") { emit(tokens[j], "__cli_safe_cast"); continue; }
                            emit(tokens[j], tokens[j].text);
                        }
                        emit(tokens[end], "; } " + class_access[i] + ":");
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
                if ((t.text == "ref" || t.text == "interface" || t.text == "value") && i + 2 < tokens.size() &&
                    (tokens[i + 1].text == "class" || tokens[i + 1].text == "struct")) {
                    bool interface = t.text == "interface", value = t.text == "value";
                    std::string attributes = value ? "annotate(\"cli.value\")" : "annotate(\"cli.ref\")";
                    if (layouts.count(i)) attributes += layouts[i];
                    if (i && (tokens[i-1].text == "public" || tokens[i-1].text == "private")) attributes += ", annotate(\"cli.visibility:" + tokens[i-1].text + "\")";
                    if (interface) attributes += ", annotate(\"cli.interface\"), annotate(\"cli.abstract\")";
                    size_t end = i + 3;
                    while (end < tokens.size() && (tokens[end].text == "abstract" || tokens[end].text == "sealed")) {
                        attributes += ", annotate(\"cli." + tokens[end].text + "\")";
                        ++end;
                    }
                    emit(t, std::string(interface || tokens[i + 1].text == "struct" ? "struct" : "class") + " __attribute__((" + attributes + ")) " + tokens[i + 2].text);
                    if (attributes.find("cli.sealed") != std::string::npos) emit(t, "final");
                    if (end < tokens.size() && tokens[end].text == ":") {
                        emit(tokens[end], ":"); ++end;
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
    std::map<const VarDecl*, unsigned> locals;
    std::map<const ParmVarDecl*, unsigned> parameters;
    std::ostringstream code;
    FunctionDecl* current = nullptr;
    std::string native_library;
    unsigned label_count = 0, max_stack = 0;
    int stack = 0;
    struct ControlLabels { std::string stop, next; unsigned depth; };
    std::vector<ControlLabels> control_labels;
    std::map<const SwitchCase*, std::pair<std::string, unsigned>> switch_labels;
    unsigned exception_depth = 0, catch_depth = 0, finally_depth = 0, return_slot = 0;
    std::string return_label;
    bool return_slot_set = false;
    std::vector<FunctionIR> ir;
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
        if (isa<ClassTemplateSpecializationDecl>(record)) throw std::runtime_error("source-defined managed class template emission is not enabled yet");
        if (!record_annotated(record, "cli.ref") && !record_annotated(record, "cli.value"))
            throw std::runtime_error("native C++ class is not a managed type: " + record->getQualifiedNameAsString());
        std::string name = record->getQualifiedNameAsString();
        for (size_t at; (at = name.find("::")) != std::string::npos;) name.replace(at, 2, ".");
        return "'" + name + "'";
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
            auto reference = loc.getAs<ReferenceTypeLoc>();
            if (!reference.isNull()) {
                if (reference.getType()->isRValueReferenceType()) throw std::runtime_error("rvalue reference signatures are not enabled yet");
                SourceLocation sigil = sm.getSpellingLoc(reference.getSigilLoc());
                if (sigil.isValid() && sm.isWrittenInMainFile(sigil) && *sm.getCharacterData(sigil) != '%')
                    throw std::runtime_error("native '&' references are not supported in managed declarations; use a '%' tracking reference");
            }
            auto pointer = loc.getAs<PointerTypeLoc>();
            if (pointer.isNull()) continue;
            SourceLocation star = sm.getSpellingLoc(pointer.getStarLoc());
            if (!handle_target(pointer.getType())) continue;
            if (star.isValid() && sm.isWrittenInMainFile(star) && *sm.getCharacterData(star) == '*')
                throw std::runtime_error("native '*' pointers to managed reference classes are not supported; declare a '^' handle");
        }
    }
    std::string field_signature(const FieldDecl* f) const {
        auto* parent = dyn_cast<CXXRecordDecl>(f->getParent());
        if (!parent) throw std::runtime_error("field has no managed parent");
        auto imported = metadata.fields.find(annotation(f, "cli.import.field:"));
        return (imported == metadata.fields.end() ? type(f->getType()) : imported->second.type) + " " + record_token(parent) + "::'" + f->getNameAsString() + "'";
    }
    std::string static_field_signature(const VarDecl* f) const {
        if (!f->isStaticDataMember()) throw std::runtime_error("static storage requires a managed class field");
        auto imported = metadata.fields.find(annotation(f, "cli.import.field:"));
        return (imported == metadata.fields.end() ? type(f->getType()) : imported->second.type) + " " + record_token(cast<CXXRecordDecl>(f->getDeclContext())) + "::'" + f->getNameAsString() + "'";
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
        std::string result = isa<CXXConstructorDecl>(f) ? "void" : type(f->getReturnType());
        std::string name = method_name(f);
        std::string sig = (method && !method->isStatic() ? "instance " : "") + result + " " + owner + "'" + name + "'(";
        for (unsigned i = 0; i < f->getNumParams(); ++i) {
            if (i) sig += ", ";
            sig += type(f->getParamDecl(i)->getType());
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
    void address(const Expr* original) {
        const Expr* e = strip(original)->IgnoreParenImpCasts();
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
                if (v->isStaticDataMember()) op("ldsflda " + static_field_signature(v), 0, 1);
                else op(std::string(v->getType()->isReferenceType() ? "ldloc " : "ldloca ") + std::to_string(locals.at(v)), 0, 1);
            }
            else fail(e, "value address requires a local or parameter");
            return;
        }
        if (auto* member = dyn_cast<MemberExpr>(e)) {
            auto* field = dyn_cast<FieldDecl>(member->getMemberDecl());
            if (!field) fail(e, "value address requires an instance field");
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
            unsigned slot = temporary(type(field->getType()));
            op("stloc " + std::to_string(slot), 1);
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
        if (b->getOpcode() == BO_Assign) { expr(b->getRHS()); store(b->getLHS(), true); return; }
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
                unsigned receiver = temporary(receiver_type(field));
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
        expr(b->getLHS()); expr(b->getRHS());
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
    void call(const CallExpr* c) {
        auto* f = c->getDirectCallee();
        if (!f) fail(c, "indirect calls are not supported yet");
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
        if (auto* opcall = dyn_cast<CXXOperatorCallExpr>(c)) {
            if (opcall->getOperator() == OO_Arrow) { expr(opcall->getArg(0)); return; }
            if (opcall->getOperator() == OO_Equal && opcall->getNumArgs() == 2 && (handle_target(opcall->getArg(0)->getType()) || value_target(opcall->getArg(0)->getType()))) {
                expr(opcall->getArg(1)); store(opcall->getArg(0), true); return;
            }
        }
        if (f->getNameAsString() == "__cli_safe_cast" || f->getNameAsString() == "__cli_dynamic_cast") {
            if (c->getNumArgs() != 1) fail(c, "managed casts require one argument");
            auto* target = handle_target(c->getType());
            bool safe = f->getNameAsString() == "__cli_safe_cast";
            QualType source_type = c->getArg(0)->getType();
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
        if (f->getNameAsString() == "__cli_new") {
            auto* target = handle_target(c->getType());
            if (!target) fail(c, "allocation marker has no managed type");
            if (is_interface(target)) fail(c, "cannot allocate a managed interface");
            QualType element = array_element(target);
            if (!element.isNull()) {
                if (c->getNumArgs() != 1 || !c->getArg(0)->getType()->isIntegerType()) fail(c, "array allocation requires one integer length");
                expr(c->getArg(0)); op("newarr " + type(element), 1, 1); return;
            }
            if (record_name(target) == "[System.Runtime]'System.String'") {
                if (c->getNumArgs() != 1 || !isa<StringLiteral>(c->getArg(0)->IgnoreParenImpCasts()))
                    fail(c, "String allocation currently requires a literal");
                expr(c->getArg(0));
                op("callvirt instance char[] [System.Runtime]System.String::ToCharArray()", 1, 1);
                op("newobj instance void [System.Runtime]System.String::.ctor(char[])", 1, 1);
                return;
            }
            if (target->getDefinition() && annotated(target->getDefinition(), "cli.abstract")) fail(c, "cannot allocate an abstract managed class");
            const CXXConstructorDecl* selected = nullptr;
            // Reuse Clang's constructor overload resolution in __cli_new's
            // decltype(T(args...)) constraint, rather than selecting by arity.
            std::function<void(const Stmt*)> find_constructor = [&](const Stmt* s) {
                if (!s) return;
                if (auto* construct = dyn_cast<CXXConstructExpr>(s))
                    if (construct->getConstructor()->getParent()->getCanonicalDecl() == target->getCanonicalDecl()) selected = construct->getConstructor();
                for (auto* child : s->children()) find_constructor(child);
            };
            if (auto* trailing = dyn_cast<DecltypeType>(f->getReturnType().getTypePtr())) find_constructor(trailing->getUnderlyingExpr());
            if (!selected) fail(c, "allocation has no Clang-resolved managed constructor");
            if (record_annotated(target, "cli.value") && annotated(selected, "cli.value.zero")) {
                auto value_type = "valuetype " + record_name(target);
                unsigned slot = temporary(value_type);
                op("ldloca " + std::to_string(slot), 0, 1); op("initobj " + record_token(target), 1);
                op("ldloc " + std::to_string(slot), 0, 1); op("box " + value_type, 1, 1); return;
            }
            if (!record_annotation(target, "cli.external:").empty() && annotation(selected, "cli.import:").empty())
                fail(c, "imported class has no public metadata constructor");
            if (selected->getNumParams() != c->getNumArgs()) fail(c, "default constructor arguments are not supported yet");
            for (unsigned i = 0; i < c->getNumArgs(); ++i) {
                if (type(c->getArg(i)->getType()) != type(selected->getParamDecl(i)->getType())) fail(c, "constructor argument conversion is not enabled yet");
                expr(c->getArg(i));
            }
            op("newobj " + signature(selected), c->getNumArgs(), 1);
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
            if (f->getParamDecl(i)->getType()->isReferenceType()) address(c->getArg(i)); else expr(c->getArg(i));
        }
        std::string id = annotation(f, "cli.import:");
        std::string sig;
        if (!id.empty()) {
            auto imported = metadata.methods.find(id);
            if (imported == metadata.methods.end()) fail(c, "imported member is absent from metadata");
            sig = imported_signature(imported->second, m);
        } else {
            if (!f->hasBody() && !(m && m->isPureVirtual()) && !native_function(f)) fail(c, "calling a function without an emitted definition");
            sig = signature(f);
        }
        bool qualified = false;
        if (auto* member = dyn_cast<CXXMemberCallExpr>(c)) if (auto* callee = dyn_cast<MemberExpr>(member->getCallee()->IgnoreParenImpCasts())) qualified = callee->hasQualifier();
        if (qualified && m && m->isPureVirtual()) fail(c, "a qualified base call cannot target an abstract method");
        op(std::string(instance && !qualified && !record_annotated(m->getParent(), "cli.value") ? "callvirt " : "call ") + sig, c->getNumArgs() + unsigned(instance), f->getReturnType()->isVoidType() ? 0 : 1);
    }
    void expr(const Expr* original) {
        const Expr* e = strip(original);
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
                    unsigned slot = temporary(type(opaque->getType()));
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
            expr(conditional->getTrueExpr());
            if (stack != base + 1) fail(e, "conditional operand did not produce one value");
            op("br " + done); stack = base; code << otherwise << ":\n";
            expr(conditional->getFalseExpr());
            if (stack != base + 1) fail(e, "conditional branch stack mismatch");
            code << done << ":\n"; return;
        }
        if (auto* c = dyn_cast<CallExpr>(e)) { call(c); return; }
        if (auto* c = dyn_cast<CXXConstructExpr>(e)) {
            if (!interior_element(c->getType()).isNull()) {
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
                    unsigned receiver = temporary(receiver_type(field));
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
            if (c->getCastKind() == CK_ConstructorConversion && value_target(c->getType())) { expr(c->getSubExpr()); return; }
            if (c->getCastKind() == CK_UserDefinedConversion && c->getType()->isPointerType() && !handle_target(c->getType())) {
                auto* conversion = dyn_cast<CXXMemberCallExpr>(strip(c->getSubExpr()));
                auto* method = conversion ? dyn_cast_or_null<CXXConversionDecl>(conversion->getDirectCallee()) : nullptr;
                if (!method || !record_annotated(method->getParent(), "cli.pin")) fail(c, "explicit native pointer conversion requires a pin_ptr intrinsic");
                expr(c->getSubExpr()); return;
            }
            if (c->getCastKind() == CK_BitCast && c->getType()->isPointerType() && !handle_target(c->getType()) &&
                c->getSubExpr()->getType()->isPointerType() && !handle_target(c->getSubExpr()->getType())) { expr(c->getSubExpr()); return; }
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
            type(v->getType());
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
                if (!v->hasInit()) fail(s, "uninitialized locals are not supported yet");
                if (v->getType()->isReferenceType()) address(v->getInit()); else expr(v->getInit()); op("stloc " + std::to_string(locals.at(v)), 1);
            }
        } else if (auto* r = dyn_cast<ReturnStmt>(s)) {
            if (finally_depth) fail(s, "return cannot leave a finally handler");
            if (r->getRetValue()) expr(r->getRetValue());
            if (exception_depth) {
                if (return_label.empty()) return_label = label();
                if (r->getRetValue()) {
                    if (!return_slot_set) { return_slot = temporary(type(current->getReturnType())); return_slot_set = true; }
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
        return !native_library.empty() && f->isExternC() && !isa<CXXMethodDecl>(f) && !f->hasBody() && f->isUsed();
    }
    void validate_native_signature(const FunctionDecl* f) const {
        auto valid = [&](QualType t, bool result) {
            if (result && t->isVoidType()) return;
            if ((!t->isIntegerType() && !t->isRealFloatingType() && !t->isPointerType()) || t->isBooleanType() || handle_target(t))
                throw std::runtime_error("native bridge signatures require scalars or unmanaged pointers; bool and managed/pointer marshalling are not enabled");
            type(t);
        };
        valid(f->getReturnType(), true);
        for (auto* parameter : f->parameters()) valid(parameter->getType(), false);
        auto convention = f->getType()->castAs<FunctionType>()->getCallConv();
        if (f->isVariadic() || f->hasAttr<AsmLabelAttr>() || (convention != CC_C && convention != CC_Win64))
            throw std::runtime_error("native bridge requires a non-variadic extern-C function using the platform C ABI");
    }
    FunctionIR function(FunctionDecl* f) {
        if (pinned(f->getReturnType())) throw std::runtime_error("pin_ptr is restricted to block-scoped locals");
        for (auto* p : f->parameters()) if (pinned(p->getType())) throw std::runtime_error("pin_ptr is restricted to block-scoped locals");
        if (native_function(f)) {
            validate_native_signature(f);
            return {method_name(f), "", type(f->getReturnType()), false, 0, "", ""};
        }
        current = f; locals.clear(); parameters.clear(); temporary_types.clear(); opaque_values.clear(); code.str(""); code.clear(); stack = 0; max_stack = 0;
        label_count = 0; control_labels.clear(); switch_labels.clear(); exception_depth = catch_depth = finally_depth = 0; return_label.clear(); return_slot_set = false;
        auto* method = dyn_cast<CXXMethodDecl>(f);
        bool instance = method && !method->isStatic();
        if (method && method->isPureVirtual()) return {method_name(f), record_name(method->getParent()), type(f->getReturnType()), instance, 0, "", ""};
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
                op("ldarg.0", 0, 1); expr(init->getInit()); op("stfld " + field_signature(init->getMember()), 2);
            }
        }
        statement(f->getBody());
        if (f->getReturnType()->isVoidType()) op("ret");
        else if (value_target(f->getReturnType())) {
            unsigned slot = temporary(type(f->getReturnType()));
            op("ldloca " + std::to_string(slot), 0, 1); op("initobj " + record_token(value_target(f->getReturnType())), 1);
            op("ldloc " + std::to_string(slot), 0, 1); op("ret", 1);
        } else {
            std::string result = type(f->getReturnType());
            op(result == "int64" || result == "uint64" ? "ldc.i8 0" : result == "float32" ? "ldc.r4 0" : result == "float64" ? "ldc.r8 0" :
               handle_target(f->getReturnType()) ? "ldnull" : "ldc.i4.0", 0, 1);
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
            declarations += "[" + std::to_string(i) + "] " + type(ordered[i]->getType()) + (pinned(ordered[i]->getType()) ? " pinned" : "") + " 'local" + std::to_string(i) + "'";
        }
        for (unsigned i = 0; i < temporary_types.size(); ++i) {
            if (!declarations.empty()) declarations += ", ";
            declarations += "[" + std::to_string(ordered.size() + i) + "] " + temporary_types[i] + " 'temporary" + std::to_string(i) + "'";
        }
        return {method_name(f), method ? record_name(method->getParent()) : "", type(f->getReturnType()), instance,
            std::max(1u, max_stack), declarations, code.str()};
    }
public:
    Emitter(CompilerInstance& compiler, const Metadata& md) : ci(compiler), context(ci.getASTContext()), metadata(md) {}
    bool VisitFunctionDecl(FunctionDecl* f) {
        auto* method = dyn_cast<CXXMethodDecl>(f);
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
        if (isa<CXXDestructorDecl>(f)) throw std::runtime_error("managed destructors/finalizers are not enabled yet");
        if (method && f->isOverloadedOperator()) throw std::runtime_error("managed operator metadata is not enabled yet");
        if (method) named_overrides(method);
        if (method) for (auto* base : method->overridden_methods()) {
            if (type(method->getReturnType()) != type(base->getReturnType())) throw std::runtime_error("covariant managed return metadata is not enabled yet");
            if (!is_interface(base->getParent()) && !method->hasAttr<OverrideAttr>()) throw std::runtime_error("managed class overrides require an explicit override specifier");
            if (is_interface(base->getParent()) && method->getAccess() != AS_public) throw std::runtime_error("implicit interface implementations must be public");
        }
        if (f->getReturnType()->isReferenceType()) throw std::runtime_error("returning tracking references is not enabled yet");
        if (!interior_element(f->getReturnType()).isNull()) throw std::runtime_error("returning managed pointer intrinsics is not enabled yet");
        validate_declarator(f->getTypeSourceInfo());
        for (auto* p : f->parameters()) validate_declarator(p->getTypeSourceInfo());
        if (f->getType()->isDependentType() || f->getTemplatedKind() == FunctionDecl::TK_FunctionTemplate)
            throw std::runtime_error("unspecialized dependent function reached IL emission");
        if (seen.insert(f->getCanonicalDecl()).second) functions.push_back(f);
        return true;
    }
    bool VisitCXXRecordDecl(CXXRecordDecl* r) {
        if (r->isThisDeclarationADefinition() && (annotated(r, "cli.ref") || annotated(r, "cli.value")) && !annotated(r, "cli.array") && annotation(r,"cli.external:").empty()) {
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
                        if (!implemented) throw std::runtime_error("concrete managed class is missing interface slot: " + imported->second.owner + "::" + imported->second.name);
                    }
                    for (auto& parent : base->bases()) validate_slots(parent.getType()->getAsCXXRecordDecl());
                };
                for (auto& base : r->bases()) validate_slots(base.getType()->getAsCXXRecordDecl());
            }
            auto layout = layout_kind(r);
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
                auto offset = layout_constant(field, "cli.field.offset", -1);
                if (layout == 2 && !annotated(field, "cli.field.offset")) throw std::runtime_error("explicit layout requires FieldOffset on every instance field");
                if (annotated(field, "cli.field.offset") && (layout != 2 || offset < 0 || offset > INT32_MAX)) throw std::runtime_error("FieldOffset requires explicit layout and a nonnegative Int32 offset");
                if (layout != 3 && field->isBitField()) throw std::runtime_error("managed layout bitfields are not enabled yet");
            }
            for (auto* member : r->decls()) if (auto* field = dyn_cast<VarDecl>(member); field && annotated(field, "cli.field.offset"))
                throw std::runtime_error("FieldOffset requires a managed instance field");
            for (auto* method : r->methods()) if (annotated(method, "cli.cctor") && !method->hasBody())
                throw std::runtime_error("managed static constructor requires a definition in this translation unit");
            if (record_annotated(r, "cli.value")) {
                if (isa<CXXRecordDecl>(r->getDeclContext())) throw std::runtime_error("nested managed value types are not enabled yet");
                if (r->getNumBases() || r->isPolymorphic()) throw std::runtime_error("value types with bases or virtual methods are not enabled yet");
                for (auto* field : r->fields()) if (field->getType()->isPointerType() && !handle_target(field->getType())) throw std::runtime_error("native pointer fields in value types are not enabled yet");
            }
            unsigned classes = 0;
            for (auto& b : r->bases()) if (!is_interface(b.getType()->getAsCXXRecordDecl())) ++classes;
            if (classes > 1 || (is_interface(r) && classes)) throw std::runtime_error("a managed class permits one class base and interfaces only");
            if (is_interface(r) && !r->field_empty()) throw std::runtime_error("interface instance fields are forbidden");
            for (const auto& base : r->bases()) record_name(base.getType()->getAsCXXRecordDecl());
            for (auto* field : r->fields()) { if (field->getType()->isReferenceType() || !interior_element(field->getType()).isNull()) throw std::runtime_error("tracking reference and managed pointer fields are not enabled yet"); type(field->getType()); validate_declarator(field->getTypeSourceInfo()); }
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
            if (auto* f = dyn_cast<FunctionDecl>(decl)) VisitFunctionDecl(f);
            else if (auto* r = dyn_cast<CXXRecordDecl>(decl)) {
                if (r->isThisDeclarationADefinition()) { VisitCXXRecordDecl(r); collect(r); }
            } else if (auto* ns = dyn_cast<NamespaceDecl>(decl)) collect(ns);
            else if (auto* linkage = dyn_cast<LinkageSpecDecl>(decl)) collect(linkage);
            else if (auto* tmpl = dyn_cast<FunctionTemplateDecl>(decl)) {
                if (llvm::StringRef(tmpl->getNameAsString()).starts_with("__cli_")) continue;
                for (auto* specialization : tmpl->specializations()) if (specialization->isUsed()) VisitFunctionDecl(specialization);
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
        collect(context.getTranslationUnitDecl());
        bool entry = false;
        for (auto* f : functions) {
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
                il << "  .method public hidebysig static pinvokeimpl(" << quote(native_library) << " as " << quote(f->getNameAsString())
                   << " cdecl) " << type(f->getReturnType()) << " '" << f->getNameAsString() << "'(";
                for (unsigned j = 0; j < f->getNumParams(); ++j) { if (j) il << ", "; il << type(f->getParamDecl(j)->getType()); }
                il << ") cil managed preservesig {}\n"; return;
            }
            std::string flags;
            if (method && method->isVirtual()) {
                bool class_override = false;
                for (auto* base : method->overridden_methods()) if (!is_interface(base->getParent())) class_override = true;
                flags = "virtual " + std::string(class_override ? "" : "newslot ") + (method->isPureVirtual() ? "abstract " : "");
                if (method->hasAttr<FinalAttr>()) flags += "final ";
            }
            il << "  .method " << (cctor || (isa<CXXMethodDecl>(f) && f->getAccess() == AS_private) ? "private" : f->getAccess() == AS_protected ? "family" : annotation(f, "cli.access:") == "assembly" ? "assembly" : "public") << " hidebysig " << flags
               << (cctor || isa<CXXConstructorDecl>(f) ? "specialname rtspecialname " : !annotation(f, "cli.accessor:").empty() ? "specialname " : "") << (body.instance ? "instance " : "static ")
               << type(f->getReturnType()) << " '" << body.name << "'(";
            for (unsigned j = 0; j < f->getNumParams(); ++j) { if (j) il << ", "; il << type(f->getParamDecl(j)->getType()); }
            il << ") cil managed {\n";
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
        for (auto* r : records) {
            auto* parent = class_base(r);
            auto layout = layout_kind(r), charset = layout_constant(r, "cli.layout.CharSet", 1);
            il << ".class " << (annotation(r, "cli.visibility:") == "private" ? "private " : "public ") << (is_interface(r) ? "interface abstract " :
                std::string(layout == 2 ? "explicit " : layout == 0 ? "sequential " : "auto ") + (charset == 3 ? "unicode " : charset == 4 ? "autochar " : "ansi "))
               << (!is_interface(r) && (annotated(r, "cli.abstract") || r->isAbstract()) ? "abstract " : "")
               << ((annotated(r, "cli.sealed") || annotated(r, "cli.value")) ? "sealed " : "") << record_name(r);
            if (!is_interface(r)) il << " extends " << (annotated(r, "cli.value") ? "[System.Runtime]System.ValueType" : parent ? record_token(parent) : "[System.Runtime]System.Object");
            bool first_interface = true;
            for (auto& base : r->bases()) if (is_interface(base.getType()->getAsCXXRecordDecl())) {
                il << (first_interface ? " implements " : ", ") << record_token(base.getType()->getAsCXXRecordDecl()); first_interface = false;
            }
            il << " {\n";
            for (auto* member : r->decls()) if (auto* property = dyn_cast<MSPropertyDecl>(member); property && property->getNameAsString() == "__cli_default")
                il << "  .custom instance void [System.Runtime]System.Reflection.DefaultMemberAttribute::.ctor(string) = (01 00 04 49 74 65 6d 00 00)\n";
            if (annotated(r, "cli.layout.Pack")) il << "  .pack " << layout_constant(r, "cli.layout.Pack", 0) << "\n";
            if (annotated(r, "cli.layout.Size")) il << "  .size " << layout_constant(r, "cli.layout.Size", 0) << "\n";
            for (auto* field : r->fields()) {
                il << "  .field ";
                if (layout == 2) il << '[' << layout_constant(field, "cli.field.offset", -1) << "] ";
                il << (field->getAccess() == AS_private ? "private " : field->getAccess() == AS_protected ? "family " : annotation(field, "cli.access:") == "assembly" ? "assembly " : "public ")
                    << type(field->getType()) << " '" << field->getNameAsString() << "'\n";
            }
            for (auto* member : r->decls()) if (auto* field = dyn_cast<VarDecl>(member); field && field->isStaticDataMember()) {
                if (!annotation(field, "cli.static.property:").empty()) continue;
                if (field->getType()->isReferenceType() || !interior_element(field->getType()).isNull()) throw std::runtime_error("managed pointer static fields are not enabled yet");
                validate_declarator(field->getTypeSourceInfo());
                il << "  .field " << (field->getAccess() == AS_private ? "private " : field->getAccess() == AS_protected ? "family " : annotation(field, "cli.access:") == "assembly" ? "assembly " : "public ")
                    << "static " << type(field->getType()) << " '" << field->getNameAsString() << "'\n";
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
            il << "}\n";
        }
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
