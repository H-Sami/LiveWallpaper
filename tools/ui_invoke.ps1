param(
    [Parameter(Mandatory=$true)][string]$Name,
    [int]$Index = 0
)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$deadline = [DateTime]::UtcNow.AddSeconds(10)
do {
    $process = Get-Process -Name LiveWallpaper -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if ($null -eq $process) { Start-Sleep -Milliseconds 150 }
} while ($null -eq $process -and [DateTime]::UtcNow -lt $deadline)
if ($null -eq $process) { throw 'LiveWallpaper controller process not found' }
$root = [System.Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
$condition = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $Name)
$matches = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $condition)
if ($matches.Count -le $Index) { throw "Automation element '$Name' at index $Index not found" }
$element = $matches.Item($Index)
$pattern = $element.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
$pattern.Invoke()
Write-Output "Invoked $Name"
