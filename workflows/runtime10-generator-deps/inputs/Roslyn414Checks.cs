using System;
using System.Collections.Immutable;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
using System.Globalization;
using System.Resources;
using System.Threading;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Diagnostics;
using Microsoft.CodeAnalysis.Emit;
using Microsoft.CodeAnalysis.Text;

internal static class Check
{
    internal static void Require(bool value, string message)
    {
        if (!value) throw new Exception(message);
    }

    private static void Identity(Assembly assembly)
    {
        var name = assembly.GetName();
        Require(name.Version == new Version(4, 14, 0, 0), "Wrong assembly version: " + name);
        Require(Convert.ToHexString(name.GetPublicKeyToken()!).ToLowerInvariant() == "31bf3856ad364e35", "Wrong signing identity");
        Require(assembly.GetCustomAttribute<AssemblyFileVersionAttribute>()!.Version == "4.14.0.0", "Wrong file version");
        Require(assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()!.InformationalVersion ==
            "4.14.0+8edf7bcd4f1594c3d68a6a567469f41dbd33dd1b", "Wrong source provenance");
        Require(Path.GetDirectoryName(assembly.Location) == Path.TrimEndingDirectorySeparator(AppContext.BaseDirectory),
            "Library must load from consumer output: " + assembly.Location);
        Console.WriteLine("Loaded: " + name + "; " + assembly.Location);
    }

    private static (byte[] pe, byte[] pdb) Emit(CSharpCompilation compilation)
    {
        using var pe = new MemoryStream();
        using var pdb = new MemoryStream();
        var result = compilation.Emit(pe, pdb, options: new EmitOptions(debugInformationFormat: DebugInformationFormat.PortablePdb));
        Require(result.Success, string.Join(Environment.NewLine, result.Diagnostics));
        return (pe.ToArray(), pdb.ToArray());
    }

