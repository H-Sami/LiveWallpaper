$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class UiStateNative {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
'@
function Require($condition, [string]$message) { if (-not $condition) { throw $message } }
function ControllerProcess() { Get-Process -Name LiveWallpaper -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1 }
function Find-ById($root, [string]$id) {
  $condition = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $id)
  $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
}
$p = ControllerProcess
Require ($null -ne $p) 'Controller process not found.'
$root = [System.Windows.Automation.AutomationElement]::FromHandle($p.MainWindowHandle)
$settings = Find-ById $root 'SettingsNav'
$settings.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
[UiStateNative]::SetWindowPos($p.MainWindowHandle, [IntPtr]::Zero, 100, 100, 700, 540, 0x0040) | Out-Null
Start-Sleep -Milliseconds 400
$close = Find-ById $root 'CloseButton'
$close.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
$p.WaitForExit(8000) | Out-Null
Require ($p.HasExited) 'Controller did not close.'
$exe = Join-Path $PSScriptRoot '..\controller\bin\Release\net8.0-windows\win-x64\LiveWallpaper.exe'
Start-Process -FilePath $exe
$deadline = [DateTime]::UtcNow.AddSeconds(10)
do { Start-Sleep -Milliseconds 200; $p = ControllerProcess } while ($null -eq $p -and [DateTime]::UtcNow -lt $deadline)
Require ($null -ne $p) 'Controller did not relaunch.'
$root = [System.Windows.Automation.AutomationElement]::FromHandle($p.MainWindowHandle)
$rect = $root.Current.BoundingRectangle
Require ([Math]::Abs($rect.Width - 700) -le 4 -and [Math]::Abs($rect.Height - 540) -le 4) "Window bounds were not restored: $rect"
$settings = Find-ById $root 'SettingsNav'
Require ($settings.Current.ItemStatus -eq 'Current page') "Settings workflow was not restored: $($settings.Current.ItemStatus)"
Write-Output "Controller state restoration passed: rect=$rect page=settings"
