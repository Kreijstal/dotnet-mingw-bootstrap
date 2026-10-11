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
    std::ofstream out(path, std::ios::binary);
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
    std::string assembly = "cli_program", target = "exe";
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
                if ((tokens[i].text == "ref" || tokens[i].text == "interface") && (tokens[i + 1].text == "class" || tokens[i + 1].text == "struct")) metadata.short_types.insert(tokens[i + 2].text);
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
            for (size_t i = 0; i + 2 < tokens.size(); ++i) {
                if ((tokens[i].text != "ref" && tokens[i].text != "interface") || (tokens[i + 1].text != "class" && tokens[i + 1].text != "struct")) continue;
                std::string name = tokens[i + 2].text;
                size_t open = i + 3;
                while (open < tokens.size() && tokens[open].text != "{" && tokens[open].text != ";") ++open;
                if (open == tokens.size() || tokens[open].text != "{") continue;
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
            for (size_t i = 0; i < tokens.size(); ++i) {
                auto& t = tokens[i];
                if (interface_virtual.count(i)) emit(t, "virtual");
                if (interface_pure.count(i)) emit(t, "= 0");
                if (!class_context[i].empty() && t.text == "abstract" && i && tokens[i - 1].text == ")") { emit(t, "= 0"); continue; }
                if (!class_context[i].empty() && t.text == "sealed" && i && (tokens[i - 1].text == ")" || tokens[i - 1].text == "override")) { emit(t, "final"); continue; }
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
                    (tokens[i + 1].text == "ref" || tokens[i + 1].text == "interface") &&
                    (tokens[i + 2].text == "class" || tokens[i + 2].text == "struct")) continue;
                if ((t.text == "ref" || t.text == "interface") && i + 2 < tokens.size() &&
                    (tokens[i + 1].text == "class" || tokens[i + 1].text == "struct")) {
                    bool interface = t.text == "interface";
                    std::string attributes = "annotate(\"cli.ref\")";
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
                    } else if (!interface && end < tokens.size() && tokens[end].text == "{") emit(t, ": public ::System::Object");
                    i = end - 1;
                    continue;
                }
                if (t.text == "gcnew") {
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
                        throw std::runtime_error("unsupported gcnew declarator (arrays and generics are not enabled yet)");
                    std::string type;
                    for (size_t j = i + 1; j < end; ++j) type += tokens[j].text;
                    emit(t, "__cli_new<" + type + ">");
                    i = end - 1;
                    continue;
                }
                if (tok::isStringLiteral(t.kind) && call_context[i] && i > 0 && i + 1 < tokens.size() &&
                    (tokens[i - 1].text == "(" || tokens[i - 1].text == ",") &&
                    (tokens[i + 1].text == ")" || tokens[i + 1].text == ",")) {
                    emit(t, "__cli_literal(" + t.text + ")");
                    continue;
                }
                // Keep ^ unchanged. Only the private Clang parser can resolve
                // declarator/type context correctly after C++ scope lookup.
                std::string next = i + 1 < tokens.size() ? tokens[i + 1].text : "";
                if ((t.text == "value" && (next == "class" || next == "struct")) ||
                    ((t.text == "generic" || t.text == "pin_ptr" || t.text == "interior_ptr") && next == "<") ||
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
    std::set<const FunctionDecl*> seen;
    std::map<const VarDecl*, unsigned> locals;
    std::map<const ParmVarDecl*, unsigned> parameters;
    std::ostringstream code;
    FunctionDecl* current = nullptr;
    unsigned label_count = 0, max_stack = 0;
    int stack = 0;
    std::vector<std::pair<std::string, std::string>> loop_labels;
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
    const CXXRecordDecl* handle_target(QualType type) const {
        if (type->isPointerType()) {
            auto* r = type->getPointeeType()->getAsCXXRecordDecl();
            if (!r) return nullptr;
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
        if (!annotated(record, "cli.ref") && !(record->getDefinition() && annotated(record->getDefinition(), "cli.ref")))
            throw std::runtime_error("native C++ class is not a managed type: " + record->getQualifiedNameAsString());
        std::string name = record->getQualifiedNameAsString();
        for (size_t at; (at = name.find("::")) != std::string::npos;) name.replace(at, 2, ".");
        return "'" + name + "'";
    }
    std::string type(QualType t) const {
        t = t.getCanonicalType();
        if (t->isVoidType()) return "void";
        if (t->isBooleanType()) return "bool";
        if (t->isSpecificBuiltinType(BuiltinType::Char16) ||
            (t->isWideCharType() && context.getTypeSize(t) == 16)) return "char";
        if (t->isIntegerType()) {
            unsigned bits = context.getTypeSize(t);
            if (bits != 8 && bits != 16 && bits != 32 && bits != 64) throw std::runtime_error("unsupported integer width");
            return std::string(t->isUnsignedIntegerType() ? "uint" : "int") + std::to_string(bits);
        }
        if (t->isRealFloatingType()) return context.getTypeSize(t) == 32 ? "float32" : "float64";
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
    void load_decl(const ValueDecl* d, const Expr* at) {
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
            expr(member->getBase()); op("ldloc " + std::to_string(slot), 0, 1);
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
                unsigned receiver = temporary("class " + record_name(cast<CXXRecordDecl>(field->getParent())));
                unsigned result = temporary(type(field->getType()));
                expr(member->getBase()); op("stloc " + std::to_string(receiver), 1);
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
            case BO_LT: case BO_GE: operation = unsign ? "clt.un" : "clt"; break;
            case BO_GT: case BO_LE: operation = unsign ? "cgt.un" : "cgt"; break;
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
            expr(member->getImplicitObjectArgument()); op("ldlen", 1, 1); op("conv.i4", 1, 1); return;
        }
        if (f->getNameAsString() == "__cli_literal") {
            if (c->getNumArgs() != 1 || !isa<StringLiteral>(c->getArg(0)->IgnoreParenImpCasts())) fail(c, "invalid string literal marker");
            expr(c->getArg(0)); return;
        }
        if (auto* opcall = dyn_cast<CXXOperatorCallExpr>(c)) {
            if (opcall->getOperator() == OO_Arrow) { expr(opcall->getArg(0)); return; }
            if (opcall->getOperator() == OO_Equal && opcall->getNumArgs() == 2 && handle_target(opcall->getArg(0)->getType())) {
                expr(opcall->getArg(1)); store(opcall->getArg(0), true); return;
            }
        }
        if (f->getNameAsString() == "__cli_safe_cast" || f->getNameAsString() == "__cli_dynamic_cast") {
            auto* target = handle_target(c->getType());
            if (!target || c->getNumArgs() != 1 || !handle_target(c->getArg(0)->getType())) fail(c, "only managed reference casts are enabled");
            expr(c->getArg(0));
            op(std::string(f->getNameAsString() == "__cli_safe_cast" ? "castclass " : "isinst ") + reference_token(c->getType()), 1, 1);
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
            expr(member->getImplicitObjectArgument());
        }
        for (auto* a : c->arguments()) expr(a);
        std::string id = annotation(f, "cli.import:");
        std::string sig;
        if (!id.empty()) {
            auto imported = metadata.methods.find(id);
            if (imported == metadata.methods.end()) fail(c, "imported member is absent from metadata");
            sig = imported_signature(imported->second);
        } else {
            if (!f->hasBody() && !(m && m->isPureVirtual())) fail(c, "calling a function without an emitted definition");
            sig = signature(f);
        }
        bool qualified = false;
        if (auto* member = dyn_cast<CXXMemberCallExpr>(c)) if (auto* callee = dyn_cast<MemberExpr>(member->getCallee()->IgnoreParenImpCasts())) qualified = callee->hasQualifier();
        op(std::string(instance && !qualified ? "callvirt " : "call ") + sig, c->getNumArgs() + unsigned(instance), f->getReturnType()->isVoidType() ? 0 : 1);
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
                std::string target = type(c->getType());
                std::string conversion;
                if (target == "int32") conversion = "conv.i4";
                else if (target == "int64") conversion = "conv.i8";
                else if (target == "uint32") conversion = "conv.u4";
                else if (target == "uint64") conversion = "conv.u8";
                else if (target == "float64") conversion = "conv.r8";
                else if (target == "float32") conversion = "conv.r4";
                else fail(c, "unsupported numeric conversion");
                op(conversion, 1, 1);
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
        if (isa<CXXNullPtrLiteralExpr>(e)) { op("ldnull", 0, 1); return; }
        if (isa<CXXThisExpr>(e)) {
            auto* method = dyn_cast<CXXMethodDecl>(current);
            if (!method || method->isStatic()) fail(e, "this outside a managed instance method");
            op("ldarg.0", 0, 1); return;
        }
        if (auto* member = dyn_cast<MemberExpr>(e)) {
            auto* field = dyn_cast<FieldDecl>(member->getMemberDecl());
            if (!field) fail(e, "unsupported managed member expression");
            expr(member->getBase()); op("ldfld " + field_signature(field), 1, 1); return;
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
                    unsigned receiver = temporary("class " + record_name(cast<CXXRecordDecl>(field->getParent())));
                    unsigned old = temporary(type(field->getType())), result = temporary(type(field->getType()));
                    expr(member->getBase()); op("stloc " + std::to_string(receiver), 1);
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
                op("ldc.i4.1", 0, 1); op(u->isIncrementOp() ? "add" : "sub", 2, 1);
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
                expr(source); op("castclass " + reference_token(c->getType()), 1, 1); return;
            }
            if (c->getCastKind() != CK_NoOp) fail(c, "explicit conversions are not enabled yet");
            expr(c->getSubExpr()); return;
        }
        fail(e, std::string("unsupported AST expression: ") + e->getStmtClassName());
    }
    void collect_locals(const Stmt* s) {
        if (!s) return;
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
            if (r->getRetValue()) expr(r->getRetValue());
            op("ret", current->getReturnType()->isVoidType() ? 0 : 1);
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
            loop_labels.emplace_back(done, check); statement(w->getBody()); loop_labels.pop_back();
            op("br " + check); mark(done);
        } else if (auto* f = dyn_cast<ForStmt>(s)) {
            if (f->getConditionVariable()) fail(s, "for condition variable is not supported yet");
            statement(f->getInit()); auto check = label(), inc = label(), done = label(); mark(check);
            if (f->getCond()) { expr(f->getCond()); op("brfalse " + done, 1); }
            loop_labels.emplace_back(done, inc); statement(f->getBody()); loop_labels.pop_back();
            mark(inc); statement(f->getInc()); op("br " + check); mark(done);
        } else if (isa<BreakStmt>(s) || isa<ContinueStmt>(s)) {
            if (loop_labels.empty()) fail(s, "loop control outside a loop");
            op("br " + (isa<BreakStmt>(s) ? loop_labels.back().first : loop_labels.back().second));
        } else if (auto* e = dyn_cast<Expr>(s)) {
            expr(e); if (!e->getType()->isVoidType()) op("pop", 1);
        } else fail(s, std::string("unsupported AST statement: ") + s->getStmtClassName());
        if (stack != before) fail(s, "internal IL stack mismatch at statement boundary");
    }
    FunctionIR function(FunctionDecl* f) {
        current = f; locals.clear(); parameters.clear(); temporary_types.clear(); opaque_values.clear(); code.str(""); code.clear(); stack = 0; max_stack = 0;
        label_count = 0; loop_labels.clear();
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
            if (!base_initialized) { op("ldarg.0", 0, 1); op("call instance void [System.Runtime]System.Object::.ctor()", 1); }
            for (auto* init : constructor->inits()) if (init->isMemberInitializer()) {
                op("ldarg.0", 0, 1); expr(init->getInit()); op("stfld " + field_signature(init->getMember()), 2);
            }
        }
        statement(f->getBody());
        if (f->getReturnType()->isVoidType()) op("ret");
        else {
            std::string result = type(f->getReturnType());
            op(result == "int64" || result == "uint64" ? "ldc.i8 0" : result == "float32" ? "ldc.r4 0" : result == "float64" ? "ldc.r8 0" :
               handle_target(f->getReturnType()) ? "ldnull" : "ldc.i4.0", 0, 1);
            op("ret", 1);
        } // Unreachable for correctly-returning non-void functions except main.
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
        if (isa<CXXConstructorDecl>(f) && method && is_interface(method->getParent())) return true;
        if (f->isImplicit() && method && annotated(method->getParent(), "cli.abstract") && annotated(method->getParent(), "cli.sealed")) return true;
        if ((f->isImplicit() && !(isa<CXXConstructorDecl>(f) && cast<CXXConstructorDecl>(f)->isDefaultConstructor())) || (!f->doesThisDeclarationHaveABody() && !(method && method->isPureVirtual())) || llvm::StringRef(f->getNameAsString()).starts_with("__cli_")) return true;
        if (auto* m = dyn_cast<CXXMethodDecl>(f)) if (!annotation(m->getParent(), "cli.external:").empty()) return true;
        validate_declarator(f->getTypeSourceInfo());
        for (auto* p : f->parameters()) validate_declarator(p->getTypeSourceInfo());
        if (f->getType()->isDependentType() || f->getTemplatedKind() == FunctionDecl::TK_FunctionTemplate)
            throw std::runtime_error("unspecialized dependent function reached IL emission");
        if (seen.insert(f->getCanonicalDecl()).second) functions.push_back(f);
        return true;
    }
    bool VisitCXXRecordDecl(CXXRecordDecl* r) {
        if (r->isThisDeclarationADefinition() && annotated(r, "cli.ref") && !annotated(r, "cli.array")) {
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
            else if (auto* tmpl = dyn_cast<FunctionTemplateDecl>(decl)) {
                if (llvm::StringRef(tmpl->getNameAsString()).starts_with("__cli_")) continue;
                for (auto* specialization : tmpl->specializations()) if (specialization->isUsed()) VisitFunctionDecl(specialization);
            }
            else if (auto* alias = dyn_cast<TypedefNameDecl>(decl)) validate_declarator(alias->getTypeSourceInfo());
        }
    }
    std::string emit(const Options& options) {
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
        auto emit_function = [&](size_t i) {
            auto* f = functions[i]; auto& body = ir[i];
            auto* method = dyn_cast<CXXMethodDecl>(f);
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
        for (auto* r : records) {
            auto* parent = class_base(r);
            il << ".class public " << (is_interface(r) ? "interface abstract " : "auto ansi ")
               << (!is_interface(r) && (annotated(r, "cli.abstract") || r->isAbstract()) ? "abstract " : "")
               << (annotated(r, "cli.sealed") ? "sealed " : "") << record_name(r);
            if (!is_interface(r)) il << " extends " << (parent ? record_name(parent) : "[System.Runtime]System.Object");
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
