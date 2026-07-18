using System.Diagnostics;
using Microsoft.Win32;

namespace LiveWallpaper.Controller.Services;

public static class HostController
{
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string RunValue = "LiveWallpaper";
    private static string RuntimeStatusPath => Path.Combine(SettingsStore.DataDirectory, "runtime.status");

    public static string HostPath
    {
        get
        {
            string installed = Path.Combine(AppContext.BaseDirectory, "LiveWallpaper.Host.exe");
            if (File.Exists(installed)) return installed;
            return Path.GetFullPath(Path.Combine(AppContext.BaseDirectory,
                "..", "..", "..", "..", "build", "LiveWallpaper.Host.exe"));
        }
    }

    public static bool IsRunning
    {
        get
        {
            try { return Process.GetProcessesByName("LiveWallpaper.Host").Length > 0; }
            catch { return false; }
        }
    }

    public static bool Send(string command, out string error)
    {
        error = string.Empty;
        if (!File.Exists(HostPath))
        {
            error = "The native wallpaper host is missing. Reinstall LiveWallpaper.";
            return false;
        }

        string? expectedState = command switch
        {
            "--apply" => "playing",
            "--stop" => "stopped",
            _ => null
        };
        string operation = command.TrimStart('-');
        string requestId = expectedState is null ? string.Empty : Guid.NewGuid().ToString("N");
        string arguments = expectedState is null ? command : $"{command}|{requestId}";
        string requestStatusPath = RuntimeStatus.PathForRequest(RuntimeStatusPath, requestId);

        try
        {
            if (expectedState is not null)
            {
                Directory.CreateDirectory(SettingsStore.DataDirectory);
                File.Delete(requestStatusPath);
            }

            using Process? process = Process.Start(new ProcessStartInfo
            {
                FileName = HostPath,
                Arguments = arguments,
                UseShellExecute = false,
                CreateNoWindow = true,
                WorkingDirectory = AppContext.BaseDirectory
            });
            if (process is null)
            {
                error = "Windows could not start the wallpaper host.";
                return false;
            }

            if (expectedState is null)
            {
                if (!process.WaitForExit(7000))
                {
                    error = "The wallpaper host did not acknowledge the command in time.";
                    return false;
                }
                if (process.ExitCode != 0)
                {
                    error = $"The wallpaper host rejected the command (code {process.ExitCode}).";
                    return false;
                }
                return true;
            }

            DateTime deadline = DateTime.UtcNow.AddSeconds(10);
            int? relayExitCode = null;
            while (DateTime.UtcNow < deadline)
            {
                if (File.Exists(requestStatusPath))
                {
                    string payload = File.ReadAllText(requestStatusPath);
                    if (!RuntimeStatus.TryParse(payload, out RuntimeStatus? status) ||
                        status is null || !status.Matches(requestId, operation))
                    {
                        Thread.Sleep(75);
                        continue;
                    }
                    if (string.Equals(status.State, expectedState, StringComparison.OrdinalIgnoreCase)) return true;
                    if (status.State == "pending-shell")
                    {
                        error = string.IsNullOrWhiteSpace(status.Message)
                            ? "Windows Explorer is not ready to host the wallpaper yet. LiveWallpaper will retry automatically."
                            : status.Message;
                        return false;
                    }
                    if (status.State == "error")
                    {
                        error = !string.IsNullOrWhiteSpace(status.Message)
                            ? status.Message
                            : command == "--apply"
                                ? "Windows could not decode or display this wallpaper."
                                : "The wallpaper host could not stop playback.";
                        return false;
                    }
                    if (status.State == "superseded")
                    {
                        error = string.IsNullOrWhiteSpace(status.Message)
                            ? "A newer wallpaper request replaced this request."
                            : status.Message;
                        return false;
                    }
                }
                if (process.HasExited && process.ExitCode != 0)
                {
                    // The relay uses SendMessageTimeout. A media open can finish
                    // successfully just after that transport timeout, so the
                    // correlated status file—not the helper's early exit—is the
                    // authority for Apply/Stop completion.
                    relayExitCode = process.ExitCode;
                }
                Thread.Sleep(75);
            }
            error = relayExitCode is int exitCode
                ? $"The wallpaper host did not confirm the requested state in time (relay code {exitCode})."
                : "The wallpaper host did not confirm the requested state in time.";
            return false;
        }
        catch (Exception exception)
        {
            error = exception.Message;
            return false;
        }
        finally
        {
            if (expectedState is not null)
            {
                try { File.Delete(requestStatusPath); }
                catch { }
            }
        }
    }

    public static bool StartsWithWindows
    {
        get
        {
            try
            {
                using RegistryKey? key = Registry.CurrentUser.OpenSubKey(RunKey);
                string expected = $"\"{HostPath}\" --apply";
                return string.Equals(key?.GetValue(RunValue) as string, expected, StringComparison.OrdinalIgnoreCase);
            }
            catch { return false; }
        }
        set
        {
            using RegistryKey key = Registry.CurrentUser.CreateSubKey(RunKey, true);
            if (value)
                key.SetValue(RunValue, $"\"{HostPath}\" --apply", RegistryValueKind.String);
            else
                key.DeleteValue(RunValue, false);
        }
    }
}
