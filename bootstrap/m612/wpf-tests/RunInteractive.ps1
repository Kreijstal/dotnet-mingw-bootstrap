param(
    [Parameter(Mandatory=$true)][string]$Work,
    [Parameter(Mandatory=$true)][string]$Dotnet,
    [Parameter(Mandatory=$true)][string]$ToolchainBin,
    [string]$Debugger,
    [string]$DebuggerCommands,
    [switch]$Text
)
$ErrorActionPreference='Stop'
$taskName='DotnetBootstrap-WpfWindowCheck-'+[Guid]::NewGuid().ToString('N')
$log=Join-Path $Work $(if($Text){'interactive-text.log'}else{'interactive.log'})
$dll=Join-Path $Work 'WindowCheck.dll'
$launch='"'+$Dotnet+'" "'+$dll+'"'
if($Debugger) { $launch='"'+$Debugger+'" -batch -x "'+$DebuggerCommands+'" --args '+$launch }
$argument='/d /c "set "PATH='+$ToolchainBin+';%PATH%" && '+$launch+$(if($Text){' --text'}else{''})+' >"'+$log+'" 2>&1"'
$action=New-ScheduledTaskAction -Execute "$env:SystemRoot\System32\cmd.exe" -Argument $argument -WorkingDirectory $Work
$principal=New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Limited
$settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Seconds 45) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
try {
    Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal -Settings $settings | Out-Null
    $before=(Get-ScheduledTaskInfo -TaskName $taskName).LastRunTime
    Start-ScheduledTask -TaskName $taskName
    $deadline=(Get-Date).AddSeconds(55)
    do {
        Start-Sleep -Milliseconds 250
        $task=Get-ScheduledTask -TaskName $taskName
        $info=Get-ScheduledTaskInfo -TaskName $taskName
        if($info.LastRunTime -gt $before -and $task.State -ne 'Running' -and $task.State -ne 'Queued') { break }
        if((Get-Date) -ge $deadline) { throw 'Interactive WPF task did not finish within its time limit' }
    } while($true)
    if(Test-Path $log) { Get-Content $log }
    if($info.LastTaskResult -ne 0) { throw "Interactive WPF check failed: task result $($info.LastTaskResult)" }
    if($Debugger) { Write-Output 'Interactive debugger completed' }
    else { Write-Output 'Interactive WPF task passed' }
} finally {
    if(Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
        Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
    }
}
