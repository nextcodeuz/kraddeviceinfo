// krad.device.info - KradUpdate: C# (.NET 8) self-updater for KradDeviceInfo.
// Checks a version manifest, verifies SHA256, downloads and applies the new
// portable package with backup/rollback. The C++ core stays dependency-free;
// this C# companion shows the managed side of the Krad toolchain.
using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text.Json;
using System.Threading.Tasks;

const string Usage =
    "KradUpdate - krad.device.info self-updater\n\n" +
    "Usage:\n" +
    "  KradUpdate check   <manifest-url> [current-version]\n" +
    "  KradUpdate install <manifest-url> <install-dir>\n" +
    "  KradUpdate help\n\n" +
    "Manifest format (version.json):\n" +
    "  { \"version\": \"1.1.0\", \"url\": \"<zip>\", \"sha256\": \"<hex>\", \"notes\": \"...\" }\n";

if (args.Length < 2) { Console.Write(Usage); return 2; }

using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(120) };
http.DefaultRequestHeaders.Add("User-Agent", "KradUpdate/1.1");

static (Version v, string url, string sha, string notes) ParseManifest(string json)
{
    using var doc = JsonDocument.Parse(json);
    var root = doc.RootElement;
    var v = new Version(root.GetProperty("version").GetString()!);
    var url = root.GetProperty("url").GetString() ?? "";
    var sha = root.TryGetProperty("sha256", out var s) ? s.GetString() ?? "" : "";
    var notes = root.TryGetProperty("notes", out var n) ? n.GetString() ?? "" : "";
    return (v, url, sha, notes);
}

static async Task DownloadToAsync(HttpClient http, string url, string dest)
{
    using var resp = await http.GetAsync(url,
        HttpCompletionOption.ResponseHeadersRead);
    resp.EnsureSuccessStatusCode();
    await using var src = await resp.Content.ReadAsStreamAsync();
    await using var dst = File.Create(dest);
    var buffer = new byte[1 << 16];
    long total = 0;
    int read;
    while ((read = await src.ReadAsync(buffer)) > 0)
    {
        await dst.WriteAsync(buffer.AsMemory(0, read));
        total += read;
        if (total % (8 << 20) < read)
            Console.WriteLine($"  ... {total / (1 << 20)} MB");
    }
    Console.WriteLine($"  downloaded {total / (1 << 20)} MB");
}

static string Sha256OfFile(string path)
{
    using var sha = SHA256.Create();
    var hash = sha.ComputeHash(File.OpenRead(path));
    return Convert.ToHexString(hash).ToLowerInvariant();
}

switch (args[0].ToLowerInvariant())
{
    case "check":
    {
        var current = args.Length > 2 ? args[2] : "0.0.0";
        var json = await http.GetStringAsync(args[1]);
        var (latest, _, _, notes) = ParseManifest(json);
        if (latest > new Version(current))
        {
            Console.WriteLine($"update available: {latest} (running {current})");
            if (notes.Length > 0) Console.WriteLine("notes: " + notes);
            return 10;   // convention: 10 = update available
        }
        Console.WriteLine($"up to date ({current})");
        return 0;
    }

    case "install":
    {
        if (args.Length < 3) { Console.Write(Usage); return 2; }
        var installDir = Path.GetFullPath(args[2]);
        var json = await http.GetStringAsync(args[1]);
        var (latest, url, sha256, notes) = ParseManifest(json);
        Console.WriteLine($"latest version: {latest}");
        if (notes.Length > 0) Console.WriteLine($"notes: {notes}");
        if (string.IsNullOrEmpty(url))
        {
            Console.Error.WriteLine("manifest has no download url");
            return 3;
        }

        Directory.CreateDirectory(installDir);
        var tmpZip = Path.Combine(Path.GetTempPath(),
            $"krad-{latest}-{Guid.NewGuid():N}.zip");

        Console.WriteLine($"downloading {url}");
        await DownloadToAsync(http, url, tmpZip);

        if (sha256.Length == 64)
        {
            var actual = Sha256OfFile(tmpZip);
            if (!string.Equals(actual, sha256, StringComparison.OrdinalIgnoreCase))
            {
                File.Delete(tmpZip);
                Console.Error.WriteLine($"sha256 MISMATCH: {actual}");
                return 4;
            }
            Console.WriteLine("sha256 verified");
        }

        // backup current exe/dll files (rollback safety)
        var backup = Path.Combine(installDir, "..", "KradDeviceInfo.bak");
        if (Directory.Exists(backup)) Directory.Delete(backup, true);
        Directory.CreateDirectory(backup);
        foreach (var f in Directory.EnumerateFiles(installDir)
                     .Where(f => f.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)
                              || f.EndsWith(".dll", StringComparison.OrdinalIgnoreCase)))
            File.Move(f, Path.Combine(backup, Path.GetFileName(f)), true);

        Console.WriteLine("extracting...");
        var extractDir = tmpZip + ".x";
        if (Directory.Exists(extractDir)) Directory.Delete(extractDir, true);
        System.IO.Compression.ZipFile.ExtractToDirectory(tmpZip, extractDir);

        // zip may contain a top-level folder - find the dir with the exe
        var inner = Directory.EnumerateFiles(extractDir,
            "kraddeviceinfo.exe", SearchOption.AllDirectories).FirstOrDefault();
        var srcRoot = inner is null ? extractDir : Path.GetDirectoryName(inner)!;

        int copied = 0;
        foreach (var src in Directory.EnumerateFiles(srcRoot, "*", SearchOption.AllDirectories))
        {
            var rel = Path.GetRelativePath(srcRoot, src);
            var dst = Path.Combine(installDir, rel);
            Directory.CreateDirectory(Path.GetDirectoryName(dst)!);
            File.Copy(src, dst, overwrite: true);
            ++copied;
        }
        Directory.Delete(extractDir, true);
        File.Delete(tmpZip);

        await File.WriteAllTextAsync(Path.Combine(installDir, "krad.version"),
            latest.ToString());
        Console.WriteLine($"installed {latest}: {copied} files -> {installDir}");
        Console.WriteLine("backup kept at " + Path.GetFullPath(backup));
        return 0;
    }

    default:
        Console.Write(Usage);
        return 2;
}
