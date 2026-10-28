$ErrorActionPreference='Stop'
$env:PSModulePath=Join-Path $PSHOME 'Modules'
$expectedBuild=[Environment]::OSVersion.Version.Build.ToString()
$cim=Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
if ($cim.BuildNumber -ne $expectedBuild) { throw 'CIM OS build mismatch' }
# Pass a CLR null string: PowerShell normally converts $null to an empty string.
$session=[Microsoft.Management.Infrastructure.CimSession]::Create([NullString]::Value)
try {
    $count=0
    # PowerShell foreach does not dispose this native-operation enumerator.
    $rows=$session.QueryInstances('root/cimv2','WQL',"SELECT ProcessId FROM Win32_Process WHERE ProcessId=$PID").GetEnumerator()
    try {
        while ($rows.MoveNext()) {
            $item=$rows.Current
            try {
                if ($item.CimInstanceProperties['ProcessId'].Value -ne $PID) { throw 'MMI process mismatch' }
                $count++
            } finally { $item.Dispose() }
        }
    } finally { $rows.Dispose() }
    if ($count -ne 1) { throw "MMI process count: $count" }
} finally { $session.Dispose() }

$searcher=[System.Management.ManagementObjectSearcher]::new('SELECT BuildNumber FROM Win32_OperatingSystem')
try {
    $results=$searcher.Get()
    try {
        $count=0
        foreach ($item in $results) {
            try {
                if ($item['BuildNumber'] -ne $expectedBuild) { throw 'WMI OS build mismatch' }
                $count++
            } finally { $item.Dispose() }
        }
        if ($count -ne 1) { throw "WMI OS count: $count" }
    } finally { $results.Dispose() }
} finally { $searcher.Dispose() }
$process=[System.Management.ManagementObject]::new("Win32_Process.Handle='$PID'")
try {
    $parameters=$process.GetMethodParameters('GetOwner')
    try {
        $owner=$process.InvokeMethod('GetOwner',$parameters,$null)
        try {
            if ($owner['ReturnValue'] -ne 0 -or -not $owner['User']) { throw 'WMI GetOwner failed' }
        } finally { $owner.Dispose() }
    } finally { if ($null -ne $parameters) { $parameters.Dispose() } }
} finally { $process.Dispose() }
foreach ($type in @([Microsoft.Management.Infrastructure.CimSession],[System.Management.ManagementObjectSearcher])) {
    $name=$type.Assembly.GetName().Name+'.dll'
    $allowed=@((Join-Path $PSHOME $name))
    if ($name -eq 'System.Management.dll') {
        $allowed+=Join-Path $PSHOME ('runtimes/win/lib/net8.0/'+$name)
    }
    $actual=[IO.Path]::GetFullPath($type.Assembly.Location)
    $allowed=@($allowed | ForEach-Object { [IO.Path]::GetFullPath($_) })
    if ($actual -notin $allowed) { throw "Foreign assembly: $type at $actual" }
}
'PASS packaged MMI direct local query/disposal, original CIM cmdlets and source System.Management WMI OS/GetOwner/disposal'
