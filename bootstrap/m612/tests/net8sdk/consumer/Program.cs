Console.WriteLine(Bootstrap.Greeting.Text);
bool mono = Type.GetType("Mono.RuntimeStructs") is not null;
Console.WriteLine($"runtime: {System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription} ({(mono ? "Mono VM" : "CoreCLR")})");
