// ImplicitUsings provides System, System.Linq, System.Collections.Generic ...
var words = new List<string> { "sdk", "style", "on", "mono" };
Console.WriteLine($"Hello from an SDK-style project: {string.Join(" ", words.Select(w => w.ToUpperInvariant()))}");
Console.WriteLine(typeof(object).Assembly.Location.Contains("mono") ? "running on Mono" : "not on Mono?");
