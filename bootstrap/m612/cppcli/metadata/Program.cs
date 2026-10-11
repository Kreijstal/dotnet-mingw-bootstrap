// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Read metadata without loading/executing the referenced assemblies.
using System.Collections.Immutable;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

record CliType(string Il, string Cpp, string Kind, string Name = "");
record CliMethod(string Id, string Name, bool Static, CliType Return, CliType[] Parameters,
                 bool Supported, int GenericArity);
record CliClass(string Assembly, string Name, string Cpp, string Kind, string Base, CliMethod[] Methods);
record CliAssembly(string Name, string Version, string Token);

sealed class SignatureTypes : ISignatureTypeProvider<CliType, object?>
{
    public static string CppName(string name) => "::" + name.Replace(".", "::").Replace("+", "::");
    static CliType Unsupported(string name) => new("", "", "unsupported", name);
    public static CliType Named(string name, bool value)
    {
        if (name == "System.String") return new("string", "::System::String*", "ref", name);
        if (name == "System.Object") return new("object", "::System::Object*", "ref", name);
        if (name.Contains('`') || name.Contains('+')) return Unsupported(name);
        return new((value ? "valuetype " : "class ") + "'" + name + "'",
                   value ? CppName(name) : CppName(name) + "*", value ? "value" : "ref", name);
    }
    public CliType GetPrimitiveType(PrimitiveTypeCode type) => type switch
    {
        PrimitiveTypeCode.Void => new("void", "void", "primitive"),
        PrimitiveTypeCode.Boolean => new("bool", "bool", "primitive"),
        PrimitiveTypeCode.Char => new("char", "char16_t", "primitive"),
        PrimitiveTypeCode.SByte => new("int8", "signed char", "primitive"),
        PrimitiveTypeCode.Byte => new("uint8", "unsigned char", "primitive"),
        PrimitiveTypeCode.Int16 => new("int16", "short", "primitive"),
        PrimitiveTypeCode.UInt16 => new("uint16", "unsigned short", "primitive"),
        PrimitiveTypeCode.Int32 => new("int32", "int", "primitive"),
        PrimitiveTypeCode.UInt32 => new("uint32", "unsigned int", "primitive"),
        PrimitiveTypeCode.Int64 => new("int64", "long long", "primitive"),
        PrimitiveTypeCode.UInt64 => new("uint64", "unsigned long long", "primitive"),
        PrimitiveTypeCode.Single => new("float32", "float", "primitive"),
        PrimitiveTypeCode.Double => new("float64", "double", "primitive"),
        PrimitiveTypeCode.IntPtr => new("native int", "__INTPTR_TYPE__", "primitive"),
        PrimitiveTypeCode.UIntPtr => new("native unsigned int", "__UINTPTR_TYPE__", "primitive"),
        PrimitiveTypeCode.String => Named("System.String", false),
        PrimitiveTypeCode.Object => Named("System.Object", false),
        _ => Unsupported(type.ToString())
    };
    public CliType GetTypeFromDefinition(MetadataReader r, TypeDefinitionHandle h, byte k)
    {
        var d = r.GetTypeDefinition(h);
        return Named(Program.FullName(r, d), k == 0x11);
    }
    public CliType GetTypeFromReference(MetadataReader r, TypeReferenceHandle h, byte k)
    {
        var d = r.GetTypeReference(h);
        var ns = r.GetString(d.Namespace);
        return Named((ns.Length == 0 ? "" : ns + ".") + r.GetString(d.Name), k == 0x11);
    }
    public CliType GetTypeFromSpecification(MetadataReader r, object? c, TypeSpecificationHandle h, byte k) =>
        r.GetTypeSpecification(h).DecodeSignature(this, c);
    public CliType GetSZArrayType(CliType t) => new(t.Il + "[]", "", "unsupported", t.Name + "[]");
    public CliType GetArrayType(CliType t, ArrayShape s) => GetSZArrayType(t);
    public CliType GetByReferenceType(CliType t) => new(t.Il + "&", "", "unsupported", t.Name + "&");
    public CliType GetPointerType(CliType t) => new(t.Il + "*", "", "unsupported", t.Name + "*");
    public CliType GetGenericInstantiation(CliType t, ImmutableArray<CliType> a) => new(t.Il, "", "unsupported", t.Name);
    public CliType GetGenericMethodParameter(object? c, int i) => new("!!" + i, "", "unsupported");
    public CliType GetGenericTypeParameter(object? c, int i) => new("!" + i, "", "unsupported");
    public CliType GetFunctionPointerType(MethodSignature<CliType> s) => new("", "", "unsupported");
    public CliType GetModifiedType(CliType m, CliType t, bool required) => required ? new(t.Il, "", "unsupported") : t;
    public CliType GetPinnedType(CliType t) => new(t.Il, "", "unsupported");
}

