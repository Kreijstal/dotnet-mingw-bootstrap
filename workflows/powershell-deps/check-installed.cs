using System;
using System.Globalization;
using System.Text.Json;
using System.Text.Json.Nodes;
using Humanizer;
using Json.More;
using Json.Pointer;
using Json.Schema;
using Markdig;
using Microsoft.PowerShell.MarkdownRender;

static void Require(bool condition, string message)
{
    if (!condition) throw new Exception(message);
}
CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("en-US");
using var input = JsonDocument.Parse("{\"markdown\":\"# Header\\n\\n**bold**\",\"id\":1}");
string markdown = JsonPointer.Parse("/markdown").Evaluate(input.RootElement).Value.GetString();
var rendered = MarkdownConverter.Convert(markdown, MarkdownConversionType.HTML, new PSMarkdownOptionInfo());
Require(rendered.Html.Contains("<strong>bold</strong>"), "JSON pointer to MarkdownRender integration failed");
Require(rendered.Tokens[0] is Markdig.Syntax.HeadingBlock, "Renderer AST lost Markdig identity");
Require("SomeProperty".Underscore() == "some_property", "Humanizer dependency failed");
using var equivalent = JsonDocument.Parse("{\"id\":1.0,\"markdown\":\"# Header\\n\\n**bold**\"}");
Require(input.IsEquivalentTo(equivalent), "JSON value equivalence failed");
Require(input.RootElement.GetEquivalenceHashCode() == equivalent.RootElement.GetEquivalenceHashCode(), "Patched numeric hashing failed");
var schema = JsonSchema.FromText("{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"},\"markdown\":{\"type\":\"string\"}},\"required\":[\"id\",\"markdown\"]}");
Require(schema.Evaluate(JsonNode.Parse(input.RootElement.GetRawText())).IsValid, "Schema validation failed");
Require(!schema.Evaluate(JsonNode.Parse("{\"id\":\"bad\"}")).IsValid, "Schema accepted invalid input");
foreach (var assembly in new[] { typeof(Markdown).Assembly, typeof(MarkdownConverter).Assembly,
    typeof(JsonElementExtensions).Assembly, typeof(JsonPointer).Assembly, typeof(JsonSchema).Assembly,
    typeof(StringHumanizeExtensions).Assembly })
{
    Require(assembly.GetName().GetPublicKeyToken().Length == 8, "Unsigned dependency: " + assembly.FullName);
    // Resolve every type to detect missing transitive assemblies as well as entry-point failures.
    Console.WriteLine(assembly.FullName + "; types=" + assembly.GetTypes().Length);
}
Console.WriteLine("PASS: six installed source-built PowerShell dependencies restore offline, load all types and interoperate");
