// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Read metadata without loading/executing the referenced assemblies.
using System.Collections.Immutable;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

record CliType(string Il, string Cpp, string Kind, string Name = "") {
    public string[] Modifiers { get; init; } = Array.Empty<string>();
    public CliType? Element { get; init; }
    public CliType[] Arguments { get; init; } = Array.Empty<CliType>();
}
record CliMethod(string Id, string Name, bool Static, CliType Return, CliType[] Parameters,
                 bool Supported, int GenericArity, bool Virtual, bool Abstract, bool Final, string Access) {
    public bool ParamArray { get; init; }
}
record CliConstant(string Il, string Cpp);
record CliField(string Name, CliType Type, bool Supported, string Access, bool Static, bool ReadOnly, string Id) {
    public CliConstant? Constant { get; init; }
    public bool Literal { get; init; }
}
record CliEnumMember(string Name, string Value);
record CliProperty(string Name, CliType Type, string Getter, string Setter, bool Static, bool Supported) {
    public CliType[] Parameters { get; init; } = Array.Empty<CliType>();
}
record CliClass(string Assembly, string Name, string Cpp, string Kind, string Base, CliMethod[] Methods, CliProperty[] Properties, string[] Interfaces, CliType? Underlying = null, CliEnumMember[]? EnumMembers = null, CliField[]? Fields = null) {
    public int GenericArity { get; init; }
    public CliType? BaseType { get; init; }
    public CliType[] InterfaceTypes { get; init; } = Array.Empty<CliType>();
    public string DefaultMember { get; init; } = "";
    public bool NativeStorageSafe { get; init; }
}
record CliAssembly(string Name, string Version, string Token);
record CliAccess(string Assembly, string PublicKey, string[] Friends);

