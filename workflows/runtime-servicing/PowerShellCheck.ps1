$ErrorActionPreference = 'Stop'
if ([Environment]::Version -ne [version]'8.0.31') { throw 'Expected actual serviced runtime 8.0.31' }
$os = Get-CimInstance -Namespace root/cimv2 -ClassName Win32_OperatingSystem
if ($os.BuildNumber -ne [Environment]::OSVersion.Version.Build.ToString()) { throw 'CIM OS mismatch' }
$searcher = [System.Management.ManagementObjectSearcher]::new('SELECT BuildNumber FROM Win32_OperatingSystem')
try {
    foreach ($item in $searcher.Get()) {
        if ($item.BuildNumber -ne $os.BuildNumber) { throw 'WMI/CIM mismatch' }
    }
} finally { $searcher.Dispose() }
Add-Type -TypeDefinition 'public static class ServicedRuntimeOwnedType { public static string Value => "Grüße 日本語"; }'
if ([ServicedRuntimeOwnedType]::Value -ne 'Grüße 日本語') { throw 'Source Add-Type compiler failure' }
$value = @{ Text = 'Grüße 日本語'; Count = 42 } | ConvertTo-Json | ConvertFrom-Json
if ($value.Text -ne 'Grüße 日本語' -or $value.Count -ne 42) { throw 'PowerShell JSON conversion failure' }
$signature = Get-AuthenticodeSignature -FilePath ([IO.Path]::Combine($env:WINDIR, 'System32', 'kernel32.dll'))
if ($signature.Status -ne 'Valid' -or $null -eq $signature.SignerCertificate) { throw "Windows file signature failure: $($signature.Status)" }
$acl = Get-Acl -LiteralPath ([IO.Path]::Combine($env:WINDIR, 'System32', 'kernel32.dll'))
if ([string]::IsNullOrEmpty($acl.Owner)) { throw 'Native ACL read failed' }
$copy = [System.Management.Automation.PSSerializer]::Deserialize([System.Management.Automation.PSSerializer]::Serialize($os))
if ($copy.BuildNumber -ne $os.BuildNumber) { throw 'PowerShell CIM serializer failure' }
"PASS actual source PowerShell on .NET8.0.31: CIM/WMI, source Add-Type, Unicode JSON, trusted Windows Authenticode, native ACL read and CIM serialization"
