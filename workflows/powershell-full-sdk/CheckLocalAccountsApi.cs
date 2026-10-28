// Compare source symbols without emitting or executing an unpatched assembly.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;

class Program
{
    static readonly SymbolDisplayFormat Format = new SymbolDisplayFormat(
        typeQualificationStyle: SymbolDisplayTypeQualificationStyle.NameAndContainingTypesAndNamespaces,
        genericsOptions: SymbolDisplayGenericsOptions.IncludeTypeParameters | SymbolDisplayGenericsOptions.IncludeTypeConstraints,
        memberOptions: SymbolDisplayMemberOptions.IncludeAccessibility | SymbolDisplayMemberOptions.IncludeModifiers |
            SymbolDisplayMemberOptions.IncludeContainingType | SymbolDisplayMemberOptions.IncludeType |
            SymbolDisplayMemberOptions.IncludeParameters | SymbolDisplayMemberOptions.IncludeConstantValue,
        parameterOptions: SymbolDisplayParameterOptions.IncludeType | SymbolDisplayParameterOptions.IncludeName |
            SymbolDisplayParameterOptions.IncludeDefaultValue | SymbolDisplayParameterOptions.IncludeParamsRefOut,
        propertyStyle: SymbolDisplayPropertyStyle.ShowReadWriteDescriptor,
        miscellaneousOptions: SymbolDisplayMiscellaneousOptions.UseSpecialTypes |
            SymbolDisplayMiscellaneousOptions.EscapeKeywordIdentifiers);

    static bool Visible(ISymbol symbol) => symbol.DeclaredAccessibility is
        Accessibility.Public or Accessibility.Protected or Accessibility.ProtectedOrInternal;

    static (SortedSet<string> Api, SortedSet<string> Attributes, int Obsolete) Inspect(IAssemblySymbol assembly)
    {
        var api = new SortedSet<string>();
        var attributes = new SortedSet<string>();
        int obsolete = 0;
        void Symbol(ISymbol symbol)
        {
            string signature = symbol.Kind + " " + symbol.ToDisplayString(Format);
            api.Add(signature);
            foreach (var attribute in symbol.GetAttributes())
            {
                bool adaptation = attribute.AttributeClass?.ToDisplayString() == "System.ObsoleteAttribute" &&
                    attribute.NamedArguments.Any(a => a.Key == "DiagnosticId" && (string)a.Value.Value == "SYSLIB0051");
                if (adaptation)
                {
                    if (symbol is not IMethodSymbol { MethodKind: MethodKind.Constructor, DeclaredAccessibility: Accessibility.Protected } method ||
                        method.Parameters.Length != 2 || method.Parameters[0].Type.ToDisplayString() != "System.Runtime.Serialization.SerializationInfo" ||
                        method.Parameters[1].Type.ToDisplayString() != "System.Runtime.Serialization.StreamingContext")
                        throw new Exception("Unexpected obsolete adaptation: " + signature);
                    obsolete++;
                }
                else attributes.Add(signature + " ATTRIBUTE " + attribute);
            }
        }
        void Type(INamedTypeSymbol type)
        {
            if (!Visible(type)) return;
            Symbol(type);
            api.Add(type + " BASE " + type.BaseType + " ABSTRACT " + type.IsAbstract + " SEALED " + type.IsSealed);
            foreach (var parent in type.Interfaces) api.Add(type + " INTERFACE " + parent);
            foreach (var member in type.GetMembers())
            {
                if (member is INamedTypeSymbol nested) { Type(nested); continue; }
                if (type.TypeKind == TypeKind.Enum && member.Name == "value__") continue;
                if (Visible(member)) Symbol(member);
            }
        }
        void Namespace(INamespaceSymbol space)
        {
            foreach (var type in space.GetTypeMembers()) Type(type);
            foreach (var child in space.GetNamespaceMembers()) Namespace(child);
        }
        Namespace(assembly.GlobalNamespace);
        return (api, attributes, obsolete);
    }

    static void Main(string[] args)
    {
        using var input = JsonDocument.Parse(File.ReadAllText(args[0]));
        var data = input.RootElement;
        var references = data.GetProperty("references").EnumerateArray().Select(p => MetadataReference.CreateFromFile(p.GetString())).ToArray();
        var options = new CSharpParseOptions(LanguageVersion.CSharp12,
            preprocessorSymbols: data.GetProperty("symbols").EnumerateArray().Select(p => p.GetString()));
        CSharpCompilation Compilation(string key) => CSharpCompilation.Create("Microsoft.PowerShell.LocalAccounts",
            data.GetProperty("sources").EnumerateArray().Select(p => CSharpSyntaxTree.ParseText(p.GetProperty(key).GetString(), options, p.GetProperty("path").GetString())),
            references, new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, allowUnsafe: true));
        var originalCompilation = Compilation("original");
        var adaptedCompilation = Compilation("adapted");
        foreach (var compilation in new[] { originalCompilation, adaptedCompilation })
        {
            var errors = compilation.GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error).ToArray();
            if (errors.Length != 0) throw new Exception(string.Join("\n", errors.Select(d => d.ToString())));
        }
        var original = Inspect(originalCompilation.Assembly);
        var adapted = Inspect(adaptedCompilation.Assembly);
        if (!original.Api.SetEquals(adapted.Api) || !original.Attributes.SetEquals(adapted.Attributes))
            throw new Exception("LocalAccounts public/protected declarations or original attributes changed");
        if (original.Obsolete != 0 || adapted.Obsolete != 17) throw new Exception("Expected exactly 17 legacy constructor annotations");
        var target = MetadataReference.CreateFromFile(args[2]);
        var metadata = CSharpCompilation.Create("MetadataInspection", references: references.Append(target),
            options: new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, metadataImportOptions: MetadataImportOptions.All));
        var actualAssembly = (IAssemblySymbol)metadata.GetAssemblyOrModuleSymbol(target);
        var actual = Inspect(actualAssembly);
        if (!adapted.Api.SetEquals(actual.Api))
            throw new Exception("Emitted API mismatch:\n" + string.Join("\n", adapted.Api.Except(actual.Api).Select(p => "MISSING " + p).Concat(actual.Api.Except(adapted.Api).Select(p => "EXTRA " + p))));
        if (actual.Obsolete != 17 || actualAssembly.Identity.Name != "Microsoft.PowerShell.LocalAccounts" ||
            actualAssembly.Identity.Version != adaptedCompilation.Assembly.Identity.Version ||
            !actualAssembly.Identity.PublicKey.SequenceEqual(Convert.FromBase64String(data.GetProperty("original_public_key").GetString())))
            throw new Exception("Emitted identity, original public key or legacy annotations differ");
        File.WriteAllText(args[1], JsonSerializer.Serialize(new {
            public_protected_entries = original.Api.Count, api = original.Api,
            original_attributes = original.Attributes, matching_original_declarations_and_attributes = true,
            added_legacy_serialization_annotations = adapted.Obsolete,
            emitted_metadata_matches_original_api = true, emitted_identity = actualAssembly.Identity.ToString(),
            emitted_original_public_key_matches = true,
            unpatched_binary_emitted_or_executed = false
        }, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine($"PASS all {original.Api.Count} original LocalAccounts public/protected entries and attributes; actual emitted declarations/identity/public key;17 serialization annotations; no unpatched assembly emitted");
    }
}
