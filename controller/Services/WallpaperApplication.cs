namespace LiveWallpaper.Controller.Services;

public static class WallpaperApplication
{
    public static bool Apply(WallpaperSettings requestedSettings, out string error)
    {
        WallpaperSettings? previousSettings = SettingsStore.Load();
        try
        {
            SettingsStore.Save(requestedSettings);
        }
        catch (Exception exception)
        {
            error = $"Could not save wallpaper settings: {exception.Message}";
            return false;
        }

        if (HostController.Send("--apply", out error)) return true;

        string applyError = error;
        WallpaperSettings rollback = previousSettings ?? requestedSettings with { Playing = false };
        try
        {
            SettingsStore.Save(rollback);
        }
        catch (Exception exception)
        {
            error = $"{applyError} Previous settings could not be restored: {exception.Message}";
            return false;
        }

        if (previousSettings?.Playing == true &&
            !HostController.Send("--apply", out string recoveryError))
        {
            error = $"{applyError} The previous wallpaper could not be restarted: {recoveryError}";
            return false;
        }

        error = applyError;
        return false;
    }
}
