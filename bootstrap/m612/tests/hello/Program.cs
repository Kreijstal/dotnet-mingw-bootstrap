using System.Globalization;
using System.Runtime.InteropServices;

Console.WriteLine($"Hello, World! {RuntimeInformation.FrameworkDescription} on {RuntimeInformation.OSDescription} ({RuntimeInformation.ProcessArchitecture})");
Console.WriteLine($"CoreLib: {typeof(object).Assembly.Location}");

// exceptions through JIT frames, finally, filters, rethrow, nested
int Depth(int n) => n == 0 ? throw new InvalidOperationException("deep") : Depth(n - 1) + 1;
try { Depth(50); }
catch (InvalidOperationException e) when (e.Message == "deep") { Console.WriteLine($"caught {e.GetType().Name}: {e.Message}; frames: {e.StackTrace?.Split('\n').Length}"); }
finally { Console.WriteLine("finally ran"); }
try { try { int z = 0; Console.WriteLine(1 / z); } catch (DivideByZeroException) { throw; } }
catch (Exception e) { Console.WriteLine($"rethrown {e.GetType().Name}"); }
try { string? s = null; Console.WriteLine(s!.Length); } catch (NullReferenceException) { Console.WriteLine("NullReferenceException caught (hardware fault -> managed exception)"); }

// threads
long total = 0;
var threads = Enumerable.Range(0, 4).Select(i => new Thread(() => { for (int k = 0; k < 1_000_000; k++) Interlocked.Increment(ref total); })).ToList();
threads.ForEach(t => t.Start()); threads.ForEach(t => t.Join());
Console.WriteLine($"4 threads x 1e6 increments = {total}");
var sum = await Task.WhenAll(Enumerable.Range(1, 8).Select(i => Task.Run(() => { Thread.Sleep(10); return i * i; })));
Console.WriteLine($"tasks: {sum.Sum()} (thread pool, async/await)");
var mre = new ManualResetEventSlim(); ThreadPool.QueueUserWorkItem(_ => mre.Set()); Console.WriteLine($"thread pool work item: {mre.Wait(5000)}");

// globalization through Windows' icu.dll
Console.WriteLine($"invariant mode: {AppContext.TryGetSwitch("System.Globalization.Invariant", out bool inv) && inv}");
var tr = new CultureInfo("tr-TR"); var de = new CultureInfo("de-DE");
Console.WriteLine($"ToUpper: {"straße istanbul".ToUpper(de)} / tr: {"istanbul".ToUpper(tr)} / invariant: {"istanbul".ToUpperInvariant()}");
Console.WriteLine($"de-DE: {1234567.891.ToString("N2", de)} {new DateTime(2026, 10, 9).ToString("D", de)}");
Console.WriteLine($"ja-JP: {new DateTime(2026, 10, 9).ToString("D", new CultureInfo("ja-JP"))}");
Console.WriteLine($"compare ä/z sv-SE: {string.Compare("ä", "z", new CultureInfo("sv-SE"), CompareOptions.None)}, de-DE: {string.Compare("ä", "z", de, CompareOptions.None)}");
Console.WriteLine($"IndexOf ignore case: {"Straße".IndexOf("SS", StringComparison.CurrentCultureIgnoreCase)}, normalization: {"é".Normalize().Length}");
Console.WriteLine($"current culture: {CultureInfo.CurrentCulture.Name}, time zone: {TimeZoneInfo.Local.Id}");
return 0;
