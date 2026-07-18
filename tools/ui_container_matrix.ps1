$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
function Require($condition, [string]$message) { if (-not $condition) { throw $message } }
function ControllerProcess() { Get-Process -Name LiveWallpaper -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1 }
function Root($p) { [System.Windows.Automation.AutomationElement]::FromHandle($p.MainWindowHandle) }
function Find-ById($root, [string]$id) {
  $c = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $id)
  $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $c)
}
function Find-ByName($root, [string]$name) {
  $c = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $name)
  $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $c)
}
function Invoke($element) { $element.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }

$repo = Split-Path $PSScriptRoot -Parent
$data = Join-Path $env:LOCALAPPDATA 'LiveWallpaper'
$library = Join-Path $data 'library.json'
$backup = if (Test-Path $library) { [IO.File]::ReadAllBytes($library) } else { $null }
$cases = @(
  @{ File='test-1080p.mp4'; Kind='MP4' },
  @{ File='test-vp9.webm'; Kind='WEBM' },
  @{ File='test-h264.mkv'; Kind='MKV' },
  @{ File='test-h264.mov'; Kind='MOV' }
)
try {
  $p = ControllerProcess
  if ($null -ne $p) { $root = Root $p; Invoke (Find-ById $root 'CloseButton'); $p.WaitForExit(8000) | Out-Null }
  # Isolate the development matrix from a host left running by an installed
  # build. Both builds intentionally share the product-wide singleton name.
  Get-Process -Name 'LiveWallpaper.Host' -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 300
  $items = @()
  foreach ($case in $cases) {
    $items += @{ Id=[guid]::NewGuid().ToString('N'); Path=(Join-Path $repo "tests\fixtures\$($case.File)"); AddedUtc=[DateTime]::UtcNow.ToString('o') }
  }
  $json = $items | ConvertTo-Json
  [IO.File]::WriteAllText($library, $json, (New-Object Text.UTF8Encoding($false)))
  $exe = Join-Path $repo 'controller\bin\Release\net8.0-windows\win-x64\LiveWallpaper.exe'
  Start-Process $exe
  $deadline = [DateTime]::UtcNow.AddSeconds(10)
  do { Start-Sleep -Milliseconds 200; $p = ControllerProcess } while ($null -eq $p -and [DateTime]::UtcNow -lt $deadline)
  Require ($null -ne $p) 'Controller did not start.'

  foreach ($case in $cases) {
    $root = Root $p
    $libraryNav = Find-ById $root 'LibraryNav'
    Invoke $libraryNav
    Start-Sleep -Milliseconds 250
    $base = [IO.Path]::GetFileNameWithoutExtension($case.File)
    $cardName = "Edit $base, $($case.Kind)"
    $buttonCondition = New-Object System.Windows.Automation.PropertyCondition(
      [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
      [System.Windows.Automation.ControlType]::Button)
    $card = @($root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $buttonCondition) |
      Where-Object { $_.Current.Name.StartsWith($cardName, [StringComparison]::Ordinal) }) | Select-Object -First 1
    if ($null -eq $card) {
      $names = @($root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $buttonCondition) | ForEach-Object { $_.Current.Name })
      throw "Library card missing for $($case.File); buttons=$($names -join '|')"
    }
    Invoke $card
    $deadline = [DateTime]::UtcNow.AddSeconds(13)
    do {
      Start-Sleep -Milliseconds 250
      $root = Root $p
      $apply = Find-ById $root 'ApplyButton'
      $end = Find-ById $root 'EndTimeBox'
      $endValue = if ($null -ne $end -and $end.Current.IsEnabled) { $end.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value } else { 'Full video' }
    } while (($null -eq $apply -or $null -eq $end -or !$end.Current.IsEnabled -or $endValue -match 'Loading') -and [DateTime]::UtcNow -lt $deadline)
    Require ($null -ne $apply -and $apply.Current.IsEnabled) "Apply unavailable for $($case.File)"
    Invoke $apply
    Start-Sleep -Seconds 3
    $settings = Get-Content (Join-Path $data 'settings.ini') -Raw
    if ($settings -notmatch [regex]::Escape($case.File)) {
      $root = Root $p
      $toast = Find-ById $root 'ToastText'
      $toastMessage = if ($null -ne $toast) { $toast.Current.Name } else { '<unavailable>' }
      $runtimePath = Join-Path $data 'runtime.status'
      $runtime = if (Test-Path $runtimePath) { (Get-Content $runtimePath -Raw) -replace "`r?`n", '; ' } else { '<missing>' }
      throw "Controller did not apply $($case.File); toast=$toastMessage; runtime=$runtime"
    }
    Write-Output "Controller container passed: $($case.File) preview=$endValue"
  }
} finally {
  $p = ControllerProcess
  if ($null -ne $p) { try { Invoke (Find-ById (Root $p) 'CloseButton'); $p.WaitForExit(5000) | Out-Null } catch {} }
  if ($null -eq $backup) { Remove-Item $library -ErrorAction SilentlyContinue } else { [IO.File]::WriteAllBytes($library, $backup) }
}
Write-Output 'Controller container matrix passed'
