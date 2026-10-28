$ErrorActionPreference='Stop'
$env:PSModulePath=Join-Path $PSHOME 'Modules'
if ($PSVersionTable.PSVersion.ToString() -ne '7.4.20') { throw 'Wrong source PowerShell version' }
if ([Environment]::Version.ToString() -ne '8.0.31') { throw 'Serviced runtime required' }
if ([System.Management.Automation.PowerShell].Assembly.Location -ne (Join-Path $PSHOME 'System.Management.Automation.dll')) { throw 'Foreign engine assembly' }

# Resolve through PowerShell's generated full SDK catalog before explicitly
# loading any of these non-core dependency assemblies.
$types=@(
    [System.Data.SqlClient.SqlConnection],
    [System.ServiceModel.BasicHttpBinding],
    [System.ServiceModel.NetTcpBinding],
    [System.IO.Packaging.Package],
    [System.Speech.Synthesis.SpeechSynthesizer],
    [System.Drawing.Bitmap],
    [System.Management.ManagementObjectSearcher]
)
foreach ($type in $types) { "CATALOG $($type.FullName) -> $($type.Assembly.GetName().Name)" }

Import-Module Microsoft.PowerShell.Management,Microsoft.PowerShell.Utility,Microsoft.PowerShell.Security,CimCmdlets -ErrorAction Stop
$os=Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop
if ($os.BuildNumber -ne [Environment]::OSVersion.Version.Build.ToString()) { throw 'Original CIM result mismatch' }
$process=Get-CimInstance Win32_Process -Filter "ProcessId=$PID"
$owner=Invoke-CimMethod -InputObject $process -MethodName GetOwner
if ($owner.ReturnValue -ne 0 -or -not $owner.User) { throw 'CIM GetOwner failed' }
$copy=[System.Management.Automation.PSSerializer]::Deserialize([System.Management.Automation.PSSerializer]::Serialize($os))
if ($copy.BuildNumber -ne $os.BuildNumber) { throw 'CIM serializer' }

$json=@{text='λ full SDK';numbers=@(1,2,3)} | ConvertTo-Json -Compress
if (($json | ConvertFrom-Json).text -cne 'λ full SDK') { throw 'Source JSON cmdlets' }
Add-Type -TypeDefinition 'public static class OwnedFullSdkProbe { public static int Answer() => 42; }'
if ([OwnedFullSdkProbe]::Answer() -ne 42) { throw 'Source Add-Type' }
$signature=Get-AuthenticodeSignature (Join-Path $env:WINDIR 'System32/kernel32.dll')
if ($signature.Status -ne 'Valid') { throw "Source signature provider: $($signature.Status)" }

Import-Module Microsoft.PowerShell.LocalAccounts -ErrorAction Stop
$users=@(Get-LocalUser -ErrorAction Stop)
if ($users.Count -lt 1) { throw 'Source LocalAccounts read-only enumeration' }
foreach ($assembly in [AppDomain]::CurrentDomain.GetAssemblies()) {
    if (-not $assembly.IsDynamic -and $assembly.Location -match '[\\/]Microsoft.NET[\\/]Framework') { throw "Framework fallback: $($assembly.Location)" }
}
'PASS original full SDK catalog/console, module discovery, CIM/GetOwner/serialization, JSON/Add-Type/Authenticode, LocalAccounts and no Framework implementations'
