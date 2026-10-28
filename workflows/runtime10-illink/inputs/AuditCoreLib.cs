using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using Mono.Cecil;

internal static class Program
{
    static bool Visible(TypeDefinition type) => type.DeclaringType == null ? type.IsPublic :
        Visible(type.DeclaringType) && (type.IsNestedPublic || type.IsNestedFamily || type.IsNestedFamilyOrAssembly);
    static bool Visible(MethodDefinition method) => method.IsPublic || method.IsFamily || method.IsFamilyOrAssembly;
    static IEnumerable<TypeDefinition> AllTypes(IEnumerable<TypeDefinition> types)
    {
        foreach (var type in types)
        {
            yield return type;
            foreach (var nested in AllTypes(type.NestedTypes)) yield return nested;
        }
    }
    static string GenericParameters(IEnumerable<GenericParameter> parameters) => string.Join(";", parameters.Select(p =>
        p.Position + ":" + p.Attributes + ":" + string.Join(",", p.Constraints.Select(c => c.ConstraintType.FullName).OrderBy(s => s, StringComparer.Ordinal))));
    static SortedSet<string> Api(AssemblyDefinition assembly)
    {
        var result = new SortedSet<string>(StringComparer.Ordinal);
        foreach (var type in AllTypes(assembly.MainModule.Types).Where(Visible))
        {
            result.Add("T " + type.FullName + " " + type.Attributes + " " + type.BaseType?.FullName + " " + GenericParameters(type.GenericParameters));
            foreach (var implementation in type.Interfaces)
                result.Add("I " + type.FullName + " " + implementation.InterfaceType.FullName);
            foreach (var method in type.Methods.Where(Visible))
                result.Add("M " + method.FullName + " " + method.Attributes + " " + GenericParameters(method.GenericParameters) +
                    " " + string.Join(";", method.Parameters.Select(p => p.Attributes + ":" + p.HasConstant + ":" + p.Constant)));
            foreach (var field in type.Fields.Where(f => f.IsPublic || f.IsFamily || f.IsFamilyOrAssembly))
                result.Add("F " + field.FullName + " " + field.Attributes + " " + field.HasConstant + " " + field.Constant);
            foreach (var property in type.Properties.Where(p => p.GetMethod != null && Visible(p.GetMethod) || p.SetMethod != null && Visible(p.SetMethod)))
                result.Add("P " + property.FullName + " " + property.Attributes);
            foreach (var ev in type.Events.Where(e => e.AddMethod != null && Visible(e.AddMethod) || e.RemoveMethod != null && Visible(e.RemoveMethod)))
                result.Add("E " + ev.FullName + " " + ev.Attributes);
        }
        return result;
    }
    public static int Main(string[] args)
    {
        using var raw = AssemblyDefinition.ReadAssembly(args[0]);
        using var linked = AssemblyDefinition.ReadAssembly(args[1], new ReaderParameters { ReadSymbols = true });
        var before = Api(raw);
        var after = Api(linked);
        var removed = before.Except(after).ToArray();
        var added = after.Except(before).ToArray();
        if (raw.Name.FullName != linked.Name.FullName) throw new Exception("Assembly identity changed");
        if (linked.MainModule.AssemblyReferences.Count != 0) throw new Exception("Unexpected assembly references");
        if (removed.Length != 0 || added.Length != 0)
            throw new Exception("Public API changed:\nREMOVED\n" + string.Join("\n", removed) + "\nADDED\n" + string.Join("\n", added));
        var rawTypes = AllTypes(raw.MainModule.Types).ToArray();
        var linkedTypes = AllTypes(linked.MainModule.Types).ToArray();
        var internalCalls = linkedTypes.SelectMany(t => t.Methods).Where(m => m.IsInternalCall).ToArray();
        if (internalCalls.Length == 0 || internalCalls.Any(m => m.RVA != 0 || m.HasBody))
            throw new Exception("Invalid InternalCall body metadata");
        var resources = raw.MainModule.Resources.OfType<EmbeddedResource>().ToArray();
        foreach (var resource in resources)
        {
            var rewritten = linked.MainModule.Resources.OfType<EmbeddedResource>().Single(r => r.Name == resource.Name);
            if (!resource.GetResourceData().SequenceEqual(rewritten.GetResourceData()))
                throw new Exception("Embedded resource changed: " + resource.Name);
        }
        Console.WriteLine(JsonSerializer.Serialize(new {
            identity = linked.Name.FullName, public_api_entries = before.Count, public_api_unchanged = true,
            raw_types = rawTypes.Length, linked_types = linkedTypes.Length,
            raw_methods = rawTypes.Sum(t => t.Methods.Count), linked_methods = linkedTypes.Sum(t => t.Methods.Count),
            internal_calls = internalCalls.Length, internal_calls_with_body = 0,
            original_embedded_resources_unchanged = resources.Select(r => r.Name).ToArray(),
            original_pdb_read_successfully = linked.MainModule.HasSymbols,
            assembly_references = linked.MainModule.AssemblyReferences.Count
        }, new JsonSerializerOptions { WriteIndented = true }));
        return 0;
    }
}
