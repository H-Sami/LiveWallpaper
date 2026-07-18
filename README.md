# LiveWallpaper

A lightweight Windows live wallpaper application for local MP4, WebM, MKV, and MOV video files.

Run `build.bat` from a normal command prompt. It locates Visual Studio 2022 C++
Build Tools, runs the core tests, builds the x64 release with `/W4 /WX`, and runs
the noninteractive media-component smoke check.

Choose a local video through **Add wallpaper** or drag it onto the controller window.
Playback uses Windows Media Foundation with hardware acceleration when available,
audio controls, centered cover scaling, and validated millisecond loop points.
Container and codec availability comes from the Windows Media Foundation components
installed on the machine; unsupported codecs produce an Apply/preview error rather
than silently adding a bundled playback runtime. Preferences are saved under the
current user's local app data.

## Install and use

1. Run `LiveWallpaper-Setup-x64.exe` and complete the setup wizard.
2. Open **LiveWallpaper** from the Start menu.
3. Select **Add wallpaper**, choose a supported local video, then select its card.
4. Adjust the loop segment, audio, and volume as needed, then select **Apply wallpaper**.
5. Closing the controller leaves the lightweight native wallpaper host running.

The optional setup task can register the current wallpaper to start when the user
signs in. The same setting can be changed later from the Settings page.

## Troubleshooting

- A recognized MP4, WebM, MKV, or MOV container can still contain a codec that is not
  available through Windows Media Foundation. Re-encode the file with a Windows-supported
  video codec if Apply reports a decode error.
- If Explorer is restarting or the desktop is unavailable, LiveWallpaper reports that
  the shell is not ready and retries its desktop attachment.
- Preview availability in the controller is not authoritative: the native wallpaper
  host may support a file that WPF cannot preview.
- Use **Stop wallpaper** to stop playback without exiting the controller, or **Exit
  background host** in Settings to stop the host process completely.

## Uninstall

Uninstall **LiveWallpaper** from Windows Settings > Apps. Setup stops the controller
and native host, removes shortcuts and startup registration, and deletes LiveWallpaper's
per-user application data.
