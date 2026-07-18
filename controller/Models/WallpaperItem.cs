using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text.Json.Serialization;

namespace LiveWallpaper.Controller.Models;

public sealed class WallpaperItem : INotifyPropertyChanged
{
    private bool _isSelected;
    private bool _isPlaying;


    public string Id { get; set; } = Guid.NewGuid().ToString("N");
    public string Path { get; set; } = string.Empty;
    public DateTime AddedUtc { get; set; } = DateTime.UtcNow;

    [JsonIgnore] public string Name => System.IO.Path.GetFileNameWithoutExtension(Path);
    [JsonIgnore] public string Kind => SupportedMedia.KindForPath(Path);
    [JsonIgnore] public string FileName => System.IO.Path.GetFileName(Path);
    [JsonIgnore] public bool IsVideo => SupportedMedia.IsSupportedPath(Path);
    [JsonIgnore] public bool Exists => File.Exists(Path);
    [JsonIgnore] public string Meta => Exists ? $"{Kind}  •  {FormatBytes(new FileInfo(Path).Length)}" : $"{Kind}  •  File missing";
    [JsonIgnore] public string AccessibleName =>
        $"Edit {Name}, {Kind}{(IsPlaying ? ", currently playing" : string.Empty)}{(Exists ? string.Empty : ", file missing")}";

    [JsonIgnore]
    public bool IsSelected
    {
        get => _isSelected;
        set { if (_isSelected != value) { _isSelected = value; OnPropertyChanged(); } }
    }

    [JsonIgnore]
    public bool IsPlaying
    {
        get => _isPlaying;
        set { if (_isPlaying != value) { _isPlaying = value; OnPropertyChanged(); OnPropertyChanged(nameof(AccessibleName)); } }
    }


    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

    private static string FormatBytes(long bytes)
    {
        string[] units = ["B", "KB", "MB", "GB"];
        double value = bytes;
        int unit = 0;
        while (value >= 1024 && unit < units.Length - 1) { value /= 1024; unit++; }
        return $"{value:0.#} {units[unit]}";
    }
}
