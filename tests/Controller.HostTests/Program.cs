using System.Diagnostics;
using LiveWallpaper.Controller.Services;

static void Check(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

string fixture = Path.GetFullPath(args.Single());
byte[]? original = File.Exists(SettingsStore.SettingsPath) ? File.ReadAllBytes(SettingsStore.SettingsPath) : null;
try
{
    Process.Start(new ProcessStartInfo(HostController.HostPath, "--exit") { UseShellExecute = false })?.WaitForExit(8000);

    var validSettings = new WallpaperSettings(fixture, true, 0, 0, 0, true);
    SettingsStore.Save(validSettings);
    File.WriteAllText(Path.Combine(SettingsStore.DataDirectory, "runtime.status"),
        "version=1\nrequest_id=stale\noperation=apply\nstate=playing\nerror_code=\nmessage=\n");
    Check(HostController.Send("--apply", out string applyError), $"First Apply failed: {applyError}");

    string missing = Path.Combine(Path.GetDirectoryName(fixture)!, "missing-video.mp4");
    Check(!WallpaperApplication.Apply(
        new WallpaperSettings(missing, true, 0, 0, 0, true), out string invalidError),
        "Missing media Apply unexpectedly succeeded");
    Check(invalidError.Contains("could not be found", StringComparison.OrdinalIgnoreCase),
        $"Missing media returned the wrong error: {invalidError}");
    Check(SettingsStore.Load() == validSettings, "Failed Apply did not restore the prior settings");
    string latestStatus = File.ReadAllText(Path.Combine(SettingsStore.DataDirectory, "runtime.status"));
    Check(RuntimeStatus.TryParse(latestStatus, out RuntimeStatus? recovered) && recovered?.State == "playing",
        "Failed Apply did not restart the last confirmed wallpaper");

    Check(HostController.Send("--stop", out string stopError), $"Relayed Stop failed: {stopError}");
    Process.Start(new ProcessStartInfo(HostController.HostPath, "--exit") { UseShellExecute = false })?.WaitForExit(8000);
    Check(HostController.Send("--stop", out string noHostStopError), $"No-host Stop failed: {noHostStopError}");

    SettingsStore.Save(new WallpaperSettings(fixture, true, 0, 0, 0, true));
    using var gate = new ManualResetEventSlim(false);
    Task<(bool Success, string Error)> SendApply() => Task.Run(() =>
    {
        gate.Wait();
        bool success = HostController.Send("--apply", out string error);
        return (success, error);
    });
    Task<(bool Success, string Error)> first = SendApply();
    Task<(bool Success, string Error)> second = SendApply();
    gate.Set();
    (bool Success, string Error)[] results = await Task.WhenAll(first, second);
    Check(results.Any(result => result.Success), "Neither concurrent Apply request succeeded");
    foreach ((bool success, string error) in results)
    {
        Check(success || error.Contains("newer Apply request", StringComparison.OrdinalIgnoreCase),
            $"Concurrent Apply failed without a correlated result: {error}");
        Check(!error.Contains("did not confirm", StringComparison.OrdinalIgnoreCase),
            $"Concurrent Apply timed out instead of receiving a correlated result: {error}");
    }

    Console.WriteLine("Controller/host acknowledgement tests passed");
}
finally
{
    Process.Start(new ProcessStartInfo(HostController.HostPath, "--exit") { UseShellExecute = false })?.WaitForExit(8000);
    if (original is null) File.Delete(SettingsStore.SettingsPath);
    else File.WriteAllBytes(SettingsStore.SettingsPath, original);
}
