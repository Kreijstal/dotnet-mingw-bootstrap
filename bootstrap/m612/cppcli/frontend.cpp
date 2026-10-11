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
    return r && (annotated(r, "cli.interface") || (r->getDefinition() && annotated(r->getDefinition(), "cli.interface")));
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
};
struct Metadata {
    std::map<std::string, ImportedMethod> methods;
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
                    m.getObject("Return")->getString("Il")->str(), {}, m.getBoolean("Static").value_or(false)};
                for (auto& p : *m.getArray("Parameters")) im.parameters.push_back(p.getAsObject()->getString("Il")->str());
                methods.emplace(m.getString("Id")->str(), std::move(im));
            }
        }
    }
};

struct Options {
    std::string metadata, output, evidence;
    std::string assembly = "cli_program", target = "exe", native_library;
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
            else return false;
        }
        return (!require_metadata || !metadata.empty()) && !output.empty() && (target == "exe" || target == "library");
    }
};
void report(CompilerInstance& ci, SourceLocation loc, const std::string& message) {
    unsigned id = ci.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, "C++/CLI: %0");
    ci.getDiagnostics().Report(loc, id) << message;
}

struct Lexeme { std::string text, file; unsigned line; tok::TokenKind kind; };
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
        references.push_back(llvm::json::Object{{"path", text.substr(1, text.size() - 2)},
            {"source", loc.isValid() ? loc.getFilename() : ""}});
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
            auto buffer = sm.getBufferData(file); auto start = sm.getLocForStartOfFile(file);
            Lexer lexer(start, ci.getLangOpts(), buffer.begin(), buffer.begin(), buffer.end());
            std::vector<std::pair<unsigned, unsigned>> replacements;
            Token token;
            while (!lexer.LexFromRawLexer(token)) {
                if (token.is(tok::hash) && token.isAtStartOfLine()) {
                    Token name; lexer.LexFromRawLexer(name);
                    if (Lexer::getSpelling(name, sm, ci.getLangOpts()) == "using")
                        replacements.emplace_back(sm.getFileOffset(name.getLocation()), name.getLength());
                }
            }
            std::string text = buffer.str();
            for (auto it = replacements.rbegin(); it != replacements.rend(); ++it) text.replace(it->first, it->second, "pragma cli_reference");
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
                tokens.push_back({pp.getSpelling(token), loc.getFilename(), loc.getLine(), token.getKind()});
            }
            if (ci.getDiagnostics().hasErrorOccurred()) return;
            // Only managed declarations and allocation markers are rewritten.
            // Expressions/statements remain C++ and are parsed by Clang later.
            for (size_t i = 0; i + 2 < tokens.size(); ++i)
                if ((tokens[i].text == "ref" || tokens[i].text == "interface" || tokens[i].text == "value") && (tokens[i + 1].text == "class" || tokens[i + 1].text == "struct")) metadata.short_types.insert(tokens[i + 2].text);
            std::ostringstream out;
            std::string file;
            unsigned line = 0;
            auto emit = [&](const Lexeme& at, const std::string& text) {
                if (file != at.file || line != at.line) {
                    out << "\n#line " << at.line << " " << quote(at.file) << "\n";
                    file = at.file; line = at.line;
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
            std::set<size_t> interface_virtual, interface_pure;
            std::map<size_t, std::string> value_defaults;
            for (size_t i = 0; i + 2 < tokens.size(); ++i) {
                if ((tokens[i].text != "ref" && tokens[i].text != "interface" && tokens[i].text != "value") || (tokens[i + 1].text != "class" && tokens[i + 1].text != "struct")) continue;
                std::string name = tokens[i + 2].text;
                size_t open = i + 3;
                while (open < tokens.size() && tokens[open].text != "{" && tokens[open].text != ";") ++open;
                if (open == tokens.size() || tokens[open].text != "{") continue;
                if (tokens[i].text == "value") value_defaults[open] = "public: __attribute__((annotate(\"cli.value.zero\"))) " + name + "() {} " + (tokens[i+1].text == "struct" ? "public:" : "private:");
                int depth = 1;
                std::string access = tokens[i].text == "interface" || tokens[i + 1].text == "struct" ? "public" : "private";
                for (size_t j = open + 1; j < tokens.size() && depth; ++j) {
                    if (depth == 1 && j + 1 < tokens.size() && tokens[j + 1].text == ":" &&
                        (tokens[j].text == "public" || tokens[j].text == "private" || tokens[j].text == "protected")) access = tokens[j].text;
                    class_context[j] = name; class_access[j] = access;
                    if (tokens[j].text == "{") ++depth;
                    else if (tokens[j].text == "}") --depth;
                    else if (depth == 1 && tokens[j].text == "(" && j && tokens[j - 1].kind == tok::identifier) {
                        size_t end = j + 1; int parentheses = 1; bool string_parameter = false;
                        while (end < tokens.size() && parentheses) {
                            if (tokens[end].text == "(") ++parentheses;
                            if (tokens[end].text == ")") --parentheses;
                            if (tokens[end].text == "String" && end + 1 < tokens.size() && tokens[end + 1].text == "^") string_parameter = true;
                            ++end;
                        }
                        if (tokens[i].text == "value" && tokens[j-1].text == name && end == j+2)
                            throw std::runtime_error("user-defined zero-argument value constructors are not supported; default storage is zero initialized");
                        if (string_parameter) managed_string_calls.insert(name + "::" + tokens[j - 1].text);
                        if (tokens[i].text == "interface") {
                            if (end >= tokens.size() || tokens[end].text != ";") throw std::runtime_error("interface instance methods currently require a declaration without a body");
                            size_t start = j - 1;
                            while (start > open + 1 && tokens[start - 1].text != ";" && tokens[start - 1].text != ":") --start;
                            bool already_virtual = false;
                            for (size_t k = start; k < j; ++k) already_virtual |= tokens[k].text == "virtual";
                            if (!already_virtual) interface_virtual.insert(start);
                            interface_pure.insert(end);
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
                if (end == i + 1 || end == tokens.size() || tokens[end].text != "(")
                    throw std::runtime_error("gcnew currently requires a named type and parenthesized constructor arguments");
                std::string type;
                for (size_t j = i + 1; j < end; ++j) type += tokens[j].text;
                emit(t, "__cli_new<" + type + ">");
                i = end - 1;
                return true;
            };
            for (size_t i = 0; i < tokens.size(); ++i) {
                auto& t = tokens[i];
                if (value_defaults.count(i)) { emit(t, "{ " + value_defaults[i]); continue; }
                if (cli_structural.count(i)) { emit(t, cli_structural[i]); continue; }
                if (interface_virtual.count(i)) emit(t, "virtual");
                if (interface_pure.count(i)) emit(t, "= 0");
                if (!class_context[i].empty() && t.text == "abstract" && i && tokens[i - 1].text == ")") { emit(t, "= 0"); continue; }
                if (!class_context[i].empty() && t.text == "sealed" && i && (tokens[i - 1].text == ")" || tokens[i - 1].text == "override")) { emit(t, "final"); continue; }
                if ((t.text == "public" || t.text == "private") && i + 3 < tokens.size() && tokens[i + 1].text == "enum" &&
                    (tokens[i + 2].text == "class" || tokens[i + 2].text == "struct")) {
                    emit(t, "enum class __attribute__((annotate(\"cli.enum\"), annotate(\"cli.visibility:" + t.text + "\")))");
                    i += 2; continue;
                }
                if (t.text == "property" && !class_context[i].empty()) {
                    size_t name = i + 1;
                    while (name + 1 < tokens.size() && !(tokens[name].kind == tok::identifier &&
                        (tokens[name + 1].text == "{" || tokens[name + 1].text == ";"))) ++name;
                    if (name + 1 == tokens.size()) throw std::runtime_error("unsupported property declarator");
                    std::string property = tokens[name].text, property_type;
                    for (size_t j = i + 1; j < name; ++j) property_type += tokens[j].text + " ";
                    size_t end = name + 1; bool getter = false, setter = false;
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
                    emit(t, attribute + ")) " + property_type + property + ";");
                    if (tokens[name + 1].text == ";") {
                        emit(t, "private: " + property_type + " __cli_backing_" + property + "; " + class_access[i] + ":");
                        emit(t, property_type + " __attribute__((annotate(\"cli.accessor:get\"))) get_" + property + "() { return __cli_backing_" + property + "; }");
                        emit(t, "void __attribute__((annotate(\"cli.accessor:set\"))) set_" + property + "(" + property_type + " value) { __cli_backing_" + property + " = value; }");
                    } else {
                        int depth = 1;
                        for (size_t j = name + 2; j + 1 < end; ++j) {
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
                        bool start = true;
                        while (end < tokens.size() && tokens[end].text != "{" && tokens[end].text != ";") {
                            if (start && tokens[end].text != "public" && tokens[end].text != "protected" && tokens[end].text != "private") emit(tokens[end], "public");
                            if (tokens[end].text == "protected" || tokens[end].text == "private") throw std::runtime_error("managed bases require public inheritance");
                            emit(tokens[end], tokens[end].text); start = tokens[end].text == ","; ++end;
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
                if (((t.text == "generic" || t.text == "pin_ptr" || t.text == "interior_ptr") && next == "<") ||
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
    struct LoopLabels { std::string stop, next; unsigned depth; };
    std::vector<LoopLabels> loop_labels;
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
            if (!r || record_annotated(r, "cli.value")) return nullptr;
            auto* definition = r->getDefinition();
            if (record_annotated(r, "cli.ref") || !annotation(r, "cli.external:").empty() ||
                (definition && (annotated(definition, "cli.ref") || !annotation(definition, "cli.external:").empty()))) return r;
            return nullptr;
        }
        auto* rec = type->getAsCXXRecordDecl();
        auto* spec = dyn_cast_or_null<ClassTemplateSpecializationDecl>(rec);
        if (!spec || spec->getSpecializedTemplate()->getName() != "__cli_handle") return nullptr;
        return spec->getTemplateArgs()[0].getAsType()->getAsCXXRecordDecl();
    }
    QualType array_element(const CXXRecordDecl* r) const {
        if (!record_annotated(r, "cli.array")) return {};
        auto* specialization = dyn_cast<ClassTemplateSpecializationDecl>(r);
        if (!specialization || specialization->getTemplateArgs().size() != 2 || specialization->getTemplateArgs()[1].getAsIntegral() != 1)
            throw std::runtime_error("only rank-one managed arrays are supported");
        return specialization->getTemplateArgs()[0].getAsType();
    }
    std::string record_name(const CXXRecordDecl* record) const {
        std::string ext = annotation(record, "cli.external:");
        if (ext.empty() && record->getDefinition()) ext = annotation(record->getDefinition(), "cli.external:");
        if (!ext.empty()) {
            auto bar = ext.find('|');
            return "[" + ext.substr(0, bar) + "]'" + ext.substr(bar + 1) + "'";
        }
        if (!record_annotated(record, "cli.ref") && !record_annotated(record, "cli.value"))
            throw std::runtime_error("native C++ class is not a managed type: " + record->getQualifiedNameAsString());
        std::string name = record->getQualifiedNameAsString();
        for (size_t at; (at = name.find("::")) != std::string::npos;) name.replace(at, 2, ".");
        return "'" + name + "'";
    }
    std::string enum_name(const EnumDecl* enumeration) const {
        std::string ext = annotation(enumeration, "cli.external:");
        if (ext.empty() && enumeration->getDefinition()) ext = annotation(enumeration->getDefinition(), "cli.external:");
        if (!ext.empty()) {
            auto bar = ext.find('|'); return "[" + ext.substr(0, bar) + "]'" + ext.substr(bar + 1) + "'";
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
            QualType element = array_element(r);
            if (!element.isNull()) return type(element) + "[]";
            std::string name = record_name(r);
            if (name == "[System.Runtime]'System.String'") return "string";
            if (name == "[System.Runtime]'System.Object'") return "object";
            return "class " + name;
        }
        throw std::runtime_error("unsupported CLI type: " + t.getAsString());
    }
    std::string reference_token(QualType t) const {
        auto* r = handle_target(t);
        if (!r) throw std::runtime_error("reference token requires a managed handle");
        return array_element(r).isNull() ? record_name(r) : type(t);
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
            auto pointer = loc.getAs<PointerTypeLoc>();
            if (pointer.isNull() || !handle_target(pointer.getType())) continue;
            SourceLocation star = sm.getSpellingLoc(pointer.getStarLoc());
            if (star.isValid() && sm.isWrittenInMainFile(star) && *sm.getCharacterData(star) == '*')
                throw std::runtime_error("native '*' pointers to managed reference classes are not supported; declare a '^' handle");
        }
    }
    std::string field_signature(const FieldDecl* f) const {
        auto* parent = dyn_cast<CXXRecordDecl>(f->getParent());
        if (!parent) throw std::runtime_error("field has no managed parent");
        return type(f->getType()) + " " + record_name(parent) + "::'" + f->getNameAsString() + "'";
    }
    std::string method_name(const FunctionDecl* f) const {
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
        if (method) owner = record_name(method->getParent()) + "::";
        std::string result = isa<CXXConstructorDecl>(f) ? "void" : type(f->getReturnType());
        std::string name = method_name(f);
        std::string sig = (method && !method->isStatic() ? "instance " : "") + result + " " + owner + "'" + name + "'(";
        for (unsigned i = 0; i < f->getNumParams(); ++i) {
            if (i) sig += ", ";
            sig += type(f->getParamDecl(i)->getType());
        }
        return sig + ")";
    }
    std::string imported_signature(const ImportedMethod& m) const {
        std::string sig = (m.is_static ? "" : "instance ") + m.result + " [" + m.assembly + "]'" + m.owner + "'::'" + m.name + "'(";
        for (size_t i = 0; i < m.parameters.size(); ++i) { if (i) sig += ", "; sig += m.parameters[i]; }
        return sig + ")";
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
        if (auto* d = dyn_cast<DeclRefExpr>(e)) {
            if (auto* p = dyn_cast<ParmVarDecl>(d->getDecl())) op("ldarga " + std::to_string(parameters.at(p)), 0, 1);
            else if (auto* v = dyn_cast<VarDecl>(d->getDecl())) op("ldloca " + std::to_string(locals.at(v)), 0, 1);
            else fail(e, "value address requires a local or parameter");
            return;
        }
        if (auto* member = dyn_cast<MemberExpr>(e)) {
            auto* field = dyn_cast<FieldDecl>(member->getMemberDecl());
            if (!field) fail(e, "value address requires an instance field");
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
        if (value_target(e->getType())) address(e);
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
        if (auto* p = dyn_cast<ParmVarDecl>(d)) { op("ldarg " + std::to_string(parameters.at(p)), 0, 1); return; }
        if (auto* v = dyn_cast<VarDecl>(d)) {
            auto it = locals.find(v);
            if (it == locals.end()) fail(at, "global/static storage is not supported yet");
            op("ldloc " + std::to_string(it->second), 0, 1); return;
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
            unsigned slot = temporary(type(field->getType()));
            op("stloc " + std::to_string(slot), 1);
            emit_receiver(member->getBase()); op("ldloc " + std::to_string(slot), 0, 1);
            op("stfld " + field_signature(field), 2);
            if (retain) op("ldloc " + std::to_string(slot), 0, 1);
            return;
        }
        auto* d = dyn_cast<DeclRefExpr>(target);
        if (!d) fail(target, "assignment target is not a local or parameter");
        if (retain) op("dup", 1, 2);
        if (auto* p = dyn_cast<ParmVarDecl>(d->getDecl())) op("starg " + std::to_string(parameters.at(p)), 1);
        else if (auto* v = dyn_cast<VarDecl>(d->getDecl())) op("stloc " + std::to_string(locals.at(v)), 1);
        else fail(target, "unsupported assignment target");
    }
    void binary(const BinaryOperator* b) {
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
                if (record_name(target) != "[System.Runtime]'System.Object'") fail(c, "boxing currently requires an Object handle target");
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
            if (!annotation(target, "cli.external:").empty() && annotation(selected, "cli.import:").empty())
                fail(c, "imported class has no public metadata constructor");
            if (selected->getNumParams() != c->getNumArgs()) fail(c, "default constructor arguments are not supported yet");
            for (unsigned i = 0; i < c->getNumArgs(); ++i) {
                if (type(c->getArg(i)->getType()) != type(selected->getParamDecl(i)->getType())) fail(c, "constructor argument conversion is not enabled yet");
                expr(c->getArg(i));
            }
            op("newobj " + signature(selected), c->getNumArgs(), 1);
            return;
        }
        auto* m = dyn_cast<CXXMethodDecl>(f);
        bool instance = m && !m->isStatic();
        if (instance) {
            auto* member = dyn_cast<CXXMemberCallExpr>(c);
            if (!member) fail(c, "unsupported instance call shape");
            emit_receiver(member->getImplicitObjectArgument());
        }
        for (auto* a : c->arguments()) expr(a);
        std::string id = annotation(f, "cli.import:");
        std::string sig;
        if (!id.empty()) {
            auto imported = metadata.methods.find(id);
            if (imported == metadata.methods.end()) fail(c, "imported member is absent from metadata");
            sig = imported_signature(imported->second);
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
            if (c->getCastKind() == CK_NullToPointer && handle_target(c->getType())) { op("ldnull", 0, 1); return; }
            expr(c->getSubExpr());
            if (c->getCastKind() == CK_PointerToBoolean && handle_target(c->getSubExpr()->getType())) {
                op("ldnull", 0, 1); op("ceq", 2, 1); op("ldc.i4.0", 0, 1); op("ceq", 2, 1); return;
            }
            if (c->getCastKind() == CK_IntegralToBoolean) { op("ldc.i4.0", 0, 1); op("ceq", 2, 1); op("ldc.i4.0", 0, 1); op("ceq", 2, 1); }
            else if (c->getCastKind() == CK_IntegralCast || c->getCastKind() == CK_IntegralToFloating || c->getCastKind() == CK_FloatingCast || c->getCastKind() == CK_FloatingToIntegral) {
                numeric_conversion(c->getType());
            } else if (c->getCastKind() != CK_LValueToRValue && c->getCastKind() != CK_NoOp && c->getCastKind() != CK_ArrayToPointerDecay && c->getCastKind() != CK_FunctionToPointerDecay &&
                       !(c->getCastKind() == CK_ConstructorConversion && handle_target(c->getType())) &&
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
            if (value_target(c->getType())) {
                if (annotated(c->getConstructor(), "cli.value.zero")) {
                    unsigned slot = temporary(type(c->getType()));
                    op("ldloca " + std::to_string(slot), 0, 1); op("initobj " + record_name(value_target(c->getType())), 1);
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
            if (u->isIncrementDecrementOp()) {
                if (handle_target(u->getSubExpr()->getType())) fail(u, "managed handles cannot be incremented");
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
            if (!v || v->isStaticLocal() || v->getType()->isReferenceType()) fail(s, "unsupported local declaration");
            type(v->getType());
            validate_declarator(v->getTypeSourceInfo());
            locals.emplace(v, locals.size());
        }
        for (auto* child : s->children()) collect_locals(child);
    }
    void statement(const Stmt* s) {
        if (!s || isa<NullStmt>(s)) return;
        int before = stack;
        if (auto* block = dyn_cast<CompoundStmt>(s)) { for (auto* child : block->body()) statement(child); }
        else if (auto* d = dyn_cast<DeclStmt>(s)) {
            for (auto* decl : d->decls()) {
                auto* v = cast<VarDecl>(decl);
                if (!v->hasInit()) fail(s, "uninitialized locals are not supported yet");
                expr(v->getInit()); op("stloc " + std::to_string(locals.at(v)), 1);
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
        } else if (auto* w = dyn_cast<WhileStmt>(s)) {
            if (w->getConditionVariable()) fail(s, "while condition variable is not supported yet");
            auto check = label(), done = label(); mark(check);
            expr(w->getCond()); op("brfalse " + done, 1);
            loop_labels.push_back({done, check, exception_depth}); statement(w->getBody()); loop_labels.pop_back();
            op("br " + check); mark(done);
        } else if (auto* f = dyn_cast<ForStmt>(s)) {
            if (f->getConditionVariable()) fail(s, "for condition variable is not supported yet");
            statement(f->getInit()); auto check = label(), inc = label(), done = label(); mark(check);
            if (f->getCond()) { expr(f->getCond()); op("brfalse " + done, 1); }
            loop_labels.push_back({done, inc, exception_depth}); statement(f->getBody()); loop_labels.pop_back();
            mark(inc); statement(f->getInc()); op("br " + check); mark(done);
        } else if (isa<BreakStmt>(s) || isa<ContinueStmt>(s)) {
            if (loop_labels.empty()) fail(s, "loop control outside a loop");
            auto& target = loop_labels.back();
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
            if ((!t->isIntegerType() && !t->isRealFloatingType()) || t->isBooleanType())
                throw std::runtime_error("native bridge signatures currently require integer/floating scalars; bool and managed/pointer marshalling are not enabled");
            type(t);
        };
        valid(f->getReturnType(), true);
        for (auto* parameter : f->parameters()) valid(parameter->getType(), false);
        auto convention = f->getType()->castAs<FunctionType>()->getCallConv();
        if (f->isVariadic() || f->hasAttr<AsmLabelAttr>() || (convention != CC_C && convention != CC_Win64))
            throw std::runtime_error("native bridge requires a non-variadic extern-C function using the platform C ABI");
    }
    FunctionIR function(FunctionDecl* f) {
        if (native_function(f)) {
            validate_native_signature(f);
            return {method_name(f), "", type(f->getReturnType()), false, 0, "", ""};
        }
        current = f; locals.clear(); parameters.clear(); temporary_types.clear(); opaque_values.clear(); code.str(""); code.clear(); stack = 0; max_stack = 0;
        label_count = 0; loop_labels.clear(); exception_depth = catch_depth = finally_depth = 0; return_label.clear(); return_slot_set = false;
        auto* method = dyn_cast<CXXMethodDecl>(f);
        bool instance = method && !method->isStatic();
        if (method && method->isPureVirtual()) return {method_name(f), record_name(method->getParent()), type(f->getReturnType()), instance, 0, "", ""};
        if (f->isVariadic()) throw std::runtime_error("variadic managed functions are unsupported");
        for (unsigned i = 0; i < f->getNumParams(); ++i) parameters[f->getParamDecl(i)] = i + unsigned(instance);
        collect_locals(f->getBody());
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
                op("ldarg.0", 0, 1); op("initobj " + record_name(method->getParent()), 1);
            } else if (!base_initialized) { op("ldarg.0", 0, 1); op("call instance void [System.Runtime]System.Object::.ctor()", 1); }
            for (auto* init : constructor->inits()) if (init->isMemberInitializer()) {
                op("ldarg.0", 0, 1); expr(init->getInit()); op("stfld " + field_signature(init->getMember()), 2);
            }
        }
        statement(f->getBody());
        if (f->getReturnType()->isVoidType()) op("ret");
        else if (value_target(f->getReturnType())) {
            unsigned slot = temporary(type(f->getReturnType()));
            op("ldloca " + std::to_string(slot), 0, 1); op("initobj " + record_name(value_target(f->getReturnType())), 1);
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
            declarations += "[" + std::to_string(i) + "] " + type(ordered[i]->getType()) + " 'local" + std::to_string(i) + "'";
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
        if (annotated(f, "cli.value.zero")) return true;
        if (isa<CXXConstructorDecl>(f) && method && is_interface(method->getParent())) return true;
        if (f->isImplicit() && method && annotated(method->getParent(), "cli.abstract") && annotated(method->getParent(), "cli.sealed")) return true;
        if ((f->isImplicit() && !(isa<CXXConstructorDecl>(f) && cast<CXXConstructorDecl>(f)->isDefaultConstructor())) || (!f->doesThisDeclarationHaveABody() && !(method && method->isPureVirtual()) && !native_function(f)) || llvm::StringRef(f->getNameAsString()).starts_with("__cli_")) return true;
        if (auto* m = dyn_cast<CXXMethodDecl>(f)) if (!annotation(m->getParent(), "cli.external:").empty()) return true;
        if (isa<CXXDestructorDecl>(f)) throw std::runtime_error("managed destructors/finalizers are not enabled yet");
        if (method && f->isOverloadedOperator()) throw std::runtime_error("managed operator metadata is not enabled yet");
        if (method) for (auto* base : method->overridden_methods()) {
            if (type(method->getReturnType()) != type(base->getReturnType())) throw std::runtime_error("covariant managed return metadata is not enabled yet");
            if (!is_interface(base->getParent()) && !method->hasAttr<OverrideAttr>()) throw std::runtime_error("managed class overrides require an explicit override specifier");
            if (is_interface(base->getParent()) && method->getAccess() != AS_public) throw std::runtime_error("implicit interface implementations must be public");
        }
        validate_declarator(f->getTypeSourceInfo());
        for (auto* p : f->parameters()) validate_declarator(p->getTypeSourceInfo());
        if (f->getType()->isDependentType() || f->getTemplatedKind() == FunctionDecl::TK_FunctionTemplate)
            throw std::runtime_error("unspecialized dependent function reached IL emission");
        if (seen.insert(f->getCanonicalDecl()).second) functions.push_back(f);
        return true;
    }
    bool VisitCXXRecordDecl(CXXRecordDecl* r) {
        if (r->isThisDeclarationADefinition() && (annotated(r, "cli.ref") || annotated(r, "cli.value")) && !annotated(r, "cli.array") && annotation(r,"cli.external:").empty()) {
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
            for (auto* field : r->fields()) { type(field->getType()); validate_declarator(field->getTypeSourceInfo()); }
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
        il << metadata.externs << ".assembly '" << options.assembly << "' {}\n.module '" << options.assembly << ".dll'\n";
        if (!native_library.empty()) il << ".module extern '" << native_library << "'\n";
        auto emit_function = [&](size_t i) {
            auto* f = functions[i]; auto& body = ir[i];
            auto* method = dyn_cast<CXXMethodDecl>(f);
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
            il << "  .method " << (isa<CXXMethodDecl>(f) && f->getAccess() == AS_private ? "private" : f->getAccess() == AS_protected ? "family" : "public") << " hidebysig " << flags
               << (isa<CXXConstructorDecl>(f) ? "specialname rtspecialname " : !annotation(f, "cli.accessor:").empty() ? "specialname " : "") << (body.instance ? "instance " : "static ")
               << type(f->getReturnType()) << " '" << body.name << "'(";
            for (unsigned j = 0; j < f->getNumParams(); ++j) { if (j) il << ", "; il << type(f->getParamDecl(j)->getType()); }
            il << ") cil managed {\n";
            if (f->getNameAsString() == "main" && body.owner.empty() && options.target == "exe") il << "    .entrypoint\n";
            if (method && method->isPureVirtual()) { il << "  }\n"; return; }
            il << "    .maxstack " << body.max_stack << "\n";
            if (!body.locals.empty()) il << "    .locals init (" << body.locals << ")\n";
            il << body.code << "  }\n";
        };
        for (auto* enumeration : enums) {
            il << ".class " << (annotation(enumeration, "cli.visibility:") == "private" ? "private" : "public")
               << " auto ansi sealed " << enum_name(enumeration) << " extends [System.Runtime]System.Enum {\n"
               << "  .field public specialname rtspecialname " << type(enumeration->getIntegerType()) << " value__\n";
            for (auto* member : enumeration->enumerators()) {
                llvm::SmallString<32> value; member->getInitVal().toString(value, 10);
                il << "  .field public static literal valuetype " << enum_name(enumeration) << " '" << member->getNameAsString()
                   << "' = " << type(enumeration->getIntegerType()) << "(" << value.str().str() << ")\n";
            }
            il << "}\n";
        }
        for (auto* r : records) {
            auto* parent = class_base(r);
            il << ".class public " << (is_interface(r) ? "interface abstract " : "auto ansi ")
               << (!is_interface(r) && (annotated(r, "cli.abstract") || r->isAbstract()) ? "abstract " : "")
               << ((annotated(r, "cli.sealed") || annotated(r, "cli.value")) ? "sealed " : "") << record_name(r);
            if (!is_interface(r)) il << " extends " << (annotated(r, "cli.value") ? "[System.Runtime]System.ValueType" : parent ? record_name(parent) : "[System.Runtime]System.Object");
            bool first_interface = true;
            for (auto& base : r->bases()) if (is_interface(base.getType()->getAsCXXRecordDecl())) {
                il << (first_interface ? " implements " : ", ") << record_name(base.getType()->getAsCXXRecordDecl()); first_interface = false;
            }
            il << " {\n";
            for (auto* field : r->fields()) il << "  .field " << (field->getAccess() == AS_private ? "private " : field->getAccess() == AS_protected ? "family " : "public ")
                << type(field->getType()) << " '" << field->getNameAsString() << "'\n";
            for (size_t i = 0; i < functions.size(); ++i) if (ir[i].owner == record_name(r)) emit_function(i);
            for (auto* member : r->decls()) if (auto* property = dyn_cast<MSPropertyDecl>(member)) {
                il << "  .property instance " << type(property->getType()) << " '" << property->getNameAsString() << "'() {\n";
                for (auto* method : r->methods()) {
                    if (property->getGetterId() && method->getIdentifier() == property->getGetterId()) il << "    .get " << signature(method) << "\n";
                    if (property->getSetterId() && method->getIdentifier() == property->getSetterId()) il << "    .set " << signature(method) << "\n";
                }
                il << "  }\n";
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
