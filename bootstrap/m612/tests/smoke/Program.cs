using System.Diagnostics;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Xml.Linq;

int failures = 0;
void Check(string name, Func<object?> f)
{
    try { Console.WriteLine($"[ok]   {name}: {f()}"); }
    catch (Exception e) { failures++; Console.WriteLine($"[FAIL] {name}: {e.GetType().FullName}: {e.Message}\n{e.StackTrace}"); }
}

Check("file IO", () => {
    var dir = Path.Combine(Path.GetTempPath(), "m612-smoke-" + Environment.ProcessId);
    Directory.CreateDirectory(dir);
    var p = Path.Combine(dir, "a.txt");
    File.WriteAllLines(p, Enumerable.Range(1, 1000).Select(i => $"line {i} äöü"));
    using (var fs = new FileStream(p, FileMode.Append, FileAccess.Write, FileShare.None, 4096, useAsync: true))
        fs.WriteAsync(Encoding.UTF8.GetBytes("tail\n")).AsTask().Wait();
    var n = File.ReadLines(p).Count(); var len = new FileInfo(p).Length;
    var files = Directory.GetFiles(dir).Length;
    Directory.Delete(dir, true);
    return $"{n} lines, {len} bytes, {files} file(s), deleted={!Directory.Exists(dir)}";
});
Check("LINQ", () => string.Join(",", Enumerable.Range(1, 100).Where(i => i % 7 == 0).GroupBy(i => i % 2).Select(g => $"{g.Key}:{g.Sum()}").OrderBy(s => s)));
Check("LINQ expression compile", () => { System.Linq.Expressions.Expression<Func<int, int>> e = x => x * x + 1; return e.Compile()(7); });
Check("Regex (interpreted)", () => Regex.Match("2026-10-09", @"(\d+)-(\d+)-(\d+)").Groups[2].Value);
Check("Regex (compiled, Reflection.Emit)", () => Regex.Replace("Hello World", @"(?<w>\w+)", m => m.Value.ToUpperInvariant(), RegexOptions.None) + " / " +
    new Regex(@"\b[a-z]+\b", RegexOptions.Compiled | RegexOptions.IgnoreCase).Matches("The quick brown fox").Count);
Check("System.Text.Json", () => {
    var json = JsonSerializer.Serialize(new { Name = "m612", Values = new[] { 1, 2, 3 }, When = new DateTime(2026, 10, 9) });
    using var doc = JsonDocument.Parse(json);
    var back = JsonSerializer.Deserialize<Dictionary<string, JsonElement>>(json)!;
    return $"{json} -> {doc.RootElement.GetProperty("Values").GetArrayLength()} values, Name={back["Name"].GetString()}";
});
Check("XML (System.Private.Xml + Linq)", () => XDocument.Parse("<a><b x='1'/><b x='2'/></a>").Root!.Elements("b").Sum(b => (int)b.Attribute("x")!));
Check("Process", () => {
    var psi = new ProcessStartInfo("cmd.exe", "/c echo from-child") { RedirectStandardOutput = true, UseShellExecute = false };
    using var pr = Process.Start(psi)!; var o = pr.StandardOutput.ReadToEnd().Trim(); pr.WaitForExit();
    return $"'{o}' exit={pr.ExitCode}, self={Process.GetCurrentProcess().ProcessName}";
});
Check("sockets (loopback TCP)", () => {
    var l = new TcpListener(IPAddress.Loopback, 0); l.Start(); int port = ((IPEndPoint)l.LocalEndpoint).Port;
    var srv = Task.Run(async () => { using var c = await l.AcceptTcpClientAsync(); var s = c.GetStream(); var b = new byte[5]; int r = await s.ReadAsync(b); await s.WriteAsync(b.AsMemory(0, r)); });
    using var cl = new TcpClient(); cl.Connect(IPAddress.Loopback, port); var st = cl.GetStream();
    st.Write("ping!"u8); var buf = new byte[5]; int got = st.Read(buf); srv.Wait(); l.Stop();
    return $"echo '{Encoding.ASCII.GetString(buf, 0, got)}' via port {port}";
});
Check("HttpClient (loopback HttpListener)", () => {
    var hl = new HttpListener(); var port = 18000 + Environment.ProcessId % 1000; hl.Prefixes.Add($"http://127.0.0.1:{port}/"); hl.Start();
    var srv = Task.Run(() => { var ctx = hl.GetContext(); var b = Encoding.UTF8.GetBytes("hi from HttpListener"); ctx.Response.OutputStream.Write(b); ctx.Response.Close(); });
    using var http = new HttpClient(); var s = http.GetStringAsync($"http://127.0.0.1:{port}/x").Result; srv.Wait(); hl.Stop();
    return s;
});
Check("DNS", () => string.Join(",", Dns.GetHostAddresses("localhost").Select(a => a.AddressFamily)));
if (args.Contains("--net"))
    Check("HttpClient https (internet)", () => { using var h = new HttpClient(); var r = h.GetAsync("https://example.com/").Result; return $"{(int)r.StatusCode} {r.Content.Headers.ContentLength}"; });
Check("Reflection.Emit", () => {
    var ab = System.Reflection.Emit.AssemblyBuilder.DefineDynamicAssembly(new("dyn"), System.Reflection.Emit.AssemblyBuilderAccess.Run);
    var mb = ab.DefineDynamicModule("dyn"); var tb = mb.DefineType("T", System.Reflection.TypeAttributes.Public);
    var m = tb.DefineMethod("F", System.Reflection.MethodAttributes.Public | System.Reflection.MethodAttributes.Static, typeof(int), Type.EmptyTypes);
    var il = m.GetILGenerator(); il.Emit(System.Reflection.Emit.OpCodes.Ldc_I4, 1234); il.Emit(System.Reflection.Emit.OpCodes.Ret);
    return tb.CreateType()!.GetMethod("F")!.Invoke(null, null);
});
Check("compression (System.IO.Compression.Native)", () => {
    var data = Encoding.UTF8.GetBytes(string.Concat(Enumerable.Repeat("compress me ", 1000)));
    var ms = new MemoryStream(); using (var gz = new System.IO.Compression.GZipStream(ms, System.IO.Compression.CompressionLevel.Optimal, true)) gz.Write(data);
    var bs = new MemoryStream(); using (var br = new System.IO.Compression.BrotliStream(bs, System.IO.Compression.CompressionLevel.Optimal, true)) br.Write(data);
    ms.Position = 0; var back = new MemoryStream(); using (var gz = new System.IO.Compression.GZipStream(ms, System.IO.Compression.CompressionMode.Decompress)) gz.CopyTo(back);
    return $"{data.Length} -> gzip {ms.Length}, brotli {bs.Length}, roundtrip {back.Length == data.Length}";
});
Check("crypto (SHA256, AES via CNG)", () => {
    var h = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData("abc"u8));
    using var aes = System.Security.Cryptography.Aes.Create(); var ct = aes.EncryptCbc(new byte[32], aes.IV);
    return $"{h[..16]}... aes {ct.Length} bytes";
});
Check("GC + finalizers + weak refs", () => {
    var wr = new WeakReference(new object()); GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
    return $"collected={!wr.IsAlive}, gen2={GC.CollectionCount(2)}, heap={GC.GetTotalMemory(false) / 1024} KB";
});
Console.WriteLine(failures == 0 ? "SMOKE OK" : $"SMOKE: {failures} failure(s)");
return failures;
