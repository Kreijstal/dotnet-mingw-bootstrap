param([Parameter(Mandatory)][string]$Output)
$ErrorActionPreference='Stop'
$env:PSModulePath=Join-Path $PSHOME 'Modules'
$null=New-Item -ItemType Directory -Path $Output -ErrorAction Stop
$modules=@{
    'PowerShellGet'='2.2.5'
    'PackageManagement'='1.4.8.1'
    'Microsoft.PowerShell.PSResourceGet'='1.1.1'
    'Microsoft.PowerShell.Archive'='1.2.6'
    'PSReadLine'='2.3.6'
    'ThreadJob'='2.0.3'
}
foreach ($name in $modules.Keys) {
    $module=Import-Module -FullyQualifiedName @{ModuleName=$name;RequiredVersion=$modules[$name]} -PassThru -ErrorAction Stop
    if (-not $module -or -not $module.ModuleBase.StartsWith((Join-Path $PSHOME 'Modules'), [StringComparison]::OrdinalIgnoreCase)) { throw "Foreign module: $name" }
    "MODULE $name $($module.Version) $($module.ModuleBase)"
}
foreach ($name in 'Find-Module','Get-PackageProvider','Find-PSResource','Compress-Archive','Get-PSReadLineOption','Start-ThreadJob') {
    $command=Get-Command $name -ErrorAction Stop
    if (-not $command.Module) { throw "No source module for $name" }
}
if (-not (Get-PSReadLineOption)) { throw 'PSReadLine options' }
if (-not (Get-PackageProvider -Name NuGet -ListAvailable -ErrorAction Stop)) { throw 'Source NuGet provider discovery' }

$inputFile=Join-Path $Output 'unicode.txt'
$archive=Join-Path $Output 'owned.zip'
$expanded=Join-Path $Output 'expanded'
$text='λ original console/gallery archive'
[IO.File]::WriteAllText($inputFile,$text)
Compress-Archive -LiteralPath $inputFile -DestinationPath $archive -ErrorAction Stop
Expand-Archive -LiteralPath $archive -DestinationPath $expanded -ErrorAction Stop
if ([IO.File]::ReadAllText((Join-Path $expanded 'unicode.txt')) -cne $text) { throw 'Original gallery archive round trip' }

$job=Start-ThreadJob -ScriptBlock {
    $PSVersionTable.PSVersion.ToString()
    (Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop).BuildNumber
    'λ source thread'
}
try {
    $null=Wait-Job $job -Timeout 45
    if ($job.State -ne 'Completed') { throw "Source thread state: $($job.State)" }
    $result=@(Receive-Job $job -ErrorAction Stop)
    if ($result.Count -ne 3 -or $result[0] -ne '7.4.20' -or $result[1] -ne [Environment]::OSVersion.Version.Build.ToString() -or $result[2] -cne 'λ source thread') { throw 'ThreadJob original console/CIM result' }
} finally {
    Remove-Job $job -Force -ErrorAction SilentlyContinue
}
foreach ($assembly in [AppDomain]::CurrentDomain.GetAssemblies()) {
    if (-not $assembly.IsDynamic -and $assembly.Location -match '[\\/]Microsoft.NET[\\/]Framework') { throw "Framework fallback: $($assembly.Location)" }
}
'PASS six source gallery modules in original console, source NuGet provider, PSReadLine options, Unicode archive round trip and real ThreadJob/CIM/Unicode execution'
