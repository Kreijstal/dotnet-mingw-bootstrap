// Inspect target assemblies as Roslyn metadata only; never load or execute them.
using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Text.Json;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
class Program
{
    static bool Visible(ISymbol symbol)=>symbol.DeclaredAccessibility is Accessibility.Public or Accessibility.Protected or Accessibility.ProtectedOrInternal;
    static readonly SymbolDisplayFormat Format=new SymbolDisplayFormat(
        typeQualificationStyle:SymbolDisplayTypeQualificationStyle.NameAndContainingTypesAndNamespaces,
        genericsOptions:SymbolDisplayGenericsOptions.IncludeTypeParameters|SymbolDisplayGenericsOptions.IncludeTypeConstraints,
        memberOptions:SymbolDisplayMemberOptions.IncludeAccessibility|SymbolDisplayMemberOptions.IncludeModifiers|SymbolDisplayMemberOptions.IncludeContainingType|SymbolDisplayMemberOptions.IncludeType|SymbolDisplayMemberOptions.IncludeParameters|SymbolDisplayMemberOptions.IncludeExplicitInterface|SymbolDisplayMemberOptions.IncludeConstantValue,
        parameterOptions:SymbolDisplayParameterOptions.IncludeType|SymbolDisplayParameterOptions.IncludeName|SymbolDisplayParameterOptions.IncludeDefaultValue|SymbolDisplayParameterOptions.IncludeParamsRefOut,
        propertyStyle:SymbolDisplayPropertyStyle.ShowReadWriteDescriptor,
        miscellaneousOptions:SymbolDisplayMiscellaneousOptions.UseSpecialTypes|SymbolDisplayMiscellaneousOptions.EscapeKeywordIdentifiers|SymbolDisplayMiscellaneousOptions.IncludeNullableReferenceTypeModifier);
    static void Main(string[] args)
    {
        var references=Directory.GetFiles(args[0],"*.dll").Select(p=>MetadataReference.CreateFromFile(p)).ToList();
        var target=MetadataReference.CreateFromFile(args[1]);references.Add(target);
        foreach(var dependency in Directory.GetFiles(Path.GetDirectoryName(Path.GetFullPath(args[1])),"*.dll"))
            if(Path.GetFullPath(dependency)!=Path.GetFullPath(args[1]))references.Add(MetadataReference.CreateFromFile(dependency));
        var compilation=CSharpCompilation.Create("MetadataInspection",references:references);
        var assembly=(IAssemblySymbol)compilation.GetAssemblyOrModuleSymbol(target);
        var entries=new SortedSet<string>();
        void Type(INamedTypeSymbol type)
        {
            if(!Visible(type))return;
            entries.Add("TYPE "+type.ToDisplayString(Format)+" kind="+type.TypeKind+" enum="+type.EnumUnderlyingType?.ToDisplayString(Format)+" abstract="+type.IsAbstract+" sealed="+type.IsSealed+" base="+type.BaseType?.ToDisplayString(Format));
            foreach(var parent in type.Interfaces)entries.Add("INTERFACE "+type.ToDisplayString(Format)+" : "+parent.ToDisplayString(Format));
            foreach(var member in type.GetMembers())
            {
                if(member is INamedTypeSymbol nested){Type(nested);continue;}
                if(!Visible(member)||member.IsImplicitlyDeclared)continue;
                if(member is IMethodSymbol method && method.MethodKind is MethodKind.PropertyGet or MethodKind.PropertySet or MethodKind.EventAdd or MethodKind.EventRemove)continue;
                entries.Add(member.Kind+" "+member.ToDisplayString(Format));
            }
        }
        void Namespace(INamespaceSymbol space)
        {
            foreach(var type in space.GetTypeMembers())Type(type);
            foreach(var child in space.GetNamespaceMembers())Namespace(child);
        }
        Namespace(assembly.GlobalNamespace);
        Console.WriteLine(JsonSerializer.Serialize(new{identity=assembly.Identity.ToString(),api=entries},new JsonSerializerOptions{WriteIndented=true}));
    }
}
