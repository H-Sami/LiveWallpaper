$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class NativeWindow {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
'@

function Require($condition, [string]$message) { if (-not $condition) { throw $message } }
$deadline = [DateTime]::UtcNow.AddSeconds(10)
do {
  $process = Get-Process -Name LiveWallpaper -ErrorAction SilentlyContinue |
    Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  if ($null -eq $process) { Start-Sleep -Milliseconds 150 }
} while ($null -eq $process -and [DateTime]::UtcNow -lt $deadline)
Require ($null -ne $process) 'Controller process not found.'
[NativeWindow]::SetWindowPos($process.MainWindowHandle, [IntPtr]::Zero, 80, 80, 640, 500, 0x0040) | Out-Null
Start-Sleep -Milliseconds 700
$root = [System.Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)

function Find-ById([string]$id) {
  $condition = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $id)
  return $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
}

$editorNav = Find-ById 'EditorNav'
Require ($null -ne $editorNav -and $editorNav.Current.IsEnabled) 'Editor navigation is unavailable.'
$editorNav.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
Start-Sleep -Milliseconds 600
$root = [System.Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)

$focusableCondition = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::IsKeyboardFocusableProperty, $true)
$focusable = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $focusableCondition)
$unnamed = @()
foreach ($element in $focusable) {
  if ([string]::IsNullOrWhiteSpace($element.Current.Name)) {
    $unnamed += "$($element.Current.AutomationId):$($element.Current.ClassName):$($element.Current.ControlType.ProgrammaticName)"
  }
}
Require ($unnamed.Count -eq 0) "Focusable controls without accessible names: $($unnamed -join ', ')"

$startThumb = Find-ById 'StartThumb'
$endThumb = Find-ById 'EndThumb'
Require ($null -ne $startThumb -and $null -ne $endThumb) 'Range thumbs are missing from UI Automation.'
foreach ($thumb in @($startThumb, $endThumb)) {
  $range = $null
  Require ($thumb.TryGetCurrentPattern([System.Windows.Automation.RangeValuePattern]::Pattern, [ref]$range)) "RangeValuePattern missing: $($thumb.Current.AutomationId)"
  Require ($range.Current.Maximum -gt $range.Current.Minimum) "Invalid range: $($thumb.Current.AutomationId)"
}

$optionsScroller = Find-ById 'OptionsScroller'
$scroll = $null
Require ($null -ne $optionsScroller -and $optionsScroller.TryGetCurrentPattern(
  [System.Windows.Automation.ScrollPattern]::Pattern, [ref]$scroll)) 'Options scrolling is missing from UI Automation.'
Require ($scroll.Current.VerticallyScrollable) 'Options must remain vertically scrollable at 640x500.'
$initialScroll = $scroll.Current.VerticalScrollPercent
$scroll.Scroll([System.Windows.Automation.ScrollAmount]::NoAmount,
  [System.Windows.Automation.ScrollAmount]::LargeIncrement)
Start-Sleep -Milliseconds 200
Require ($scroll.Current.VerticalScrollPercent -gt $initialScroll) 'UI Automation could not scroll hidden-scrollbar content.'
$scroll.SetScrollPercent([System.Windows.Automation.ScrollPattern]::NoScroll, $initialScroll)

$rootRect = $root.Current.BoundingRectangle
foreach ($id in @('PreviewPlayButton','StartTimeBox','EndTimeBox','ApplyButton','StopButton')) {
  $element = Find-ById $id
  Require ($null -ne $element) "Required editor control is missing: $id"
  $scrollItem = $null
  if ($element.TryGetCurrentPattern([System.Windows.Automation.ScrollItemPattern]::Pattern, [ref]$scrollItem)) {
    $scrollItem.ScrollIntoView()
    Start-Sleep -Milliseconds 150
  }
  $element.SetFocus()
  Start-Sleep -Milliseconds 150
  $rect = $element.Current.BoundingRectangle
  Require (-not $element.Current.IsOffscreen) "Required editor control is offscreen at 640x500: $id"
  Require ($rect.Left -ge $rootRect.Left - 2 -and $rect.Right -le $rootRect.Right + 2 -and $rect.Bottom -le $rootRect.Bottom + 2) "Required editor control is clipped: $id rect=$rect root=$rootRect"
}

$editor = Find-ById 'EditorNav'
Require ($editor.Current.ItemStatus -eq 'Current page') 'Editor navigation does not expose its selected state.'
Write-Output "Controller accessibility QA passed: focusable=$($focusable.Count), range-patterns=2, viewport=640x500"
