param([string]$ExpectedTitle = 'test-audio')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

function Require($condition, [string]$message) {
    if (-not $condition) { throw $message }
}

$process = Get-Process -Name LiveWallpaper | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
Require ($null -ne $process) 'Controller process with a visible window was not found.'
$root = [System.Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)

function Find-ById([string]$id) {
    $condition = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $id)
    return $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
}

$title = Find-ById 'EditorTitle'
$start = Find-ById 'StartTimeBox'
$end = Find-ById 'EndTimeBox'
$apply = Find-ById 'ApplyButton'
Require ($null -ne $title) 'Editor title is missing.'
Require ($title.Current.Name -eq $ExpectedTitle) "Wrong editor item: $($title.Current.Name)"
Require ($null -ne $start -and $null -ne $end) 'Trim time fields are missing.'
$startValue = $start.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value
$endValue = $end.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value
Require ($startValue -match '^\d\d:\d\d\.\d\d\d$') "Unexpected start time: $startValue"
Require ($endValue -notmatch 'Loading|Not available|^00:00.000$') "Video duration did not load: $endValue"
Require ($null -ne $apply -and $apply.Current.IsEnabled) 'Apply wallpaper button is missing or disabled.'
$apply.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
Start-Sleep -Seconds 3
$hostProcess = Get-Process -Name 'LiveWallpaper.Host' -ErrorAction SilentlyContinue | Select-Object -First 1
Require ($null -ne $hostProcess) 'Native wallpaper host did not start after Apply.'
Write-Output "Controller editor smoke test passed: start=$startValue end=$endValue hostPid=$($hostProcess.Id)"
