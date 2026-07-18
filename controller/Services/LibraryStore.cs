using System.Text.Json;
using LiveWallpaper.Controller.Models;

namespace LiveWallpaper.Controller.Services;

public static class LibraryStore
{
    private static readonly JsonSerializerOptions Options = new() { WriteIndented = true };

    public static IReadOnlyList<WallpaperItem> Load()
    {
        try
        {
            if (!File.Exists(SettingsStore.LibraryPath)) return [];
            string json = File.ReadAllText(SettingsStore.LibraryPath);
            return JsonSerializer.Deserialize<List<WallpaperItem>>(json, Options) ?? [];
        }
        catch
        {
            return [];
        }
    }

    public static void Save(IEnumerable<WallpaperItem> items)
    {
        Directory.CreateDirectory(SettingsStore.DataDirectory);
        string temporary = SettingsStore.LibraryPath + ".tmp";
        string json = JsonSerializer.Serialize(items, Options);
        File.WriteAllText(temporary, json);
        File.Move(temporary, SettingsStore.LibraryPath, true);
    }
}
