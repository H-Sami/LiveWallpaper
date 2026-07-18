using System.Text;

namespace LiveWallpaper.Controller.Services;

public sealed record WallpaperSettings(
    string MediaPath,
    bool Muted,
    int Volume,
    long StartMs,
    long EndMs,
    bool Playing);

public static class SettingsStore
{
    public static string DataDirectory { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "LiveWallpaper");
    public static string SettingsPath => Path.Combine(DataDirectory, "settings.ini");
    public static string LibraryPath => Path.Combine(DataDirectory, "library.json");

    public static WallpaperSettings? Load()
    {
        try
        {
            if (!File.Exists(SettingsPath)) return null;
            var values = File.ReadLines(SettingsPath)
                .Select(line => line.Split('=', 2))
                .Where(parts => parts.Length == 2)
                .ToDictionary(parts => parts[0], parts => Unescape(parts[1]), StringComparer.OrdinalIgnoreCase);
            values.TryGetValue("media", out string? media);
            int.TryParse(values.GetValueOrDefault("volume"), out int volume);
            long.TryParse(values.GetValueOrDefault("start_ms"), out long startMs);
            long.TryParse(values.GetValueOrDefault("end_ms"), out long endMs);
            return new WallpaperSettings(
                media ?? string.Empty,
                values.GetValueOrDefault("muted") == "1",
                Math.Clamp(volume, 0, 100),
                Math.Max(0, startMs),
                Math.Max(0, endMs),
                values.GetValueOrDefault("playing") == "1");
        }
        catch
        {
            return null;
        }
    }

    public static void Save(WallpaperSettings settings)
    {
        Directory.CreateDirectory(DataDirectory);
        string content = string.Join('\n',
            "version=1",
            $"media={EscapeUtf8(settings.MediaPath)}",
            $"muted={(settings.Muted ? 1 : 0)}",
            $"volume={Math.Clamp(settings.Volume, 0, 100)}",
            $"start_ms={Math.Max(0, settings.StartMs)}",
            $"end_ms={Math.Max(0, settings.EndMs)}",
            "close_to_tray=1",
            $"playing={(settings.Playing ? 1 : 0)}",
            string.Empty);
        string temporary = SettingsPath + ".controller.tmp";
        File.WriteAllText(temporary, content, new UTF8Encoding(false));
        File.Move(temporary, SettingsPath, true);
    }

    private static string EscapeUtf8(string value)
    {
        var output = new StringBuilder(value.Length);
        foreach (char character in value)
        {
            output.Append(character switch
            {
                '%' => "%25",
                '\n' => "%0A",
                '\r' => "%0D",
                '=' => "%3D",
                _ => character.ToString()
            });
        }
        return output.ToString();
    }

    private static string Unescape(string value)
    {
        var output = new StringBuilder(value.Length);
        for (int index = 0; index < value.Length; index++)
        {
            if (value[index] == '%' && index + 2 < value.Length &&
                byte.TryParse(value.AsSpan(index + 1, 2), System.Globalization.NumberStyles.HexNumber,
                    System.Globalization.CultureInfo.InvariantCulture, out byte decoded))
            {
                output.Append((char)decoded);
                index += 2;
            }
            else output.Append(value[index]);
        }
        return output.ToString();
    }
}
