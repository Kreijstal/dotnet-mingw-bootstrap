# Regression for native WMI proxy security in the default Windows apartment.
$ErrorActionPreference='Stop'
if ([Threading.Thread]::CurrentThread.ApartmentState -ne 'STA') { throw 'Run this diagnostic with -STA' }
$searcher=[System.Management.ManagementObjectSearcher]::new('SELECT BuildNumber FROM Win32_OperatingSystem')
try {
    $results=$searcher.Get()
    try {
        $rows=$results.GetEnumerator()
        try {
            $count=0
            while ($rows.MoveNext()) {
                $item=$rows.Current
                try {
                    if ($item['BuildNumber'] -ne [Environment]::OSVersion.Version.Build.ToString()) { throw 'WMI OS mismatch' }
                    $count++
                } finally { $item.Dispose() }
            }
            if ($count -ne 1) { throw "WMI row count: $count" }
        } finally { $rows.Dispose() }
    } finally { $results.Dispose() }
} finally { $searcher.Dispose() }
'PASS source WMI local enumeration in STA'
