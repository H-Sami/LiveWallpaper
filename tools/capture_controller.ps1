param(
  [string]$Output = (Join-Path (Split-Path $PSScriptRoot -Parent) 'artifacts\ui-redesign-library.png'),
  [ValidateSet('none','minimize','maximize','close')][string]$Hover = 'none'
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class UiCaptureNative {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
}
'@
$deadline = (Get-Date).AddSeconds(15)
do {
  $process = Get-Process -Name 'LiveWallpaper' -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  if (-not $process) { Start-Sleep -Milliseconds 150 }
} while (-not $process -and (Get-Date) -lt $deadline)
if (-not $process) { throw 'Controller window was not found.' }
$handle = $process.MainWindowHandle
[UiCaptureNative]::SetForegroundWindow($handle) | Out-Null
[UiCaptureNative+RECT]$rect = New-Object UiCaptureNative+RECT
[UiCaptureNative]::GetWindowRect($handle, [ref]$rect) | Out-Null
$offset = switch ($Hover) { 'minimize' { 115 } 'maximize' { 69 } 'close' { 23 } default { -1 } }
if ($offset -ge 0) {
  [UiCaptureNative]::SetCursorPos($rect.Right - $offset, $rect.Top + 24) | Out-Null
  Start-Sleep -Milliseconds 700
}
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
try { $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size) }
finally { $graphics.Dispose() }
$directory = Split-Path $Output -Parent
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$bitmap.Save($Output, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
Write-Output "Captured $Output (${width}x${height}) hover=$Hover"
