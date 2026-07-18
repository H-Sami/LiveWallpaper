Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$process = Get-Process -Name LiveWallpaper -ErrorAction Stop | Select-Object -First 1
$root = [System.Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
if ($null -eq $root) { throw 'Controller automation root not found' }
$condition = [System.Windows.Automation.Condition]::TrueCondition
$items = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $condition)
$result = foreach ($item in $items) {
    $name = $item.Current.Name
    $type = $item.Current.ControlType.ProgrammaticName
    if (-not [string]::IsNullOrWhiteSpace($name) -or $type -match 'Button|Edit|Slider|CheckBox') {
        [pscustomobject]@{
            Type = $type.Replace('ControlType.', '')
            Name = $name
            Enabled = $item.Current.IsEnabled
            Offscreen = $item.Current.IsOffscreen
            AutomationId = $item.Current.AutomationId
        }
    }
}
$result | ConvertTo-Json -Depth 3
