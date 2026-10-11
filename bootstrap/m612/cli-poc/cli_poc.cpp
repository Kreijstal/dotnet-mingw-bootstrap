// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental C++/CLI subset frontend hosted by a GCC plugin.
// It owns this invocation at PLUGIN_START_UNIT, emits textual IL, and exits
// before GCC's C++ parser. It does not modify GCC's parser or use GIMPLE.
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#define EXPORT __declspec(dllexport)
#else
#include <dlfcn.h>
#define EXPORT __attribute__((visibility("default")))
#endif

// Only the public C ABI from GCC's plugin.h is needed. MSYS2's GCC package
// does not ship its plugin development headers. No GCC tree/layout internals
// are accessed; resolve public entry points from cc1plus at runtime.
struct plugin_argument { char* key; char* value; };
struct plugin_name_args {
    char* base_name;
    const char* full_name;
    int argc;
    plugin_argument* argv;
    const char* version;
    const char* help;
};
struct plugin_gcc_version {
    const char* basever;
    const char* datestamp;
    const char* devphase;
    const char* revision;
    const char* configuration_arguments;
};

extern "C" {
EXPORT int plugin_is_GPL_compatible = 1;
}

namespace {
std::string source_path, output_path;

struct Token {
    enum Kind { identifier, number, literal, punctuation, end } kind;
    std::string text;
    unsigned line;
};

class Lexer {
    std::string input;
    size_t pos = 0;
    unsigned line = 1;

    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error("line " + std::to_string(line) + ": " + message);
    }
    char take() {
        char c = input[pos++];
        if (c == '\n') ++line;
        return c;
    }
public:
    explicit Lexer(std::string text) : input(std::move(text)) {}
    Token next() {
        for (;;) {
            while (pos < input.size() && std::isspace(static_cast<unsigned char>(input[pos]))) take();
            if (input.compare(pos, 2, "//") == 0) {
                while (pos < input.size() && take() != '\n') {}
            } else if (input.compare(pos, 2, "/*") == 0) {
                pos += 2;
                while (pos < input.size() && input.compare(pos, 2, "*/") != 0) take();
                if (pos == input.size()) fail("unterminated comment");
                pos += 2;
            } else break;
        }
        if (pos == input.size()) return {Token::end, "<end of file>", line};
        unsigned start_line = line;
        if (input[pos] == '"' || input.compare(pos, 2, "L\"") == 0) {
            if (input[pos] == 'L') ++pos;
            ++pos;
            std::string value;
            while (pos < input.size()) {
                char c = take();
                if (c == '"') return {Token::literal, value, start_line};
                if (c == '\n' || c == '\r') fail("newline in string literal");
                if (c == '\\') {
                    if (pos == input.size()) fail("unterminated string escape");
                    switch (c = take()) {
                        case 'n': c = '\n'; break;
                        case 'r': c = '\r'; break;
                        case 't': c = '\t'; break;
                        case '\\': case '"': break;
                        default: fail("unsupported string escape");
                    }
                }
                value += c;
            }
            fail("unterminated string literal");
        }
        size_t start = pos;
        auto alpha = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; };
        if (alpha(input[pos])) {
            do { ++pos; } while (pos < input.size() &&
                (alpha(input[pos]) || std::isdigit(static_cast<unsigned char>(input[pos]))));
            return {Token::identifier, input.substr(start, pos - start), start_line};
        }
        if (std::isdigit(static_cast<unsigned char>(input[pos]))) {
            do { ++pos; } while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos])));
            return {Token::number, input.substr(start, pos - start), start_line};
        }
        if (input.compare(pos, 2, "::") == 0) {
            pos += 2;
            return {Token::punctuation, "::", start_line};
        }
        return {Token::punctuation, std::string(1, take()), start_line};
    }
};

std::string il_string(const std::string& text) {
    std::string result = "\"";
    for (char c : text) {
        switch (c) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c;
        }
    }
    return result + "\"";
}

class Parser {
    Lexer lexer;
    Token token;
    std::map<std::string, unsigned> locals;
    std::vector<std::string> local_names;
    std::ostringstream body;

    void advance() { token = lexer.next(); }
    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error("line " + std::to_string(token.line) + ": " + message);
    }
    void expect(const std::string& text) {
        if (token.kind == Token::literal || token.text != text)
            fail("expected '" + text + "', got '" + token.text + "'");
        advance();
    }
    std::string identifier() {
        if (token.kind != Token::identifier) fail("expected an identifier");
        std::string value = token.text;
        advance();
        return value;
    }
    std::string literal() {
        if (token.kind != Token::literal) fail("expected a string literal");
        std::string value = token.text;
        advance();
        return value;
    }
    void statement() {
        if (token.text == "String") {
            expect("String"); expect("^");
            std::string name = identifier();
            if (locals.count(name)) fail("duplicate local '" + name + "'");
            expect("="); expect("gcnew"); expect("String"); expect("(");
            std::string value = literal();
            expect(")"); expect(";");
            unsigned index = local_names.size();
            if (index >= 65535) fail("too many locals");
            locals[name] = index;
            local_names.push_back(name);
            // gcnew String(L"...") creates a fresh string, rather than simply
            // reusing the interned ldstr result. Use the char[] constructor.
            body << "    ldstr " << il_string(value) << "\n"
                 << "    callvirt instance char[] [System.Runtime]System.String::ToCharArray()\n"
                 << "    newobj instance void [System.Runtime]System.String::.ctor(char[])\n"
                 << "    stloc " << index << "\n";
        } else if (token.text == "Console") {
            expect("Console"); expect("::"); expect("WriteLine"); expect("(");
            if (token.kind == Token::literal) {
                body << "    ldstr " << il_string(literal()) << "\n";
            } else {
                std::string name = identifier();
                auto local = locals.find(name);
                if (local == locals.end()) fail("unknown string local '" + name + "'");
                body << "    ldloc " << local->second << "\n";
            }
            expect(")"); expect(";");
            body << "    call void [System.Console]System.Console::WriteLine(string)\n";
        } else {
            fail("unsupported statement starting with '" + token.text + "'");
        }
    }
