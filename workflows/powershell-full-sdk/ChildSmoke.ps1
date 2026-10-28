$ErrorActionPreference='Stop'
$env:PSModulePath=Join-Path $PSHOME 'Modules'
if (-not (Test-Path (Join-Path $PSHOME 'pwsh.exe'))) { throw 'Native source console missing' }
$job=Start-Job -ScriptBlock {
    $PSVersionTable.PSVersion.ToString()
    [Environment]::Version.ToString()
    'λ original native child'
    (Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop).BuildNumber
    [System.Management.Automation.PowerShell].Assembly.Location
}
try {
    $null=Wait-Job $job -Timeout 60
    if ($job.State -ne 'Completed') { throw "Original child state: $($job.State)" }
    $result=@(Receive-Job $job -ErrorAction Stop)
    if ($result.Count -ne 5 -or $result[0] -ne '7.4.20' -or $result[1] -ne '8.0.31' -or $result[2] -cne 'λ original native child' -or $result[3] -ne [Environment]::OSVersion.Version.Build.ToString() -or $result[4] -ne (Join-Path $PSHOME 'System.Management.Automation.dll')) {
        throw "Original source child result mismatch: $($result -join ';')"
    }
} finally {
    Remove-Job $job -Force -ErrorAction SilentlyContinue
}
'PASS original native console starts actual source pwsh.exe child, Unicode CLIXML, original source engine, serviced runtime and live CIM'