static class Program
{
    public static string FullName(MetadataReader r, TypeDefinition d)
    {
        string ns = r.GetString(d.Namespace), name = r.GetString(d.Name);
        if (!d.GetDeclaringType().IsNil) return FullName(r, r.GetTypeDefinition(d.GetDeclaringType())) + "+" + name;
        return (ns.Length == 0 ? "" : ns + ".") + name;
    }
    static string Escape(string s) => s.Replace("\\", "\\\\").Replace("\"", "\\\"");
    static bool ValidName(string s) => s.Length > 0 && (char.IsLetter(s[0]) || s[0] == '_') && s.All(c => char.IsLetterOrDigit(c) || c == '_');
    public static int Main(string[] args)
    {
        try
        {
            if (args.Length < 4) throw new Exception("usage: metadata output.json output.hpp type-list assembly...");
            string jsonPath = args[0], headerPath = args[1];
            var wanted = args[2].Split(';', StringSplitOptions.RemoveEmptyEntries).ToHashSet(StringComparer.Ordinal);
            var types = new List<CliClass>();
            var assemblies = new List<CliAssembly>();
            foreach (var path in args.Skip(3))
            {
                using var file = File.OpenRead(path);
                using var pe = new PEReader(file);
                var r = pe.GetMetadataReader();
                var a = r.GetAssemblyDefinition();
                string assembly = r.GetString(a.Name);
                byte[] key = r.GetBlobBytes(a.PublicKey);
                string token = key.Length == 0 ? "" : Convert.ToHexString(SHA1.HashData(key).TakeLast(8).Reverse().ToArray()).ToLowerInvariant();
                assemblies.Add(new(assembly, a.Version.ToString(), token));
                foreach (var th in r.TypeDefinitions)
                {
                    var td = r.GetTypeDefinition(th);
                    string name = FullName(r, td);
                    if (!wanted.Contains(name)) continue;
                    if (name.Contains('`') || name.Contains('+')) throw new Exception("unsupported imported type: " + name);
                    var methods = new List<CliMethod>();
                    foreach (var mh in td.GetMethods())
                    {
                        var md = r.GetMethodDefinition(mh);
                        if ((md.Attributes & MethodAttributes.MemberAccessMask) != MethodAttributes.Public) continue;
                        string mn = r.GetString(md.Name);
                        var sig = md.DecodeSignature(new SignatureTypes(), null);
                        string id = assembly + ":" + System.Reflection.Metadata.Ecma335.MetadataTokens.GetToken(mh).ToString("x8");
                        bool supported = sig.GenericParameterCount == 0 && sig.Header.CallingConvention == SignatureCallingConvention.Default &&
                            sig.ReturnType.Kind != "unsupported" && sig.ParameterTypes.All(p => p.Kind != "unsupported") &&
                            (mn == ".ctor" || ValidName(mn));
                        methods.Add(new(id, mn, (md.Attributes & MethodAttributes.Static) != 0,
                            sig.ReturnType, sig.ParameterTypes.ToArray(), supported, sig.GenericParameterCount));
                    }
                    string baseName = "";
                    if (!td.BaseType.IsNil && td.BaseType.Kind == HandleKind.TypeReference)
                    {
                        var b = r.GetTypeReference((TypeReferenceHandle)td.BaseType);
                        string ns = r.GetString(b.Namespace);
                        baseName = (ns.Length == 0 ? "" : ns + ".") + r.GetString(b.Name);
                    }
                    else if (!td.BaseType.IsNil && td.BaseType.Kind == HandleKind.TypeDefinition)
                        baseName = FullName(r, r.GetTypeDefinition((TypeDefinitionHandle)td.BaseType));
                    types.Add(new(assembly, name, SignatureTypes.CppName(name), "ref", baseName, methods.ToArray()));
                }
            }
            var found = types.Select(t => t.Name).ToHashSet(StringComparer.Ordinal);
            if (!wanted.IsSubsetOf(found)) throw new Exception("reference metadata lacks: " + string.Join(", ", wanted.Except(found)));
            // Cross-type signatures may refer to types we haven't imported yet.
            bool Available(CliType t) => t.Kind == "primitive" || (t.Kind == "ref" && found.Contains(t.Name));
            types = types.Select(t => t with { Methods = t.Methods.Select(m => m with {
                Supported = m.Supported && Available(m.Return) && m.Parameters.All(Available)
            }).ToArray() }).OrderBy(t => t.Name, StringComparer.Ordinal).ToList();
            using var header = new StreamWriter(headerPath, false, new UTF8Encoding(false));
            header.WriteLine("// Metadata declarations only; never compile these surrogates as native objects.");
            header.WriteLine("namespace System { class String; }");
            header.WriteLine("template<class T, class... A> auto __cli_new(A... args) -> decltype(T(args...), (T*)nullptr); ");
            header.WriteLine("template<class T, class A> T __cli_safe_cast(A); template<class T, class A> T __cli_dynamic_cast(A);");
            header.WriteLine("System::String* __cli_literal(const char*); System::String* __cli_literal(const wchar_t*);");
            header.WriteLine("namespace System { using Int32 = int; using Int64 = long long; using Boolean = bool; }");
            foreach (var t in types) WriteClass(header, t, true);
            var emitted = new HashSet<string>();
            void Define(CliClass t)
            {
                if (!emitted.Add(t.Name)) return;
                var parent = types.FirstOrDefault(p => p.Name == t.Base);
                if (parent != null) Define(parent);
                WriteClass(header, t, false, parent != null);
            }
            foreach (var t in types) Define(t);
            File.WriteAllText(jsonPath, JsonSerializer.Serialize(new { version = 1, assemblies, types }, new JsonSerializerOptions { WriteIndented = true }), new UTF8Encoding(false));
            return 0;
        }
        catch (Exception e) { Console.Error.WriteLine("cli-metadata: " + e.Message); return 1; }
    }
    static void WriteClass(StreamWriter w, CliClass t, bool forward, bool hasBase = false)
    {
        string[] parts = t.Name.Split('.');
        foreach (string ns in parts.SkipLast(1)) w.Write("namespace " + ns + " { ");
        string name = parts[^1];
        w.Write("class __attribute__((annotate(\"cli.external:" + Escape(t.Assembly + "|" + t.Name) + "\"))) " + name);
        if (forward) w.Write("; ");
        else
        {
            if (hasBase) w.Write(" : public " + SignatureTypes.CppName(t.Base));
            w.WriteLine(" { public:");
            if (t.Name == "System.Object") w.WriteLine("virtual ~Object() = default;");
            if (t.Name == "System.String") w.WriteLine("String(const char*); String(const wchar_t*);");
            var seen = new HashSet<string>();
            foreach (var m in t.Methods.Where(m => m.Supported))
            {
                string parameters = string.Join(", ", m.Parameters.Select(p => p.Cpp));
                // C++ cannot distinguish overloads that collapse to the same surrogate signature.
                string signature = m.Name + "(" + parameters + ")";
                if (!seen.Add(signature)) throw new Exception("surrogate overload collision: " + t.Name + "::" + signature);
                w.Write("__attribute__((annotate(\"cli.import:" + Escape(m.Id) + "\"))) ");
                if (m.Name == ".ctor") w.WriteLine(name + "(" + parameters + ");");
                else w.WriteLine((m.Static ? "static " : "") + m.Return.Cpp + " " + m.Name + "(" + parameters + ");");
            }
            w.Write("}; ");
        }
        foreach (string ns in parts.SkipLast(1)) w.Write("} ");
        w.WriteLine();
    }
}
