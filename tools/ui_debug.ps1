Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$p = Get-Process -Name LiveWallpaper | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
$r = [System.Windows.Automation.AutomationElement]::FromHandle($p.MainWindowHandle)
foreach ($id in @('EditorTitle','EditorSubtitle','StartTimeBox','EndTimeBox','ToastText','PreviewPlaceholder','ApplyButton','FilePathText')) {
  $c = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty,$id)
  $e = $r.FindFirst([System.Windows.Automation.TreeScope]::Descendants,$c)
  if ($null -eq $e) { Write-Output "$id=<missing>"; continue }
  $v = ''
  if ($e.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern,[ref]$pattern)) { $v = $pattern.Current.Value }
  Write-Output "$id name=[$($e.Current.Name)] value=[$v] enabled=$($e.Current.IsEnabled) offscreen=$($e.Current.IsOffscreen)"
}