public:
    explicit Parser(std::string input) : lexer(std::move(input)) { advance(); }
    std::string parse() {
        expect("using"); expect("namespace"); expect("System"); expect(";");
        expect("public"); expect("ref"); expect("class");
        std::string type = identifier();
        expect("abstract"); expect("sealed"); expect("{"); expect("public"); expect(":");
        expect("static"); expect("void");
        std::string method = identifier();
        expect("("); expect(")"); expect("{");
        while (token.kind != Token::punctuation || token.text != "}") statement();
        expect("}"); expect("}"); expect(";");
        expect("int"); expect("main"); expect("("); expect(")"); expect("{");
        expect(type); expect("::"); expect(method); expect("("); expect(")"); expect(";");
        expect("return"); expect("0"); expect(";"); expect("}");
        if (token.kind != Token::end) fail("unsupported trailing input");

        std::ostringstream il;
        for (const char* assembly : {"System.Runtime", "System.Console"})
            il << ".assembly extern " << assembly << " {\n"
               << "  .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A)\n"
               << "  .ver 8:0:0:0\n}\n";
        il << ".assembly cli_hello {}\n.module cli_hello.dll\n"
           << ".class public auto ansi abstract sealed '" << type
           << "' extends [System.Runtime]System.Object {\n"
           << "  .method public hidebysig static void '" << method << "'() cil managed {\n"
           << "    .maxstack 1\n";
        if (!local_names.empty()) {
            il << "    .locals init (";
            for (size_t i = 0; i < local_names.size(); ++i) {
                if (i) il << ", ";
                il << "[" << i << "] string '" << local_names[i] << "'";
            }
            il << ")\n";
        }
        il << body.str() << "    ret\n  }\n}\n"
           << ".method public static int32 main() cil managed {\n"
           << "  .entrypoint\n  .maxstack 1\n"
           << "  call void '" << type << "'::'" << method << "'()\n"
           << "  ldc.i4.0\n  ret\n}\n";
        return il.str();
    }
};

void compile_cli(void*, void*) {
    try {
        std::ifstream input(source_path, std::ios::binary);
        if (!input) throw std::runtime_error("cannot open input '" + source_path + "'");
        std::string text((std::istreambuf_iterator<char>(input)), {});
        // Parse the entire input before creating/truncating any output.
        std::string il = Parser(std::move(text)).parse();
        std::ofstream output(output_path, std::ios::binary);
        output << il;
        output.close();
        if (!output) throw std::runtime_error("cannot write output '" + output_path + "'");
        std::fprintf(stderr, "cli_poc: emitted IL to %s (PLUGIN_START_UNIT)\n", output_path.c_str());
        // This frontend takes ownership of a dedicated -fsyntax-only process.
        // Returning would allow GCC's stock parser to reject the CLI input.
        std::exit(EXIT_SUCCESS);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "cli_poc: %s: %s\n", source_path.c_str(), e.what());
        std::exit(EXIT_FAILURE);
    }
}

template<class Function> Function gcc_symbol(const char* name) {
#ifdef _WIN32
    auto address = GetProcAddress(GetModuleHandleW(nullptr), name);
#else
    auto address = dlsym(RTLD_DEFAULT, name);
#endif
    if (!address) throw std::runtime_error(std::string("GCC does not export ") + name);
    return reinterpret_cast<Function>(address);
}
} // namespace

extern "C" EXPORT int plugin_init(plugin_name_args* args, plugin_gcc_version* version) {
    try {
        if (!version || !version->basever || std::strcmp(version->basever, "16.2.0") != 0)
            throw std::runtime_error("this prototype requires GCC 16.2.0's public plugin ABI");
        for (int i = 0; i < args->argc; ++i) {
            const auto& arg = args->argv[i];
            if (!arg.value) throw std::runtime_error(std::string("missing value for ") + arg.key);
            if (std::strcmp(arg.key, "source") == 0) source_path = arg.value;
            else if (std::strcmp(arg.key, "output") == 0) output_path = arg.value;
            else throw std::runtime_error(std::string("unknown plugin argument ") + arg.key);
        }
        if (source_path.empty() || output_path.empty())
            throw std::runtime_error("both source= and output= plugin arguments are required");
        if (source_path == output_path) throw std::runtime_error("input and output paths must differ");
        using FindEvent = int (*)(const char*, int);
        using RegisterCallback = void (*)(const char*, int, void (*)(void*, void*), void*);
        // GCC's NO_INSERT is zero. Look up the existing event by name, avoiding
        // a dependency on the numeric ordering of plugin.def.
        int event = gcc_symbol<FindEvent>("get_named_event_id")("PLUGIN_START_UNIT", 0);
        if (event < 0) throw std::runtime_error("GCC has no PLUGIN_START_UNIT event");
        gcc_symbol<RegisterCallback>("register_callback")(args->base_name, event, compile_cli, nullptr);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "cli_poc: %s\n", e.what());
        return 1;
    }
}
