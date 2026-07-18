using LiveWallpaper.Controller.Models;
using LiveWallpaper.Controller.Services;

static void Check(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

Check(SupportedMedia.IsSupportedExtension(".mp4"), "MP4 should be supported");
Check(SupportedMedia.IsSupportedExtension(".WEBM"), "WEBM should be case-insensitively supported");
Check(SupportedMedia.IsSupportedExtension(".mkv"), "MKV should be supported");
Check(SupportedMedia.IsSupportedExtension(".Mov"), "MOV should be case-insensitively supported");
Check(!SupportedMedia.IsSupportedExtension(".jpg"), "Still images must not be supported");
Check(!SupportedMedia.IsSupportedExtension("mp4"), "Extensions must include the leading period");
Check(SupportedMedia.KindForPath(@"C:\wallpapers\demo.webm") == "WEBM", "WEBM kind should be preserved");
Check(SupportedMedia.KindForPath(@"C:\wallpapers\demo.mkv") == "MKV", "MKV kind should be preserved");
Check(SupportedMedia.KindForPath(@"C:\wallpapers\demo.mov") == "MOV", "MOV kind should be preserved");
Check(SupportedMedia.KindForPath(@"C:\wallpapers\demo.mp4") == "MP4", "MP4 kind should be preserved");

const string payload = "version=1\nrequest_id=request-42\noperation=apply\nstate=error\n" +
    "error_code=codec-unavailable\nmessage=Codec%20unavailable%0AInstall%20a%20codec.\n";
Check(RuntimeStatus.TryParse(payload, out RuntimeStatus? status), "Runtime status should parse");
Check(status!.RequestId == "request-42", "Runtime status request ID should parse");
Check(status.Operation == "apply", "Runtime status operation should parse");
Check(status.State == "error", "Runtime status state should parse");
Check(status.ErrorCode == "codec-unavailable", "Runtime status error code should parse");
Check(status.Message == "Codec unavailable\nInstall a codec.", "Runtime status message should unescape");
Check(status.Matches("request-42", "apply"), "Matching acknowledgement should be accepted");
Check(!status.Matches("stale-request", "apply"), "Stale acknowledgement should be rejected");
Check(RuntimeStatus.PathForRequest(@"C:\state\runtime.status", "request-42") ==
    @"C:\state\runtime.status.request-42", "Each request should use an isolated status path");
Check(!RuntimeStatus.TryParse("version=2\nstate=playing\n", out _), "Unknown status versions should fail");
Console.WriteLine("Controller media format tests passed");