sealed class SignatureTypes : ISignatureTypeProvider<CliType, object?>
{
    public static string CppName(string name) => "::" + string.Join("::", name.Split('.').Select(p => p.Split('`')[0]));
    static CliType Unsupported(string name) => new("", "", "unsupported", name);
    public static CliType Named(string name, bool value)
    {
        if (name == "System.String") return new("string", "::System::String*", "ref", name);
        if (name == "System.Object") return new("object", "::System::Object*", "ref", name);
        if (name.Contains('+')) return Unsupported(name);
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
        PrimitiveTypeCode.IntPtr => new("native int", "::System::IntPtr", "value", "System.IntPtr"),
        PrimitiveTypeCode.UIntPtr => new("native unsigned int", "::System::UIntPtr", "value", "System.UIntPtr"),
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
    public static CliType EntityType(MetadataReader r, EntityHandle h) => h.Kind switch {
        HandleKind.TypeDefinition => new SignatureTypes().GetTypeFromDefinition(r, (TypeDefinitionHandle)h, 0x12),
        HandleKind.TypeReference => new SignatureTypes().GetTypeFromReference(r, (TypeReferenceHandle)h, 0x12),
        HandleKind.TypeSpecification => new SignatureTypes().GetTypeFromSpecification(r, null, (TypeSpecificationHandle)h, 0x12),
        _ => new("", "", "unsupported")
    };
    public CliType GetSZArrayType(CliType t) => t.Kind == "unsupported" ? new(t.Il + "[]", "", "unsupported", t.Name + "[]") :
        new(t.Il + "[]", "::cli::array<" + t.Cpp + ">*", "array", t.Name) { Element = t };
    public CliType GetArrayType(CliType t, ArrayShape s) => new(t.Il + "[... ]", "", "unsupported", t.Name);
    public CliType GetByReferenceType(CliType t) => t.Kind == "unsupported" ? t : new(t.Il + "&", t.Cpp + "&", "byref", t.Name) { Element = t };
    public CliType GetPointerType(CliType t) => t.Kind == "primitive" ? new(t.Il + "*", t.Cpp + "*", "pointer") : new(t.Il + "*", "", "unsupported", t.Name + "*");
    public CliType GetGenericInstantiation(CliType t, ImmutableArray<CliType> a) => t.Kind == "unsupported" || a.Any(p => p.Kind == "unsupported") ?
        new(t.Il, "", "unsupported", t.Name) : new(t.Il + "<" + string.Join(", ", a.Select(p => p.Il)) + ">",
            CppName(t.Name) + "<" + string.Join(", ", a.Select(p => p.Cpp)) + ">" + (t.Kind == "ref" ? "*" : ""), "generic", t.Name) { Element = t, Arguments = a.ToArray() };
    public CliType GetGenericMethodParameter(object? c, int i) => new("!!" + i, "", "unsupported");
    public CliType GetGenericTypeParameter(object? c, int i) => new("!" + i, "__cli_T" + i, "typeparam");
    public CliType GetFunctionPointerType(MethodSignature<CliType> s) => new("", "", "unsupported");
    public CliType GetModifiedType(CliType m, CliType t, bool required) {
        if (required) return new(t.Il, "", "unsupported");
        var modifiers = t.Modifiers.Append(m.Name).ToArray();
        if ((t.Name == "System.ValueType" || t.Kind == "boxed") && modifiers.Contains("System.Runtime.CompilerServices.IsBoxed")) {
            var valueName = modifiers.FirstOrDefault(n => n != "System.Runtime.CompilerServices.IsBoxed");
            if (valueName != null)
                return new("class [System.Runtime]System.ValueType modopt('" + valueName + "') modopt([System.Runtime.CompilerServices.VisualC]System.Runtime.CompilerServices.IsBoxed)",
                    CppName(valueName) + "*", "boxed", valueName) { Modifiers = modifiers };
        }
        return t with { Modifiers = modifiers };
    }
    public CliType GetPinnedType(CliType t) => new(t.Il, "", "unsupported");
}

static class Program
{
    static bool NativeStorageSafe(MetadataReader r, TypeDefinition definition, HashSet<string>? visiting = null)
    {
        visiting ??= new();
        string name = FullName(r, definition);
        if (!visiting.Add(name) || definition.GetGenericParameters().Count != 0) return false;
        bool Safe(CliType t) {
            if (t.Kind == "primitive" || t.Kind == "pointer") return true;
            if (t.Kind != "value") return false;
            if (t.Name is "System.IntPtr" or "System.UIntPtr") return true;
            foreach (var handle in r.TypeDefinitions) {
                var nested = r.GetTypeDefinition(handle);
                if (FullName(r, nested) == t.Name) return NativeStorageSafe(r, nested, visiting);
            }
            return false; // Unresolved external/generic layouts are not proof of GC-free storage.
        }
        bool result = definition.GetFields().All(handle => {
            var field = r.GetFieldDefinition(handle);
            return (field.Attributes & FieldAttributes.Static) != 0 || Safe(field.DecodeSignature(new SignatureTypes(), null));
        });
        visiting.Remove(name);
        return result;
    }
    public static string FullName(MetadataReader r, TypeDefinition d)
    {
        string ns = r.GetString(d.Namespace), name = r.GetString(d.Name);
        if (!d.GetDeclaringType().IsNil) return FullName(r, r.GetTypeDefinition(d.GetDeclaringType())) + "+" + name;
        return (ns.Length == 0 ? "" : ns + ".") + name;
    }
    static string Escape(string s) => s.Replace("\\", "\\\\").Replace("\"", "\\\"");
    static string AssemblyReference(string name) => name.Contains('.') && name.All(c => char.IsAsciiLetterOrDigit(c) || c == '_' || c == '.') ? "[" + name + "]" : "['" + name.Replace("\\", "\\\\").Replace("'", "\\'") + "']";
    static bool ValidName(string s) => s.Length > 0 && (char.IsLetter(s[0]) || s[0] == '_') && s.All(c => char.IsLetterOrDigit(c) || c == '_');
    static readonly Dictionary<string, string> PrimitiveAliases = new() {
        ["SByte"]="signed char", ["Byte"]="unsigned char", ["Int16"]="short", ["UInt16"]="unsigned short",
        ["Int32"]="int", ["UInt32"]="unsigned int", ["Int64"]="long long", ["UInt64"]="unsigned long long",
        ["Boolean"]="bool", ["Char"]="wchar_t", ["Single"]="float", ["Double"]="double"
    };
    static CliConstant? ReadConstant(MetadataReader r, ConstantHandle handle)
    {
        if (handle.IsNil) return null;
        var constant = r.GetConstant(handle); var blob = r.GetBlobReader(constant.Value);
        var culture = System.Globalization.CultureInfo.InvariantCulture;
        string opcode = "ldc.i4 ", value, cpp;
        switch (constant.TypeCode) {
            case ConstantTypeCode.Boolean: value = blob.ReadByte() == 0 ? "0" : "1"; cpp = value; break;
            case ConstantTypeCode.Char: value = blob.ReadUInt16().ToString(culture); cpp = value; break;
            case ConstantTypeCode.SByte: value = blob.ReadSByte().ToString(culture); cpp = value; break;
            case ConstantTypeCode.Byte: value = blob.ReadByte().ToString(culture); cpp = value; break;
            case ConstantTypeCode.Int16: value = blob.ReadInt16().ToString(culture); cpp = value; break;
            case ConstantTypeCode.UInt16: value = blob.ReadUInt16().ToString(culture); cpp = value; break;
            case ConstantTypeCode.Int32: value = blob.ReadInt32().ToString(culture); cpp = value == "-2147483648" ? "(-2147483647 - 1)" : value; break;
            case ConstantTypeCode.UInt32: value = blob.ReadUInt32().ToString(culture); cpp = value + "U"; break;
            case ConstantTypeCode.Int64: value = blob.ReadInt64().ToString(culture); opcode = "ldc.i8 "; cpp = value == "-9223372036854775808" ? "(-9223372036854775807LL - 1)" : value + "LL"; break;
            case ConstantTypeCode.UInt64: value = blob.ReadUInt64().ToString(culture); opcode = "ldc.i8 "; cpp = value + "ULL"; break;
            case ConstantTypeCode.Single: {
                uint bits = blob.ReadUInt32();
                opcode = "ldc.r4 "; value = "float32(0x" + bits.ToString("X8",culture) + ")";
                cpp = "__builtin_bit_cast(float, 0x" + bits.ToString("X8",culture) + "U)";
                break;
            }
            case ConstantTypeCode.Double: {
                ulong bits = blob.ReadUInt64();
                opcode = "ldc.r8 "; value = "float64(0x" + bits.ToString("X16",culture) + ")";
                cpp = "__builtin_bit_cast(double, 0x" + bits.ToString("X16",culture) + "ULL)";
                break;
            }
            default: return null;
        }
        return new(opcode + value, cpp);
    }
    static string AttributeType(MetadataReader r, EntityHandle ctor)
    {
        var owner = ctor.Kind == HandleKind.MemberReference ? r.GetMemberReference((MemberReferenceHandle)ctor).Parent :
            ctor.Kind == HandleKind.MethodDefinition ? r.GetMethodDefinition((MethodDefinitionHandle)ctor).GetDeclaringType() : default;
        if (owner.Kind == HandleKind.TypeDefinition) return FullName(r, r.GetTypeDefinition((TypeDefinitionHandle)owner));
        if (owner.Kind == HandleKind.TypeReference) {
            var type = r.GetTypeReference((TypeReferenceHandle)owner);
            return r.GetString(type.Namespace) + "." + r.GetString(type.Name);
        }
        return "";
    }
    static void ValidateFriend(MetadataReader r, AssemblyDefinition assembly, CliAccess access)
    {
        byte[] outputKey = Convert.FromHexString(access.PublicKey);
        foreach (var handle in assembly.GetCustomAttributes()) {
            var attribute = r.GetCustomAttribute(handle);
            if (AttributeType(r, attribute.Constructor) != "System.Runtime.CompilerServices.InternalsVisibleToAttribute") continue;
            var blob = r.GetBlobReader(attribute.Value);
            if (blob.ReadUInt16() != 1) throw new Exception("invalid InternalsVisibleTo attribute");
            var identity = new AssemblyName(blob.ReadSerializedString() ?? "");
            if (!string.Equals(identity.Name, access.Assembly, StringComparison.OrdinalIgnoreCase)) continue;
            byte[] friendKey = identity.GetPublicKey() ?? Array.Empty<byte>();
            if (!friendKey.AsSpan().SequenceEqual(outputKey)) continue;
            if (r.GetBlobBytes(assembly.PublicKey).Length != 0 && friendKey.Length == 0) continue;
            return;
        }
        throw new Exception("#using as_friend requires InternalsVisibleTo matching output assembly name and public key: " + access.Assembly);
    }
    static string MemberAccess(MethodAttributes attributes, bool friend) => (attributes & MethodAttributes.MemberAccessMask) switch {
        MethodAttributes.Public => "public",
        MethodAttributes.Family => "protected",
        MethodAttributes.Assembly => friend ? "public" : "",
        MethodAttributes.FamORAssem => friend ? "public" : "protected",
        MethodAttributes.FamANDAssem => friend ? "protected" : "",
        _ => ""
    };
    public static int Main(string[] args)
    {
        try
        {
            if (args.Length < 4) throw new Exception("usage: metadata output.json output.hpp type-list assembly...");
            string jsonPath = args[0], headerPath = args[1];
            var wanted = args[2].Split(';', StringSplitOptions.RemoveEmptyEntries).ToHashSet(StringComparer.Ordinal);
            var types = new List<CliClass>();
            var assemblies = new List<CliAssembly>();
            int firstAssembly = 3;
            var access = new CliAccess("", "", Array.Empty<string>());
            if (args.Length > 4 && args[3] == "--access") {
                access = JsonSerializer.Deserialize<CliAccess>(File.ReadAllText(args[4])) ?? throw new Exception("invalid access context");
                firstAssembly = 5;
            }
            var friendPaths = access.Friends.Select(Path.GetFullPath).ToHashSet(OperatingSystem.IsWindows() ? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal);
            foreach (var path in args.Skip(firstAssembly))
            {
                using var file = File.OpenRead(path);
                using var pe = new PEReader(file);
                var r = pe.GetMetadataReader();
                var a = r.GetAssemblyDefinition();
                string assembly = r.GetString(a.Name);
                bool friend = friendPaths.Contains(Path.GetFullPath(path));
                if (friend) ValidateFriend(r, a, access);
                byte[] key = r.GetBlobBytes(a.PublicKey);
                string token = key.Length == 0 ? "" : Convert.ToHexString(SHA1.HashData(key).TakeLast(8).Reverse().ToArray()).ToLowerInvariant();
                assemblies.Add(new(assembly, a.Version.ToString(), token));
                foreach (var th in r.TypeDefinitions)
                {
                    var td = r.GetTypeDefinition(th);
                    string name = FullName(r, td);
                    if (!wanted.Contains(name)) continue;
                    if ((td.Attributes & TypeAttributes.VisibilityMask) != TypeAttributes.Public && !friend)
                        throw new Exception("imported type is internal; use #using as_friend with a matching InternalsVisibleTo: " + name);
                    if (name.Contains('+')) throw new Exception("unsupported imported nested type: " + name);
                    int genericArity = td.GetGenericParameters().Count;
                    string defaultMember = "";
                    foreach (var ah in td.GetCustomAttributes()) {
                        var attribute = r.GetCustomAttribute(ah);
                        if (AttributeType(r, attribute.Constructor) != "System.Reflection.DefaultMemberAttribute") continue;
                        var blob = r.GetBlobReader(attribute.Value);
                        if (blob.ReadUInt16() != 1) throw new Exception("invalid DefaultMemberAttribute");
                        defaultMember = blob.ReadSerializedString() ?? "";
                    }
                    var methods = new List<CliMethod>();
                    foreach (var mh in td.GetMethods())
                    {
                        var md = r.GetMethodDefinition(mh);
                        string memberAccess = MemberAccess(md.Attributes, friend);
                        if (memberAccess.Length == 0) continue;
                        string mn = r.GetString(md.Name);
                        var sig = md.DecodeSignature(new SignatureTypes(), null);
                        string id = assembly + ":" + System.Reflection.Metadata.Ecma335.MetadataTokens.GetToken(mh).ToString("x8");
                        bool supported = sig.GenericParameterCount == 0 && sig.Header.CallingConvention == SignatureCallingConvention.Default &&
                            !((md.Attributes & MethodAttributes.Static) != 0 && (md.Attributes & MethodAttributes.Virtual) != 0) &&
                            sig.ReturnType.Kind != "unsupported" && sig.ParameterTypes.All(p => p.Kind != "unsupported") &&
                            (mn == ".ctor" || ValidName(mn));
                        bool paramArray = false;
                        foreach (var ph in md.GetParameters()) {
                            var parameter = r.GetParameter(ph);
                            if (!parameter.GetCustomAttributes().Any(ah => AttributeType(r, r.GetCustomAttribute(ah).Constructor) == "System.ParamArrayAttribute")) continue;
                            if (parameter.SequenceNumber != sig.ParameterTypes.Length || parameter.SequenceNumber == 0 || sig.ParameterTypes[^1].Kind != "array")
                                throw new Exception("ParamArrayAttribute requires the final rank-one array parameter: " + name + "::" + mn);
                            paramArray = true;
                        }
                        methods.Add(new(id, mn, (md.Attributes & MethodAttributes.Static) != 0,
                            sig.ReturnType, sig.ParameterTypes.ToArray(), supported, sig.GenericParameterCount,
                            (md.Attributes & MethodAttributes.Virtual) != 0, (md.Attributes & MethodAttributes.Abstract) != 0,
                            (md.Attributes & MethodAttributes.Final) != 0, memberAccess) { ParamArray = paramArray });
                    }
                    var properties = new List<CliProperty>();
                    foreach (var ph in td.GetProperties())
                    {
                        var pd = r.GetPropertyDefinition(ph);
                        var sig = pd.DecodeSignature(new SignatureTypes(), null);
                        var accessors = pd.GetAccessors();
                        CliMethod? Accessor(MethodDefinitionHandle h) => h.IsNil ? null : methods.FirstOrDefault(m =>
                            m.Id == assembly + ":" + System.Reflection.Metadata.Ecma335.MetadataTokens.GetToken(h).ToString("x8"));
                        var getter = Accessor(accessors.Getter); var setter = Accessor(accessors.Setter);
                        string pn = r.GetString(pd.Name);
                        properties.Add(new(pn, sig.ReturnType, getter?.Name ?? "", setter?.Name ?? "",
                            (getter ?? setter)?.Static ?? false, ValidName(pn) && sig.ParameterTypes.Length <= 1 &&
                            (getter?.Supported == true || setter?.Supported == true)) { Parameters = sig.ParameterTypes.ToArray() });
                    }
                    var baseType = td.BaseType.IsNil ? null : SignatureTypes.EntityType(r, td.BaseType);
                    string baseName = baseType?.Name ?? "";
                    var fields = td.GetFields().Select(fh => (Handle: fh, Field: r.GetFieldDefinition(fh)))
                        .Where(f => MemberAccess((MethodAttributes)(int)(f.Field.Attributes & FieldAttributes.FieldAccessMask), friend).Length != 0)
                        .Select(f => new CliField(r.GetString(f.Field.Name), f.Field.DecodeSignature(new SignatureTypes(), null), ValidName(r.GetString(f.Field.Name)), MemberAccess((MethodAttributes)(int)(f.Field.Attributes & FieldAttributes.FieldAccessMask), friend), (f.Field.Attributes & FieldAttributes.Static) != 0, (f.Field.Attributes & FieldAttributes.InitOnly) != 0,
                            assembly + ":" + System.Reflection.Metadata.Ecma335.MetadataTokens.GetToken(f.Handle).ToString("x8")) {
                                Constant = (f.Field.Attributes & FieldAttributes.Literal) != 0 ? ReadConstant(r, f.Field.GetDefaultValue()) : null,
                                Literal = (f.Field.Attributes & FieldAttributes.Literal) != 0
                            }).Where(f => !f.Literal || f.Constant != null).ToArray();
                    if (baseName == "System.Enum") {
                        CliType? underlying = null;
                        var members = new List<CliEnumMember>();
                        foreach (var fh in td.GetFields()) {
                            var field = r.GetFieldDefinition(fh);
                            string fn = r.GetString(field.Name);
                            if (fn == "value__") { underlying = field.DecodeSignature(new SignatureTypes(), null); continue; }
                            var ch = field.GetDefaultValue(); if (ch.IsNil) continue;
                            var constant = r.GetConstant(ch); var blob = r.GetBlobReader(constant.Value);
                            string value = constant.TypeCode switch {
                                ConstantTypeCode.SByte => blob.ReadSByte().ToString(System.Globalization.CultureInfo.InvariantCulture),
                                ConstantTypeCode.Byte => blob.ReadByte().ToString(System.Globalization.CultureInfo.InvariantCulture),
                                ConstantTypeCode.Int16 => blob.ReadInt16().ToString(System.Globalization.CultureInfo.InvariantCulture),
                                ConstantTypeCode.UInt16 => blob.ReadUInt16().ToString(System.Globalization.CultureInfo.InvariantCulture),
                                ConstantTypeCode.Int32 => blob.ReadInt32().ToString(System.Globalization.CultureInfo.InvariantCulture),
                                ConstantTypeCode.UInt32 => blob.ReadUInt32().ToString(System.Globalization.CultureInfo.InvariantCulture),
                                ConstantTypeCode.Int64 => blob.ReadInt64().ToString(System.Globalization.CultureInfo.InvariantCulture),
                                ConstantTypeCode.UInt64 => blob.ReadUInt64().ToString(System.Globalization.CultureInfo.InvariantCulture),
                                _ => throw new Exception("unsupported enum constant: " + name + "." + fn)
                            };
                            if (!ValidName(fn)) throw new Exception("unsupported enum member name: " + fn);
                            members.Add(new(fn, value));
                        }
                        if (underlying == null) throw new Exception("missing enum underlying type: " + name);
                        types.Add(new(assembly, name, SignatureTypes.CppName(name), "enum", baseName,
                            methods.ToArray(), properties.ToArray(), Array.Empty<string>(), underlying, members.ToArray()));
                        continue;
                    }
                    var interfaceTypes = td.GetInterfaceImplementations().Select(ih => SignatureTypes.EntityType(r, r.GetInterfaceImplementation(ih).Interface)).ToArray();
                    types.Add(new CliClass(assembly, name, SignatureTypes.CppName(name), baseName == "System.ValueType" ? "value" : (td.Attributes & TypeAttributes.Interface) != 0 ? "interface" : "ref", baseName, methods.ToArray(), properties.ToArray(),
                        interfaceTypes.Where(t => t.Kind != "unsupported").Select(t => t.Name).ToArray(), Fields: fields) { GenericArity = genericArity, BaseType = baseType, InterfaceTypes = interfaceTypes, DefaultMember = defaultMember,
                            NativeStorageSafe = baseName == "System.ValueType" && NativeStorageSafe(r, td) });
                }
            }
            var found = types.Select(t => t.Name).ToHashSet(StringComparer.Ordinal);
            if (!wanted.IsSubsetOf(found)) throw new Exception("reference metadata lacks: " + string.Join(", ", wanted.Except(found)));
            // Cross-type signatures may refer to types we haven't imported yet.
            bool Available(CliType t) => t.Kind == "primitive" || t.Kind == "pointer" || t.Kind == "typeparam" ||
                ((t.Kind == "ref" || t.Kind == "value" || t.Kind == "boxed") && found.Contains(t.Name)) ||
                (t.Kind == "generic" && t.Element != null && Available(t.Element) && t.Arguments.All(Available)) ||
                ((t.Kind == "array" || t.Kind == "byref") && t.Element != null && Available(t.Element));
            CliType Qualified(CliType t)
            {
                if (t.Element != null) {
                    var element = Qualified(t.Element); var arguments = t.Arguments.Select(Qualified).ToArray();
                    return t with { Element = element, Arguments = arguments,
                        Il = t.Kind == "generic" ? element.Il + "<" + string.Join(", ", arguments.Select(a => a.Il)) + ">" :
                            t.Kind == "array" ? element.Il + "[]" : t.Kind == "byref" ? element.Il + "&" : t.Il };
                }
                var owner = types.FirstOrDefault(c => c.Name == t.Name);
                return owner == null ? t : t with { Il = t.Il.Replace("'" + t.Name + "'", AssemblyReference(owner.Assembly) + "'" + t.Name + "'") };
            }
            types = types.Select(t => t with { BaseType = t.BaseType == null ? null : Qualified(t.BaseType), InterfaceTypes = t.InterfaceTypes.Select(Qualified).ToArray(), Fields = t.Fields?.Select(f => f with { Type = Qualified(f.Type), Supported = f.Supported && Available(f.Type) }).ToArray(), Methods = t.Methods.Select(m => m with {
                Supported = m.Supported && Available(m.Return) && m.Parameters.All(Available),
                Return = Qualified(m.Return), Parameters = m.Parameters.Select(Qualified).ToArray()
            }).ToArray(), Properties = t.Properties.Select(p => p with {
                Type = Qualified(p.Type), Parameters = p.Parameters.Select(Qualified).ToArray(), Supported = p.Supported && Available(p.Type) && p.Parameters.All(Available) &&
                    (p.Getter.Length == 0 || t.Methods.Any(m => m.Name == p.Getter && m.Supported && Available(m.Return) && m.Parameters.All(Available))) &&
                    (p.Setter.Length == 0 || t.Methods.Any(m => m.Name == p.Setter && m.Supported && Available(m.Return) && m.Parameters.All(Available)))
            }).ToArray() }).OrderBy(t => t.Name, StringComparer.Ordinal).ToList();
            using var header = new StreamWriter(headerPath, false, new UTF8Encoding(false));
            header.WriteLine("// Metadata declarations only; never compile these surrogates as native objects.");
            header.WriteLine("namespace System { class String; }");
            header.WriteLine("namespace cli { template<class T, int Rank = 1> class array; class __attribute__((annotate(\"cli.finally\"))) __finally_marker {}; }");
            header.WriteLine("[[noreturn]] void __cli_end_finally();");
            header.WriteLine("template<class T> T* __cli_alloc(T*);");
            header.WriteLine("template<class T> cli::array<T>* __cli_empty_params();");
            header.WriteLine("template<class T, class A> T __cli_safe_cast(A); template<class T, class A> T __cli_dynamic_cast(A);");
            header.WriteLine("template<class T> class __attribute__((annotate(\"cli.static.property.proxy\"))) __cli_static_property { public: operator T() const; T operator->() const; __cli_static_property& operator=(T); };");
            header.WriteLine("System::String* __cli_literal(const char*); System::String* __cli_literal(const wchar_t*);");
            foreach (var alias in PrimitiveAliases)
                header.WriteLine("namespace System { using " + alias.Key + " __attribute__((annotate(\"cli.primitive.facade:__cli_primitive_" + alias.Key + "\"))) = " + alias.Value + "; }");
            foreach (var t in types) WriteClass(header, t, true);
            var emitted = new HashSet<string>();
            void Define(CliClass t)
            {
                if (!emitted.Add(t.Name)) return;
                var parent = types.FirstOrDefault(p => p.Name == t.Base);
                if (parent != null) Define(parent);
                var interfaces = t.InterfaceTypes.Where(i => types.Any(p => p.Name == i.Name)).ToArray();
                foreach (var i in interfaces) Define(types.First(p => p.Name == i.Name));
                WriteClass(header, t, false, parent != null, interfaces);
            }
            foreach (var t in types) Define(t);
            if (types.Any(t => t.Name == "System.Type")) header.WriteLine("template<class T> ::System::Type* __cli_typeid();");
            header.WriteLine("namespace cli { template<class T, int Rank> class __attribute__((annotate(\"cli.ref\"), annotate(\"cli.array\"))) array : public System::Object { public: static_assert(Rank == 1, \"only rank-one managed arrays are supported\"); explicit array(int); T& operator[](int); int get_Length(); __declspec(property(get=get_Length)) int Length; }; } using cli::array;");
            // Unevaluated calls retain Clang's selected member in DecltypeType.
            // The emitter uses that selection for the real CLR member reference.
            header.WriteLine("template<class C> auto __cli_each_get(C c) -> decltype(c->GetEnumerator());");
            header.WriteLine("template<class T, int R> cli::array<T,R>* __cli_each_get(cli::array<T,R>*);");
            header.WriteLine("template<class E> auto __cli_each_next(E e, int&) -> decltype(e->MoveNext());");
            header.WriteLine("template<class T, int R> bool __cli_each_next(cli::array<T,R>*, int&);");
            header.WriteLine("template<class E> auto __cli_each_current(E e, int) -> decltype(e->get_Current());");
            header.WriteLine("template<class T, int R> T __cli_each_current(cli::array<T,R>*, int);");
            header.WriteLine("template<class E> void __cli_each_dispose(E);");

            File.WriteAllText(jsonPath, JsonSerializer.Serialize(new { version = 1, assemblies, types }, new JsonSerializerOptions { WriteIndented = true }), new UTF8Encoding(false));
            return 0;
        }
        catch (Exception e) { Console.Error.WriteLine("cli-metadata: " + e.Message); return 1; }
    }
    static void WriteClass(StreamWriter w, CliClass t, bool forward, bool hasBase = false, CliType[]? interfaces = null)
    {
        string[] parts = t.Name.Split('.');
        foreach (string ns in parts.SkipLast(1)) w.Write("namespace " + ns + " { ");
        string name = parts[^1].Split('`')[0];
        bool primitiveFacade = parts.Length == 2 && parts[0] == "System" && PrimitiveAliases.ContainsKey(name);
        if (primitiveFacade) name = "__cli_primitive_" + name;
        if (t.GenericArity != 0) w.Write("template<" + string.Join(", ", Enumerable.Range(0, t.GenericArity).Select(i => "class __cli_T" + i)) + "> ");
        if (t.Kind == "enum") {
            w.Write("enum class __attribute__((annotate(\"cli.enum\"), annotate(\"cli.external:" + Escape(t.Assembly + "|" + t.Name) + "\"))) " + name + " : " + t.Underlying!.Cpp);
            if (forward) w.Write("; ");
            else w.Write(" { " + string.Join(", ", t.EnumMembers!.Select(m => m.Name + " = " + m.Value +
                (t.Underlying.Il.StartsWith("uint", StringComparison.Ordinal) ? "ULL" : "LL"))) + " }; ");
            foreach (string ns in parts.SkipLast(1)) w.Write("} ");
            w.WriteLine(); return;
        }
        w.Write("class __attribute__((annotate(\"cli.external:" + Escape(t.Assembly + "|" + t.Name) + "\")" +
            (t.Kind == "interface" ? ", annotate(\"cli.interface\")" : t.Kind == "value" ? ", annotate(\"cli.value\")" : "") +
            (t.Name == "System.IntPtr" ? ", annotate(\"cli.nativeint:signed\")" : t.Name == "System.UIntPtr" ? ", annotate(\"cli.nativeint:unsigned\")" : "") +
            (t.NativeStorageSafe ? ", annotate(\"cli.native.storage.safe\")" : "") +
            (t.Base == "System.MulticastDelegate" ? ", annotate(\"cli.delegate\")" : "") + ")) " + name);
        if (forward) w.Write("; ");
        else
        {
            var bases = new List<string>();
            if (hasBase && t.Kind != "value") bases.Add(t.BaseType!.Cpp.TrimEnd('*'));
            // CLI interfaces have one identity even along repeated inheritance
            // paths. Virtual C++ surrogate bases coalesce those paths without
            // claiming any native object representation.
            if (interfaces != null && t.Kind != "value") bases.AddRange(interfaces.Select(i => "virtual " + i.Cpp.TrimEnd('*')));
            if (bases.Count != 0) w.Write(" : " + string.Join(", ", bases.Select(b => "public " + b)));
            w.WriteLine(" { public:");
            if (t.Name == "System.Object") w.WriteLine("virtual ~Object() = default;");
            if (t.Name == "System.String") w.WriteLine("String(const char*); String(const wchar_t*);");
            if (t.Kind == "value" && !t.Methods.Any(m => m.Name == ".ctor" && m.Parameters.Length == 0 && m.Supported))
                w.WriteLine("__attribute__((annotate(\"cli.value.zero\"))) " + name + "() {}");
            if (t.Kind == "value") {
                w.WriteLine("__attribute__((annotate(\"cli.box\"))) operator " + name + "*() const;");
                w.WriteLine("__attribute__((annotate(\"cli.box.object\"))) operator ::System::Object*() const;");
            }
            foreach (var f in t.Fields ?? Array.Empty<CliField>()) if (f.Supported) {
                if (f.Constant != null) w.WriteLine(f.Access + ": __attribute__((annotate(\"cli.import.constant:" + Escape(f.Id) + "\"))) static constexpr " + f.Type.Cpp + " " + f.Name + " = " + f.Constant.Cpp + ";");
                else w.WriteLine(f.Access + ": __attribute__((annotate(\"cli.import.field:" + Escape(f.Id) + "\"))) " + (f.ReadOnly ? "__attribute__((annotate(\"cli.readonly\"))) " : "") + (f.Static ? "static " : "") + f.Type.Cpp + " " + f.Name + ";");
            }
            if (t.Base == "System.MulticastDelegate") {
                var invoke = t.Methods.FirstOrDefault(m => m.Name == "Invoke" && m.Supported && !m.Static);
                if (invoke != null) {
                    // Typed C++ binding facades resolve the source method. The
                    // emitter writes the real CLR (Object, native int) .ctor.
                    string parameters = string.Join(", ", invoke.Parameters.Select(p => p.Cpp));
                    w.WriteLine("using _cli_signature = " + invoke.Return.Cpp + "(" + parameters + "); " + name + "(_cli_signature*);");
                    w.WriteLine("template<class T, class C> " + name + "(T*, " + invoke.Return.Cpp + " (C::*)(" + parameters + "));");
                }
            }
            var overloads = t.Methods.Where(m => m.Supported).GroupBy(m => m.Name + "(" + string.Join(", ", m.Parameters.Select(p => p.Cpp)) + ")").ToDictionary(g => g.Key, g => g.Count());
            var seen = new HashSet<string>();
            foreach (var m in t.Methods.Where(m => m.Supported && (!primitiveFacade || m.Static)))
            {
                // These nongeneric enumeration slots conflict by return type
                // with generic slots. Their CLR virtual/abstract metadata stays
                // intact; declare a callable C++ facade and validate concrete
                // implementations in the IL backend instead of imposing C++
                // virtual return/covariance rules on the CLI interface graph.
                bool cppVirtual = m.Virtual && !((t.Name == "System.Collections.IEnumerator" && m.Name == "get_Current") ||
                    (t.Name == "System.Collections.IEnumerable" && m.Name == "GetEnumerator"));
                string parameters = string.Join(", ", m.Parameters.Select(p => p.Cpp));
                // C++ cannot distinguish overloads that collapse to the same surrogate signature.
                string signature = m.Name + "(" + parameters + ")";
                string cppMethod = m.Name;
                if (overloads[signature] > 1 && m.Name.StartsWith("op_", StringComparison.Ordinal))
                    cppMethod = "_cli_metadata_" + m.Name + "_" + m.Id.Split(':')[1];
                else if (!seen.Add(signature)) throw new Exception("surrogate overload collision: " + t.Name + "::" + signature);
                w.Write(m.Access + ": __attribute__((annotate(\"cli.import:" + Escape(m.Id) + "\"))) ");
                if (m.Name == ".ctor") w.WriteLine(name + "(" + parameters + ");");
                else w.WriteLine((m.Static ? "static " : cppVirtual && t.Kind != "value" ? "virtual " : "") + m.Return.Cpp + " " + cppMethod + "(" + parameters + ")" +
                    (m.Final && cppVirtual && t.Kind != "value" ? " final" : "") + (m.Abstract && cppVirtual && t.Kind != "value" && t.Kind != "interface" ? " = 0" : "") + ";");
                if (m.ParamArray) {
                    var element = m.Parameters[^1].Element ?? throw new Exception("parameter array has no element type: " + t.Name + "::" + m.Name);
                    string omittedParameters = string.Join(", ", m.Parameters.Select((p, i) => p.Cpp + " __cli_p" + i +
                        (i == m.Parameters.Length - 1 ? " = __cli_empty_params<" + element.Cpp + ">()" : "")));
                    // Keep the normal array overload. A template facade adds
                    // omission without tying an equally good ordinary overload:
                    // Clang prefers the ordinary method for equal conversions.
                    // Virtual/abstract flags belong to the real declaration;
                    // this facade binds the same CLR member identity.
                    w.Write(m.Access + ": template<class __cli_params_default = void> __attribute__((annotate(\"cli.import:" + Escape(m.Id) + "\"))) ");
                    if (m.Name == ".ctor") w.WriteLine(name + "(" + omittedParameters + ");");
                    else w.WriteLine((m.Static ? "static " : "") + m.Return.Cpp + " " + cppMethod + "(" + omittedParameters + ");");
                }
            }
            if (t.Kind == "value" && !primitiveFacade) {
                foreach (var m in t.Methods.Where(m => m.Supported && m.Static && m.Return.Il == "bool" &&
                    (m.Name == "op_Equality" || m.Name == "op_Inequality") && m.Parameters.Length == 2 &&
                    m.Parameters.All(p => p.Cpp == t.Cpp))) {
                    // A hidden friend lets Clang select the real CLR equality
                    // slot instead of considering facade boxing conversions.
                    w.WriteLine("__attribute__((annotate(\"cli.import:" + Escape(m.Id) + "\"))) friend bool operator" +
                        (m.Name == "op_Equality" ? "==" : "!=") + "(" + string.Join(", ", m.Parameters.Select(p => p.Cpp)) + ");");
                }
            }
            var propertyFacades = new Dictionary<string, string>();
            foreach (var p in t.Properties.Where(p => p.Supported))
            {
                if (p.Static) {
                    if (p.Parameters.Length != 0) throw new Exception("static indexed properties are unsupported: " + t.Name + "::" + p.Name);
                    // A public proxy must not bypass a protected accessor.
                    // Nonpublic accessors remain available under their real
                    // method names, where Clang checks the calling context.
                    string getter = t.Methods.Any(m => m.Name == p.Getter && m.Access == "public") ? p.Getter : "";
                    string setter = t.Methods.Any(m => m.Name == p.Setter && m.Access == "public") ? p.Setter : "";
                    if (getter.Length == 0 && setter.Length == 0) continue;
                    w.WriteLine("public: static __attribute__((annotate(\"cli.static.property:" + getter + "|" + setter + "\"))) __cli_static_property<" + p.Type.Cpp + "> " + p.Name + ";");
                    continue;
                }
                var accessors = new List<string>();
                if (p.Getter.Length != 0) accessors.Add("get=" + p.Getter);
                if (p.Setter.Length != 0) accessors.Add("put=" + p.Setter);
                string propertyName = p.Parameters.Length != 0 && p.Name == t.DefaultMember ? "__cli_default" : p.Name;
                string facade = "public: __declspec(property(" + string.Join(",", accessors) + ")) " + p.Type.Cpp + " " +
                    propertyName + (p.Parameters.Length != 0 ? "[]" : "") + ";";
                if (propertyFacades.TryGetValue(propertyName, out var existing)) {
                    if (existing != facade) throw new Exception("unsupported overloaded property facade: " + t.Name + "::" + p.Name);
                    continue;
                }
                propertyFacades.Add(propertyName, facade);
                w.WriteLine(facade);
            }
            w.Write("}; ");
        }
        foreach (string ns in parts.SkipLast(1)) w.Write("} ");
        w.WriteLine();
    }
}
