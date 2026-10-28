using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Resources;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CodeActions;
using Microsoft.CodeAnalysis.CodeFixes;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.CSharp.Formatting;
using Microsoft.CodeAnalysis.Diagnostics;
using Microsoft.CodeAnalysis.Editing;
using Microsoft.CodeAnalysis.FindSymbols;
using Microsoft.CodeAnalysis.Formatting;
using Microsoft.CodeAnalysis.Host.Mef;
using Microsoft.CodeAnalysis.Rename;
using Microsoft.CodeAnalysis.Simplification;
using Microsoft.CodeAnalysis.Text;

[DiagnosticAnalyzer(LanguageNames.CSharp)]
public sealed class LowerNameAnalyzer : DiagnosticAnalyzer
{
 public static readonly DiagnosticDescriptor Rule=new("WSP001","Capitalize class","Capitalize '{0}'","Workspace test",DiagnosticSeverity.Warning,true);
 public override ImmutableArray<DiagnosticDescriptor> SupportedDiagnostics=>ImmutableArray.Create(Rule);
 public override void Initialize(AnalysisContext context)
 {
  context.ConfigureGeneratedCodeAnalysis(GeneratedCodeAnalysisFlags.None);context.EnableConcurrentExecution();
  context.RegisterSyntaxNodeAction(c=>{var node=(ClassDeclarationSyntax)c.Node;if(char.IsLower(node.Identifier.ValueText[0]))c.ReportDiagnostic(Diagnostic.Create(Rule,node.Identifier.GetLocation(),node.Identifier.ValueText));},SyntaxKind.ClassDeclaration);
 }
}
public sealed class UpperNameFix : CodeFixProvider
{
 public override ImmutableArray<string> FixableDiagnosticIds=>ImmutableArray.Create("WSP001");
 public override FixAllProvider GetFixAllProvider()=>WellKnownFixAllProviders.BatchFixer;
 public override Task RegisterCodeFixesAsync(CodeFixContext context)
 {
  context.RegisterCodeFix(CodeAction.Create("Capitalize",async token=>{
   var editor=await DocumentEditor.CreateAsync(context.Document,token);
   var node=editor.OriginalRoot.FindToken(context.Span.Start).Parent.AncestorsAndSelf().OfType<ClassDeclarationSyntax>().First();
   string name=node.Identifier.ValueText;
   editor.ReplaceNode(node,node.WithIdentifier(SyntaxFactory.Identifier(char.ToUpperInvariant(name[0])+name.Substring(1)).WithTriviaFrom(node.Identifier)));
   return editor.GetChangedDocument();
  },"capitalize-owned-class"),context.Diagnostics);return Task.CompletedTask;
 }
}
class Program
{
 internal static void Require(bool value,string message){if(!value)throw new Exception(message);}
 static void Source(string resourcesPath,string itemsPath)
 {
  using var sources=JsonDocument.Parse(File.ReadAllText(itemsPath));
  foreach(string name in new[]{"Microsoft.CodeAnalysis.Workspaces","Microsoft.CodeAnalysis.CSharp.Workspaces"})
  {
   var assembly=Assembly.Load(new AssemblyName(name));
   Require(assembly.GetName().Version==new Version(4,14,0,0)&&Convert.ToHexString(assembly.GetName().GetPublicKeyToken()).ToLowerInvariant()=="31bf3856ad364e35","Assembly identity: "+name);
   Require(assembly.GetCustomAttribute<AssemblyFileVersionAttribute>().Version=="4.14.0.0"&&assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>().InformationalVersion=="4.14.0+8edf7bcd4f1594c3d68a6a567469f41dbd33dd1b","Source identity: "+name);
   Require(Path.GetRelativePath(AppContext.BaseDirectory,assembly.Location)==name+".dll","Source consumer assembly location");
   var friends=assembly.GetCustomAttributes<InternalsVisibleToAttribute>().Select(e=>e.AssemblyName).ToArray();
   var declarations=sources.RootElement.GetProperty(name).GetProperty("friend_attributes").EnumerateArray().Select(e=>e.GetString()).Where(s=>s.StartsWith("[assembly: System.Runtime.CompilerServices.InternalsVisibleTo(")).Distinct(StringComparer.Ordinal).ToArray();
   Console.WriteLine(name+" friend attributes: actual="+friends.Length+" expected="+declarations.Length);
   foreach(var friend in friends.Where(f=>f.Contains("$(")||!f.Contains("PublicKey=")))Console.WriteLine("Invalid friend: "+friend);
   Require(friends.Length==declarations.Length&&friends.All(f=>!f.Contains("$(")&&f.Contains("PublicKey=")),"Unexpanded or missing friend identity: "+name);
   foreach(string declaration in declarations)
   {
    string literal=declaration.Substring(declaration.IndexOf('(')+1);literal=literal.Substring(0,literal.Length-2);
    Require(friends.Contains(JsonSerializer.Deserialize<string>(literal)),"Friend declaration changed");
   }
   var restricted=assembly.GetCustomAttributesData().Where(a=>a.AttributeType.FullName=="System.Runtime.CompilerServices.RestrictedInternalsVisibleToAttribute").ToArray();
   var wantedRestricted=sources.RootElement.GetProperty(name).GetProperty("friend_attributes").EnumerateArray().Select(e=>e.GetString()).Where(x=>x.StartsWith("[assembly: System.Runtime.CompilerServices.RestrictedInternalsVisibleTo(")).Distinct(StringComparer.Ordinal).ToArray();
   Require(restricted.Length==wantedRestricted.Length,"Restricted friend attribute count");
   foreach(string declaration in wantedRestricted)
   {
    string literal=declaration.Substring(declaration.IndexOf('(')+1);literal=literal.Substring(0,literal.Length-2);
    var pair=JsonSerializer.Deserialize<string[]>("["+literal+"]");
    Require(restricted.Any(a=>(string)a.ConstructorArguments[0].Value==pair[0]&&((IEnumerable<CustomAttributeTypedArgument>)a.ConstructorArguments[1].Value).Select(x=>(string)x.Value).SequenceEqual(new[]{pair[1]})),"Restricted friend namespace changed");
   }
   assembly.GetExportedTypes();
  }
  using var entries=JsonDocument.Parse(File.ReadAllText(resourcesPath));int sets=0,strings=0;
  foreach(var entry in entries.RootElement.EnumerateArray())
  {
   var assembly=Assembly.Load(new AssemblyName(entry.GetProperty("assembly").GetString()));string culture=entry.GetProperty("culture").GetString();
   if(culture.Length!=0)assembly=assembly.GetSatelliteAssembly(System.Globalization.CultureInfo.GetCultureInfo(culture));
   string name=entry.GetProperty("resource").GetString()+(culture.Length==0?"":"."+culture)+".resources";
   using var reader=new ResourceReader(assembly.GetManifestResourceStream(name)??throw new Exception(name));
   var actual=reader.Cast<System.Collections.DictionaryEntry>().ToDictionary(e=>(string)e.Key,e=>(string)e.Value);
   Require(actual.Count==entry.GetProperty("strings").EnumerateObject().Count(),"Resource count: "+name);
   foreach(var pair in entry.GetProperty("strings").EnumerateObject()){Require(actual.TryGetValue(pair.Name,out var value)&&value==pair.Value.GetString(),"Source translation: "+name+"/"+pair.Name);strings++;}sets++;
  }
  int expectedStrings=entries.RootElement.EnumerateArray().Sum(e=>e.GetProperty("strings").EnumerateObject().Count());
  Require(sets==entries.RootElement.GetArrayLength()&&sets==98&&strings==expectedStrings,"Complete original neutral/XLF resource manifest");
  foreach(System.Diagnostics.ProcessModule module in System.Diagnostics.Process.GetCurrentProcess().Modules)
   Require(!module.FileName.Replace('\\','/').Contains("/Microsoft.NET/Framework",StringComparison.OrdinalIgnoreCase),"Framework implementation loaded");
  Console.WriteLine($"PASS source identity/location/types and all expanded friend declarations; {sets} resource sets/{strings} source strings");
 }
 static async Task Main(string[] args)
 {
  Source(args[0],args[1]);
  await ChannelChecks.Run();
  var host=MefHostServices.Create(new[]{typeof(Workspace).Assembly,typeof(CSharpFormattingOptions).Assembly});
  using var workspace=new AdhocWorkspace(host);
  var dependencies=Directory.GetFiles(AppContext.BaseDirectory,"*.dll")
   .Where(p=>Path.GetFileNameWithoutExtension(p) is not ("Check" or "Microsoft.CodeAnalysis" or "Microsoft.CodeAnalysis.CSharp" or "Microsoft.CodeAnalysis.Workspaces" or "Microsoft.CodeAnalysis.CSharp.Workspaces")).ToArray();
  var names=dependencies.Select(Path.GetFileName).ToHashSet(StringComparer.OrdinalIgnoreCase);
  var references=Directory.GetFiles(args[2],"*.dll").Where(p=>!names.Contains(Path.GetFileName(p)))
   .Concat(dependencies)
   .Select(f=>MetadataReference.CreateFromFile(f)).ToArray();
  foreach(var item in new[]{(typeof(Workspace).Assembly,"common"),(typeof(CSharpFormattingOptions).Assembly,"csharp")})
   CheckApi.Run(references,item.Item1.Location,Path.Combine(args[3],"api-"+item.Item2+".txt"),
    typeof(Compilation).Assembly.Location,typeof(CSharpCompilation).Assembly.Location,typeof(Workspace).Assembly.Location,typeof(CSharpFormattingOptions).Assembly.Location);
  ProjectId library=ProjectId.CreateNewId(),client=ProjectId.CreateNewId();
  var solution=workspace.CurrentSolution.AddProject(ProjectInfo.Create(library,VersionStamp.Create(),"OwnedLibrary","OwnedLibrary",LanguageNames.CSharp,compilationOptions:new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary),parseOptions:new CSharpParseOptions(LanguageVersion.CSharp12),metadataReferences:references))
   .AddProject(ProjectInfo.Create(client,VersionStamp.Create(),"OwnedClient","OwnedClient",LanguageNames.CSharp,compilationOptions:new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary),parseOptions:new CSharpParseOptions(LanguageVersion.CSharp12),metadataReferences:references,projectReferences:new[]{new ProjectReference(library)}));
  var libDoc=DocumentId.CreateNewId(library);var clientDoc=DocumentId.CreateNewId(client);
  solution=solution.AddDocument(libDoc,"math-π.cs",SourceText.From("namespace Lib { public static class Calc { public static int Add(int a,int b)=>a+b; } }",Encoding.UTF8))
   .AddDocument(clientDoc,"client.cs",SourceText.From("public class Entry{public static int Run()=>Lib.Calc.Add(20,22);}",Encoding.UTF8));
  Require(workspace.TryApplyChanges(solution),"Initial solution changes");
  var compilation=await workspace.CurrentSolution.GetProject(library).GetCompilationAsync();
  var method=(IMethodSymbol)compilation.GetTypeByMetadataName("Lib.Calc").GetMembers("Add").Single();
  var found=(await SymbolFinder.FindReferencesAsync(method,workspace.CurrentSolution)).ToArray();
  Require(found.SelectMany(x=>x.Locations).Count()==1&&found.SelectMany(x=>x.Locations).Single().Document.Id==clientDoc,"Cross-project source references");
  solution=await Renamer.RenameSymbolAsync(workspace.CurrentSolution,method,new SymbolRenameOptions(),"Sum");
  Require(workspace.TryApplyChanges(solution),"Apply semantic rename");
  Require((await workspace.CurrentSolution.GetDocument(libDoc).GetTextAsync()).ToString().Contains("Sum")&&(await workspace.CurrentSolution.GetDocument(clientDoc).GetTextAsync()).ToString().Contains("Calc.Sum"),"Semantic rename across project boundary");
  var formatted=await Formatter.FormatAsync(workspace.CurrentSolution.GetDocument(clientDoc));
  Require((await formatted.GetTextAsync()).ToString().Contains("Run() => Lib.Calc.Sum(20, 22)"),"CSharp formatter service");
  Require(workspace.TryApplyChanges(formatted.Project.Solution),"Apply formatted document");
  var clientCompilation=await workspace.CurrentSolution.GetProject(client).GetCompilationAsync();
  Require(!clientCompilation.GetDiagnostics().Any(x=>x.Severity==DiagnosticSeverity.Error),"Project-reference compilation diagnostics");
  using(var pe=new MemoryStream())Require(clientCompilation.Emit(pe).Success&&pe.Length>0,"Workspace compilation emit");
  Console.WriteLine("PASS MEF-hosted CSharp workspace, Unicode documents, project references, semantic models, cross-project reference search/rename, formatting, solution changes and PE emit");
  var fixId=DocumentId.CreateNewId(client);solution=workspace.CurrentSolution.AddDocument(fixId,"fix.cs",SourceText.From("class lower { global::System.String Name = \"owned\"; }",Encoding.UTF8));
  Require(workspace.TryApplyChanges(solution),"Add code-fix source");
  var doc=workspace.CurrentSolution.GetDocument(fixId);
  doc=doc.WithSyntaxRoot((await doc.GetSyntaxRootAsync()).WithAdditionalAnnotations(Simplifier.Annotation));
  var simplified=await Simplifier.ReduceAsync(doc);
  Require((await simplified.GetTextAsync()).ToString().Contains("string Name"),"CSharp simplifier service");
  Require(workspace.TryApplyChanges(simplified.Project.Solution),"Apply simplified document");
  doc=workspace.CurrentSolution.GetDocument(fixId);compilation=await doc.Project.GetCompilationAsync();
  var diagnostics=await compilation.WithAnalyzers(ImmutableArray.Create<DiagnosticAnalyzer>(new LowerNameAnalyzer())).GetAnalyzerDiagnosticsAsync();
  var diagnostic=diagnostics.Single(d=>d.Id=="WSP001");CodeAction action=null;
  await new UpperNameFix().RegisterCodeFixesAsync(new CodeFixContext(doc,diagnostic,(a,_)=>action=a,CancellationToken.None));Require(action!=null,"Analyzer-backed code action registration");
  foreach(var operation in await action.GetOperationsAsync(CancellationToken.None))operation.Apply(workspace,CancellationToken.None);
  Require((await workspace.CurrentSolution.GetDocument(fixId).GetTextAsync()).ToString().Contains("class Lower"),"Code action operation edits workspace");
  var old=workspace.CurrentSolution;var changed=old.WithDocumentText(fixId,SourceText.From("class Lower { public int Value=>42; }",Encoding.UTF8));
  Require(changed.GetChanges(old).GetProjectChanges().Single().GetChangedDocuments().Single()==fixId,"Incremental solution change tracking");
  using var cancelled=new CancellationTokenSource();cancelled.Cancel();
  try{await SymbolFinder.FindReferencesAsync(method,changed,cancelled.Token);throw new Exception("Cancelled workspace request accepted");}catch(OperationCanceledException){}
  Console.WriteLine("PASS CSharp simplification, real analyzer diagnostics, DocumentEditor/code-fix registration, CodeAction/ApplyChanges operation, incremental solution edits and cancellation");
 }
}
