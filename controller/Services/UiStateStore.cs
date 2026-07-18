using System.Text.Json;

namespace LiveWallpaper.Controller.Services;

public sealed record ControllerUiState(
    double Left,
    double Top,
    double Width,
    double Height,
    bool Maximized,
    string Page,
    string? SelectedPath,
    string SearchText);

public static class UiStateStore
{
    private const int MaximumFileBytes = 64 * 1024;
    private static readonly string StatePath = Path.Combine(SettingsStore.DataDirectory, "controller-state.json");

    public static ControllerUiState? Load()
    {
        try
        {
            var info = new FileInfo(StatePath);
            if (!info.Exists || info.Length <= 0 || info.Length > MaximumFileBytes) return null;
            return JsonSerializer.Deserialize<ControllerUiState>(File.ReadAllText(StatePath));
        }
        catch
        {
            return null;
        }
    }

    public static void Save(ControllerUiState state)
    {
        Directory.CreateDirectory(SettingsStore.DataDirectory);
        string temporary = StatePath + ".tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(state, new JsonSerializerOptions { WriteIndented = true }));
        File.Move(temporary, StatePath, true);
    }
}
