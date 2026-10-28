using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Linq.Expressions;
using System.Net;
using System.Net.Http;
using System.Net.Security;
using System.Net.Sockets;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Xml.Linq;

internal static class RuntimeCheck
{
    private const string Commit = "1219a42122cf5190c7f512850557e38c422430dc";
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private static async Task Main(string[] args)
    {
        if (args.Length == 1 && args[0] == "--tls-only")
        {
            Console.WriteLine("TLS diagnostic runtime: " + RuntimeInformation.FrameworkDescription);
            using RSA rsa = RSA.Create(2048);
            await CheckTls(rsa);
            Console.WriteLine("PASS owned loopback TLS diagnostic");
            return;
        }
        Console.WriteLine("Runtime: " + RuntimeInformation.FrameworkDescription);
        Require(Environment.Version == new Version(8, 0, 31), "Actual runtime is not 8.0.31");
        string framework = Path.GetDirectoryName(typeof(object).Assembly.Location)!;
        Require(Path.GetFullPath(framework).Equals(Path.GetFullPath(args[0]), StringComparison.OrdinalIgnoreCase), "CoreLib loaded outside isolated serviced framework");
        foreach (string name in File.ReadAllLines(args[1]))
        {
            Assembly assembly = Assembly.Load(new AssemblyName(name));
            Require(Path.GetDirectoryName(assembly.Location)!.Equals(framework, StringComparison.OrdinalIgnoreCase), name + " loaded outside serviced framework");
            Require(!assembly.CustomAttributes.Any(a => a.AttributeType.FullName == "System.Runtime.CompilerServices.ReferenceAssemblyAttribute"), name + " is a reference assembly");
            Console.WriteLine("LOAD " + name + " " + assembly.GetName().Version);
        }
        string info = typeof(object).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()!.InformationalVersion;
        Require(info.Contains("8.0.31") && info.Contains(Commit), "CoreLib version/source commit mismatch: " + info);
        foreach (string name in File.ReadAllLines(args[1]))
            Require(FileVersionInfo.GetVersionInfo(Path.Combine(framework, name + ".dll")).FileVersion == "8.0.3126.42000", name + " has incorrect servicing file metadata");
        foreach (string name in new[] { "coreclr.dll", "clrjit.dll", "mscorrc.dll", "clretwrc.dll", "hostpolicy.dll", "System.IO.Compression.Native.dll" })
            Require(FileVersionInfo.GetVersionInfo(Path.Combine(framework, name)).FileVersion == "8.0.3126.42000", name + " has incorrect native servicing file metadata");
        Console.WriteLine("PASS complete framework loads and actual serviced CoreLib identity/source: " + info);

        int finalized = 0;
        for (int i = 0; i < 2000; i++)
        {
            try { throw new ArgumentException("owned-" + i); }
            catch (ArgumentException e) when (e.Message == "owned-" + i) { }
            finally { finalized++; }
        }
        Require(finalized == 2000, "Exception/filter/finally accounting");
        int sum = 0;
        await Task.WhenAll(Enumerable.Range(1, 64).Select(async n => { await Task.Yield(); Interlocked.Add(ref sum, n); }));
        Require(sum == 2080, "Thread-pool work lost");
        GC.Collect(2, GCCollectionMode.Forced, true, true);
        GC.WaitForPendingFinalizers();
        Require(Expression.Lambda<Func<int>>(Expression.Add(Expression.Constant(20), Expression.Constant(22))).Compile()() == 42, "Reflection.Emit/JIT");
        Require(new Regex("^(?<word>\\p{L}+)-(?<number>[0-9]+)$", RegexOptions.Compiled).Match("Grüße-42").Groups["number"].Value == "42", "Compiled Unicode regex");
        Require(Microsoft.VisualBasic.Strings.StrReverse("abcdef") == "fedcba", "VB core library");
        Console.WriteLine("PASS CLR exception/filter/finally, tasks/GC, dynamic compilation, compiled regex and VB");

        var culture = CultureInfo.GetCultureInfo("de-DE");
        Require(1234.5m.ToString("N1", culture) == "1.234,5", "Culture formatting");
        Require(culture.CompareInfo.Compare("Ä", "ä", CompareOptions.IgnoreCase) == 0, "Native linguistic comparison");
        string json = JsonSerializer.Serialize(new Dictionary<string, string> { ["Grüße"] = "日本語 & <xml>" });
        Require(JsonSerializer.Deserialize<Dictionary<string, string>>(json)!["Grüße"] == "日本語 & <xml>", "JSON Unicode round trip");
        Require(XDocument.Parse("<root a='✓'><child>日本語</child></root>").Root!.Element("child")!.Value == "日本語", "XML parser");
        Console.WriteLine("PASS native globalization, serviced JSON and XML");

        byte[] data = Enumerable.Range(0, 200000).Select(n => (byte)(n * 19 + n / 113)).ToArray();
        foreach (string codec in new[] { "gzip", "deflate", "brotli" })
        {
            using var compressed = new MemoryStream();
            using (Stream encoder = Codec(codec, compressed, true)) encoder.Write(data);
            compressed.Position = 0;
            using Stream decoder = Codec(codec, compressed, false);
            using var restored = new MemoryStream();
            decoder.CopyTo(restored);
            Require(data.SequenceEqual(restored.ToArray()), codec + " managed/native round trip");
        }
        using (var zip = new MemoryStream())
        {
            using (var archive = new ZipArchive(zip, ZipArchiveMode.Create, true))
            using (var output = archive.CreateEntry("Grüße/日本語.bin").Open()) output.Write(data);
            zip.Position = 0;
            using var archiveIn = new ZipArchive(zip, ZipArchiveMode.Read);
            using var input = archiveIn.GetEntry("Grüße/日本語.bin")!.Open();
            using var restored = new MemoryStream(); input.CopyTo(restored);
            Require(data.SequenceEqual(restored.ToArray()), "ZIP round trip");
        }
        Console.WriteLine("PASS source native compression through managed gzip/deflate/Brotli/ZIP APIs");

        Require(Convert.ToHexString(SHA256.HashData(Encoding.ASCII.GetBytes("abc"))) == "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", "SHA256 known vector");
        using (RSA rsa = RSA.Create(2048))
        {
            byte[] signature = rsa.SignData(data, HashAlgorithmName.SHA256, RSASignaturePadding.Pss);
            Require(rsa.VerifyData(data, signature, HashAlgorithmName.SHA256, RSASignaturePadding.Pss), "RSA CNG round trip");
            using var aes = new AesGcm(RandomNumberGenerator.GetBytes(32), 16);
            byte[] nonce = RandomNumberGenerator.GetBytes(12), tag = new byte[16], ciphertext = new byte[data.Length], clear = new byte[data.Length];
            aes.Encrypt(nonce, data, ciphertext, tag); aes.Decrypt(nonce, ciphertext, tag, clear);
            Require(data.SequenceEqual(clear), "AES-GCM round trip");
            await CheckTls(rsa);
        }
        Console.WriteLine("PASS SHA256 vector, native RSA/AES-GCM, ephemeral certificate and owned loopback TLS/HTTP");

        string temp = Path.Combine(Path.GetTempPath(), "runtime-servicing-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(temp);
        try
        {
            var observed = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            using var watcher = new FileSystemWatcher(temp) { EnableRaisingEvents = true };
            watcher.Created += (_, e) => { if (e.Name == "owned.txt") observed.TrySetResult(true); };
            await File.WriteAllTextAsync(Path.Combine(temp, "owned.txt"), "Grüße 日本語");
            Require(await observed.Task.WaitAsync(TimeSpan.FromSeconds(15)), "Native file-system event");
            Require(await File.ReadAllTextAsync(Path.Combine(temp, "owned.txt")) == "Grüße 日本語", "Async file I/O");
        }
        finally { Directory.Delete(temp, true); }
        Console.WriteLine("PASS async Unicode file I/O and native FileSystemWatcher");

        foreach (ProcessModule module in Process.GetCurrentProcess().Modules)
        {
            if (module.ModuleName.Equals("coreclr.dll", StringComparison.OrdinalIgnoreCase) ||
                module.ModuleName.Equals("clrjit.dll", StringComparison.OrdinalIgnoreCase) ||
                module.ModuleName.Equals("hostpolicy.dll", StringComparison.OrdinalIgnoreCase) ||
                module.ModuleName.Equals("System.IO.Compression.Native.dll", StringComparison.OrdinalIgnoreCase))
            {
                Require(Path.GetDirectoryName(module.FileName)!.Equals(framework, StringComparison.OrdinalIgnoreCase), "Foreign native runtime component: " + module.FileName);
                Console.WriteLine("NATIVE " + module.FileName);
            }
        }
        Console.WriteLine("PASS isolated actual .NET8.0.31 runtime execution; broader compatibility and packaging remain required");
    }

    private static Stream Codec(string name, Stream stream, bool encode) => (name, encode) switch
    {
        ("gzip", true) => new GZipStream(stream, CompressionLevel.Optimal, true),
        ("gzip", false) => new GZipStream(stream, CompressionMode.Decompress, true),
        ("deflate", true) => new DeflateStream(stream, CompressionLevel.Optimal, true),
        ("deflate", false) => new DeflateStream(stream, CompressionMode.Decompress, true),
        ("brotli", true) => new BrotliStream(stream, CompressionLevel.Optimal, true),
        ("brotli", false) => new BrotliStream(stream, CompressionMode.Decompress, true),
        _ => throw new ArgumentException(name)
    };

    private static async Task CheckTls(RSA rsa)
    {
        var request = new CertificateRequest("CN=localhost", rsa, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
        var san = new SubjectAlternativeNameBuilder(); san.AddDnsName("localhost");
        request.CertificateExtensions.Add(san.Build());
        using var generated = request.CreateSelfSigned(DateTimeOffset.UtcNow.AddMinutes(-5), DateTimeOffset.UtcNow.AddMinutes(10));
        // Windows Schannel cannot use CreateSelfSigned's ephemeral private key.
        // Import an owned PFX with normal, automatically cleaned user-key storage;
        // no PersistKeySet flag or certificate-store installation is involved.
        byte[] pfx = generated.Export(X509ContentType.Pfx, "owned-runtime-fixture");
        using var certificate = new X509Certificate2(pfx, "owned-runtime-fixture", X509KeyStorageFlags.UserKeySet);
        CryptographicOperations.ZeroMemory(pfx);
        var listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        try
        {
            int port = ((IPEndPoint)listener.LocalEndpoint).Port;
            Task server = Task.Run(async () =>
            {
                using TcpClient accepted = await listener.AcceptTcpClientAsync(deadline.Token);
                using var ssl = new SslStream(accepted.GetStream(), false);
                await ssl.AuthenticateAsServerAsync(new SslServerAuthenticationOptions { ServerCertificate = certificate, EnabledSslProtocols = SslProtocols.Tls12 | SslProtocols.Tls13 }, deadline.Token);
                using var reader = new StreamReader(ssl, Encoding.ASCII, false, 1024, true);
                string? first = await reader.ReadLineAsync(deadline.Token);
                Require(first == "GET /owned HTTP/1.1", "Loopback HTTP request: " + first);
                while (!string.IsNullOrEmpty(await reader.ReadLineAsync(deadline.Token))) { }
                await ssl.WriteAsync(Encoding.ASCII.GetBytes("HTTP/1.1 200 OK\r\nContent-Length: 13\r\nConnection: close\r\n\r\nowned-tls-ok!"), deadline.Token);
            });
            using var handler = new HttpClientHandler { UseProxy = false };
            handler.ServerCertificateCustomValidationCallback = (_, cert, _, errors) => cert != null && cert.RawData.SequenceEqual(certificate.RawData);
            using var client = new HttpClient(handler);
            try
            {
                Require(await client.GetStringAsync("https://localhost:" + port + "/owned", deadline.Token) == "owned-tls-ok!", "Loopback TLS response");
            }
            catch
            {
                try { await server; }
                catch (Exception error) { Console.Error.WriteLine("TLS SERVER FAILURE: " + error); }
                throw;
            }
            await server;
        }
        finally { listener.Stop(); }
    }
}
