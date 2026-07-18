$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
function Require($condition, [string]$message) { if (-not $condition) { throw $message } }
function Run-Wait([string]$file, [string]$arguments, [int]$timeoutSeconds = 120) {
  $p = Start-Process -FilePath $file -ArgumentList $arguments -PassThru
  Require ($p.WaitForExit($timeoutSeconds * 1000)) "Process timed out: $file"
  Require ($p.ExitCode -eq 0) "Process failed ($($p.ExitCode)): $file $arguments"
}
function Stop-Products {
  Get-Process -Name LiveWallpaper -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
  $hostProcess = Get-Process -Name 'LiveWallpaper.Host' -ErrorAction SilentlyContinue
  if ($null -ne $hostProcess) {
    $candidate = Join-Path $env:LOCALAPPDATA 'Programs\LiveWallpaper\LiveWallpaper.Host.exe'
    if (Test-Path $candidate) { try { Run-Wait $candidate '--exit' 15 } catch {} }
    Get-Process -Name 'LiveWallpaper.Host' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
  }
}
function ControllerProcess {
  Get-Process -Name LiveWallpaper -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
}
function Root($p) { [System.Windows.Automation.AutomationElement]::FromHandle($p.MainWindowHandle) }
function Find-ByName($root, [string]$name) {
  $c = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $name)
  $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $c)
}
function Invoke($element) { $element.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }

$repo = Split-Path $PSScriptRoot -Parent
$setup = Join-Path $repo 'dist\LiveWallpaper-Setup-x64.exe'
$app = Join-Path $env:LOCALAPPDATA 'Programs\LiveWallpaper'
$data = Join-Path $env:LOCALAPPDATA 'LiveWallpaper'
$backup = Join-Path $env:LOCALAPPDATA 'LiveWallpaper.qa-backup'
$startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\LiveWallpaper\LiveWallpaper.lnk'
$desktop = Join-Path ([Environment]::GetFolderPath('Desktop')) 'LiveWallpaper.lnk'
$log = Join-Path $repo 'artifacts\installer-qa.log'
Stop-Products
if (Test-Path $backup) { Remove-Item $backup -Recurse -Force }
if (Test-Path $data) { Move-Item $data $backup }
try {
  Run-Wait $setup "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /TASKS=`"startup,desktopicon`" /LOG=`"$log`"" 180
  Require (Test-Path (Join-Path $app 'LiveWallpaper.exe')) 'Installed controller is missing.'
  Require (Test-Path (Join-Path $app 'LiveWallpaper.Host.exe')) 'Installed host is missing.'
  Require (Test-Path $startMenu) 'Start Menu shortcut is missing.'
  Require (Test-Path $desktop) 'Desktop shortcut is missing.'
  $expectedIcon = Join-Path $app 'Assets\LiveWallpaper.ico'
  Require (Test-Path $expectedIcon) 'Installed shortcut icon is missing.'
  $shell = New-Object -ComObject WScript.Shell
  foreach ($shortcutPath in @($startMenu, $desktop)) {
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $iconPath = ($shortcut.IconLocation -split ',')[0].Trim()
    Require ([string]::Equals($iconPath, $expectedIcon, [StringComparison]::OrdinalIgnoreCase)) "Shortcut uses the wrong icon: $shortcutPath -> $($shortcut.IconLocation)"
  }
  $sourceIcon = Join-Path $repo 'controller\Assets\LiveWallpaper.ico'
  Require ((Get-FileHash $expectedIcon -Algorithm SHA256).Hash -eq (Get-FileHash $sourceIcon -Algorithm SHA256).Hash) 'Installed shortcut icon does not match the supplied logo asset.'
  $runValue = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name LiveWallpaper -ErrorAction Stop).LiveWallpaper
  Require ($runValue -eq "`"$app\LiveWallpaper.Host.exe`" --apply") "Startup command is incorrect: $runValue"

  New-Item -ItemType Directory -Path $data -Force | Out-Null
  $fixture = Join-Path $repo 'tests\fixtures\test-audio.mp4'
  $library = ConvertTo-Json -InputObject @(@{ Id='installerqa'; Path=$fixture; AddedUtc=[DateTime]::UtcNow.ToString('o') })
  [IO.File]::WriteAllText((Join-Path $data 'library.json'), $library, (New-Object Text.UTF8Encoding($false)))
  Start-Process (Join-Path $app 'LiveWallpaper.exe')
  $deadline = [DateTime]::UtcNow.AddSeconds(12)
  do { Start-Sleep -Milliseconds 250; $p = ControllerProcess } while ($null -eq $p -and [DateTime]::UtcNow -lt $deadline)
  Require ($null -ne $p) 'Installed controller did not launch.'
  $root = Root $p
  $card = Find-ByName $root 'Edit test-audio, MP4'
  Require ($null -ne $card) 'Installed controller did not load its library.'
  Invoke $card
  Start-Sleep -Seconds 2
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'tools\ui_smoke.ps1') | Write-Output
  Require ($LASTEXITCODE -eq 0) 'Installed playback smoke test failed.'

  [IO.File]::WriteAllText((Join-Path $data 'upgrade-marker.txt'), 'retain-me')
  Run-Wait $setup "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /TASKS=`"startup,desktopicon`" /LOG=`"$repo\artifacts\upgrade-qa.log`"" 180
  Start-Sleep -Seconds 2
  Require ($null -eq (Get-Process -Name LiveWallpaper -ErrorAction SilentlyContinue)) 'Upgrade did not close the controller.'
  Require ($null -eq (Get-Process -Name 'LiveWallpaper.Host' -ErrorAction SilentlyContinue)) 'Upgrade did not close the host.'
  Require ((Get-Content (Join-Path $data 'upgrade-marker.txt') -Raw) -eq 'retain-me') 'Upgrade did not retain user data.'
  Require ((Get-Content (Join-Path $data 'settings.ini') -Raw) -match 'test-audio.mp4') 'Upgrade did not retain wallpaper settings.'

  $uninstaller = Join-Path $app 'unins000.exe'
  Require (Test-Path $uninstaller) 'Uninstaller is missing.'
  Run-Wait $uninstaller "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=`"$repo\artifacts\uninstall-qa.log`"" 180
  Start-Sleep -Seconds 2
  Require (-not (Test-Path $app)) 'Install directory remains after uninstall.'
  Require (-not (Test-Path $startMenu)) 'Start Menu shortcut remains after uninstall.'
  Require (-not (Test-Path $desktop)) 'Desktop shortcut remains after uninstall.'
  Require (-not (Test-Path $data)) 'Application data remains after uninstall.'
  $runValue = Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name LiveWallpaper -ErrorAction SilentlyContinue
  Require ($null -eq $runValue) 'Startup registry value remains after uninstall.'
  Write-Output 'Installer QA passed: silent install, shortcuts, startup, installed playback, upgrade retention, process shutdown, and uninstall cleanup.'
} finally {
  Stop-Products
  if (Test-Path $data) { Remove-Item $data -Recurse -Force }
  if (Test-Path $backup) { Move-Item $backup $data }
}
