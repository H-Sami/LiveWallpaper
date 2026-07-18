namespace LiveWallpaper.Controller.Models;

public static class SupportedMedia
{
    private static readonly HashSet<string> Extensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".mp4", ".webm", ".mkv", ".mov"
    };

    public const string OpenFileFilter =
        "Video wallpapers (*.mp4;*.webm;*.mkv;*.mov)|*.mp4;*.webm;*.mkv;*.mov|" +
        "MP4 video (*.mp4)|*.mp4|WebM video (*.webm)|*.webm|Matroska video (*.mkv)|*.mkv|QuickTime video (*.mov)|*.mov";

    public static bool IsSupportedExtension(string? extension) =>
        !string.IsNullOrWhiteSpace(extension) && Extensions.Contains(extension);

    public static bool IsSupportedPath(string? path) =>
        !string.IsNullOrWhiteSpace(path) && IsSupportedExtension(Path.GetExtension(path));

    public static string KindForPath(string path) => Path.GetExtension(path).ToUpperInvariant() switch
    {
        ".WEBM" => "WEBM",
        ".MKV" => "MKV",
        ".MOV" => "MOV",
        _ => "MP4"
    };
}