    private static int Main(string[] args)
    {
        try {
            System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
            System.Globalization.CultureInfo.CurrentUICulture = System.Globalization.CultureInfo.InvariantCulture;
            Require(args.Length == 5, "usage: check REFERENCE_PACK COMMON_PUBLIC_API CSHARP_PUBLIC_API COMMON_LOCALIZATION CSHARP_LOCALIZATION");
            Identity(typeof(Compilation).Assembly);
            Identity(typeof(CSharpCompilation).Assembly);
            foreach (var item in new[] {
                (typeof(SemanticModel), "NullableAnalysisIsDisabled", "RSEXPERIMENTAL001"),
                (typeof(GeneratorRunResult), "HostOutputs", "RSEXPERIMENTAL004") }) {
                var attribute = item.Item1.GetProperty(item.Item2)!.GetCustomAttributesData()
                    .Single(a => a.AttributeType.FullName == "System.Diagnostics.CodeAnalysis.ExperimentalAttribute");
                Require((string)attribute.ConstructorArguments[0].Value! == item.Item3,
                    "Original experimental property annotation missing: " + item.Item2);
            }
            var references = Directory.GetFiles(args[0], "*.dll")
                .Where(p => Path.GetFileNameWithoutExtension(p) is not ("System.Collections.Immutable" or "System.Reflection.Metadata"))
                .Concat(new[] { typeof(ImmutableArray<>).Assembly.Location, typeof(MetadataReader).Assembly.Location })
                .Select(p => MetadataReference.CreateFromFile(p)).ToArray();
            var options = CSharpParseOptions.Default.WithLanguageVersion(LanguageVersion.CSharp13);
            var text = SourceText.From("""
                using System.Linq;
                /// <summary>A compiler probe.</summary>
                public class Sample(int value) {
                    public static int Run() => new Sample(8).Sum();
                    private static int SumValues(params System.ReadOnlySpan<int> values) {
                        int sum = 0; foreach (int item in values) sum += item; return sum;
                    }
                    public int Sum() { int[] values = [value, 3]; return SumValues(values); }
                }
                """, Encoding.UTF8);
            var tree = CSharpSyntaxTree.ParseText(text, options, path: "sample.cs");
            var compilation = CSharpCompilation.Create("RoslynSourceProbe", new[] { tree }, references,
                new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, optimizationLevel: OptimizationLevel.Release,
                    allowUnsafe: true, nullableContextOptions: NullableContextOptions.Enable, deterministic: true));
            Require(!compilation.GetDiagnostics().Any(d => d.Severity == DiagnosticSeverity.Error), "Valid C# 13 rejected: " + string.Join("\n", compilation.GetDiagnostics()));
            var type = compilation.GetSemanticModel(tree).GetDeclaredSymbol(tree.GetRoot().DescendantNodes().OfType<ClassDeclarationSyntax>().Single());
            Require(type!.Name == "Sample" && type.InstanceConstructors.Single().Parameters.Single().Type.SpecialType == SpecialType.System_Int32,
                "Semantic model/primary constructor failed");
            var first = Emit(compilation);
            var second = Emit(compilation);
            Require(first.pe.SequenceEqual(second.pe) && first.pdb.SequenceEqual(second.pdb), "Deterministic output changed");
            var loaded = AssemblyLoadContext.Default.LoadFromStream(new MemoryStream(first.pe), new MemoryStream(first.pdb));
            Require((int)loaded.GetType("Sample")!.GetMethod("Run")!.Invoke(null, null)! == 11, "Emitted C# 13 did not execute");
            using (var reader = new PEReader(new MemoryStream(first.pe)))
                Require(reader.HasMetadata && reader.GetMetadataReader().GetAssemblyDefinition().Name.IsNil == false, "PE metadata missing");
            using (var provider = MetadataReaderProvider.FromPortablePdbStream(new MemoryStream(first.pdb))) {
                var reader = provider.GetMetadataReader();
                Require(reader.Documents.Count == 1 && reader.GetString(reader.GetDocument(reader.Documents.Single()).Name) == "sample.cs", "PDB source document lost");
                Require(reader.MethodDebugInformation.Any(h => reader.GetMethodDebugInformation(h).GetSequencePoints().Any()), "PDB sequence points missing");
            }
            Console.WriteLine("PASS: C#13 params spans, parsing/semantics, deterministic PE/portable PDB emission and execution");

            var broken = compilation.RemoveAllSyntaxTrees().AddSyntaxTrees(CSharpSyntaxTree.ParseText(
                "public class Broken { public int Get() => \"wrong\"; public string GetNull() => null; }", options));
            var diagnostics = broken.GetDiagnostics();
            Require(diagnostics.Any(d => d.Id == "CS0029" && d.GetMessage().Contains("convert")), "Error diagnostic/resource missing");
            Require(diagnostics.Any(d => d.Id == "CS8603" && d.GetMessage().Contains("null")), "Nullable warning/resource missing");
            Require(CSharpSyntaxTree.ParseText("class {", options).GetDiagnostics().Any(d => d.Severity == DiagnosticSeverity.Error), "Syntax errors not reported");
            var commandLine = CSharpCommandLineParser.Default.Parse(new[] { "/target:library", "/langversion:12", "/unsafe", "/nullable:enable", "sample.cs" },
                AppContext.BaseDirectory, args[0], null);
            Require(commandLine.Errors.Length == 0 && commandLine.CompilationOptions.AllowUnsafe && commandLine.ParseOptions.LanguageVersion == LanguageVersion.CSharp12,
                "PowerShell CompilerOptions-style argument parsing failed");
            bool cancelled = false;
            try { CSharpSyntaxTree.ParseText(text, options, cancellationToken: new CancellationToken(true)); }
            catch (OperationCanceledException) { cancelled = true; }
            Require(cancelled, "Cancellation ignored");
            Console.WriteLine("PASS: syntax/type/nullable diagnostics, resource text, compiler options and cancellation");

            var analyzerReference = new AnalyzerFileReference(typeof(Check).Assembly.Location, new AnalyzerLoader());
            var analyzers = analyzerReference.GetAnalyzers(LanguageNames.CSharp);
            Require(analyzers.Any(a => a is ProbeAnalyzer), "AnalyzerFileReference did not discover analyzer");
            var analyzerDiagnostics = compilation.WithAnalyzers(analyzers).GetAnalyzerDiagnosticsAsync().GetAwaiter().GetResult();
            Require(analyzerDiagnostics.Count(d => d.Id == "MSYS2001") == 1, "Analyzer callback/result mismatch");
            GeneratorDriver driver = CSharpGeneratorDriver.Create(new[] { new ProbeGenerator().AsSourceGenerator() },
                additionalTexts: new[] { new ExtraText("value.txt", "42") }, parseOptions: options);
            driver = driver.RunGeneratorsAndUpdateCompilation(compilation, out var generated, out var generatorDiagnostics);
            Require(generatorDiagnostics.IsEmpty && driver.GetRunResult().GeneratedTrees.Length == 1, "Incremental generation failed");
            var generatedPe = Emit((CSharpCompilation)generated.WithAssemblyName("RoslynGeneratedProbe"));
            var generatedAssembly = AssemblyLoadContext.Default.LoadFromStream(new MemoryStream(generatedPe.pe));
            Require((int)generatedAssembly.GetType("GeneratedNumber")!.GetField("Value")!.GetValue(null)! == 42, "Generated source did not execute");
            Console.WriteLine("PASS: analyzer file loading/callbacks and incremental generation with AdditionalText");
            CheckApi.Run(references, typeof(Compilation).Assembly.Location, args[1]);
            CheckApi.Run(references, typeof(CSharpCompilation).Assembly.Location, args[2], typeof(Compilation).Assembly.Location);
            int stringsChecked = 0, setsChecked = 0;
            foreach (string file in args.Skip(3)) {
                using var expected = JsonDocument.Parse(File.ReadAllText(file));
                foreach (var entry in expected.RootElement.EnumerateArray()) {
                    var assembly = entry.GetProperty("assembly").GetString() == "Microsoft.CodeAnalysis"
                        ? typeof(Compilation).Assembly : typeof(CSharpCompilation).Assembly;
                    var culture = CultureInfo.GetCultureInfo(entry.GetProperty("culture").GetString()!);
                    var manager = new ResourceManager(entry.GetProperty("resource").GetString()!, assembly);
                    var set = manager.GetResourceSet(culture, true, false);
                    if (set == null) {
                        var satellite = assembly.GetSatelliteAssembly(culture);
                        throw new Exception("Missing satellite resources: " + culture + "/" + manager.BaseName +
                            "; found manifest names: " + string.Join(", ", satellite.GetManifestResourceNames()));
                    }
                    foreach (var item in entry.GetProperty("strings").EnumerateObject()) {
                        Require(set!.GetString(item.Name) == item.Value.GetString(), "Localized resource mismatch: " + culture + "/" + item.Name);
                        stringsChecked++;
                    }
                    setsChecked++;
                }
            }
            Require(setsChecked == 42 && stringsChecked > 10000, "Incomplete localization coverage");
            var germanMessage = diagnostics.First(d => d.Id == "CS0029").GetMessage(CultureInfo.GetCultureInfo("de"));
            Require(germanMessage.Contains("implizit") && !germanMessage.Contains("implicitly"), "German compiler diagnostic fell back to English");
            Console.WriteLine("PASS: all 13 cultures, " + setsChecked + " localized resource sets / " + stringsChecked + " strings, and German diagnostics");
            using var process = Process.GetCurrentProcess();
            foreach (ProcessModule module in process.Modules)
                Require(!module.FileName.Contains("\\Microsoft.NET\\Framework", StringComparison.OrdinalIgnoreCase), "Framework helper loaded: " + module.FileName);
            Console.WriteLine("PASS: no .NET Framework native helper loaded");
            return 0;
        } catch (Exception error) {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}

internal sealed class AnalyzerLoader : IAnalyzerAssemblyLoader
{
    public void AddDependencyLocation(string fullPath) { }
    public Assembly LoadFromPath(string fullPath) => AssemblyLoadContext.Default.LoadFromAssemblyPath(fullPath);
}

[DiagnosticAnalyzer(LanguageNames.CSharp)]
public sealed class ProbeAnalyzer : DiagnosticAnalyzer
{
    private static readonly DiagnosticDescriptor Rule = new("MSYS2001", "Class probe", "Found class {0}", "Probe", DiagnosticSeverity.Warning, true);
    public override ImmutableArray<DiagnosticDescriptor> SupportedDiagnostics => ImmutableArray.Create(Rule);
    public override void Initialize(AnalysisContext context)
    {
        context.ConfigureGeneratedCodeAnalysis(GeneratedCodeAnalysisFlags.None);
        context.EnableConcurrentExecution();
        context.RegisterSyntaxNodeAction(c => c.ReportDiagnostic(Diagnostic.Create(Rule, c.Node.GetLocation(), ((ClassDeclarationSyntax)c.Node).Identifier.Text)), SyntaxKind.ClassDeclaration);
    }
}

internal sealed class ExtraText(string path, string contents) : AdditionalText
{
    public override string Path => path;
    public override SourceText GetText(CancellationToken cancellationToken = default) => SourceText.From(contents, Encoding.UTF8);
}

[Generator]
internal sealed class ProbeGenerator : IIncrementalGenerator
{
    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        var values = context.AdditionalTextsProvider.Select((text, token) => int.Parse(text.GetText(token)!.ToString())).Collect();
        context.RegisterSourceOutput(values, (production, input) => production.AddSource("Number.g.cs",
            SourceText.From("public static class GeneratedNumber { public const int Value = " + input.Single() + "; }", Encoding.UTF8)));
    }
}
