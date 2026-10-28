param([string]$Manifest,[string]$Root,[string]$Uri,[string]$Name,[string]$MainName,[string]$ExpectedHash)
$ErrorActionPreference='Stop'
try {
function Require([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
Import-Module $Manifest -Force
New-Item -ItemType Directory -Path $Root | Out-Null
$repo='OwnedHTTP'+[guid]::NewGuid().ToString('N')
$store=Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)) 'PSResourceGet/PSResourceRepository.xml'
[xml]$beforeXml=Get-Content $store -Raw
$before=@($beforeXml.configuration.ChildNodes | ForEach-Object {$_.OuterXml})
$registered=$false
try {
    Microsoft.PowerShell.PSResourceGet\Register-PSResourceRepository -Name $repo -Uri $Uri -ApiVersion V3 -Trusted
    $registered=$true
    $unauthorized=$false
    try { Microsoft.PowerShell.PSResourceGet\Find-PSResource -Name $Name -Repository $repo -ErrorAction Stop | Out-Null }
    catch { $unauthorized=$_.Exception.Message -match '401|Unauthorized' }
    Require $unauthorized 'HTTP401 remains an error without credentials'
    $credential=[pscredential]::new('owned-user',(ConvertTo-SecureString 'owned-fixture-password' -AsPlainText -Force))
    $found=Microsoft.PowerShell.PSResourceGet\Find-PSResource -Name $Name -Repository $repo -Credential $credential
    Require ($found.Name -eq $Name -and $found.Version.ToString() -eq '1.0.0' -and $found.Repository -eq $repo) 'authenticated V3 registration metadata'
    $latest=Microsoft.PowerShell.PSResourceGet\Find-PSResource -Name $MainName -Repository $repo -Credential $credential
    Require ($latest.Version.ToString() -eq '1.1.0') 'V3 latest numeric version order'
    $exact=Microsoft.PowerShell.PSResourceGet\Find-PSResource -Name $MainName -Version '[1.0.0]' -Repository $repo -Credential $credential
    Require ($exact.Version.ToString() -eq '1.0.0') 'V3 exact version selection'
    $archive=New-Item -ItemType Directory -Path (Join-Path $Root 'archive')
    Microsoft.PowerShell.PSResourceGet\Save-PSResource -Name $Name -Repository $repo -Credential $credential -Path $archive.FullName -TrustRepository -AsNupkg
    $nupkg=Join-Path $archive "$Name.1.0.0.nupkg"
    Require ((Get-FileHash $nupkg -Algorithm SHA256).Hash.ToLowerInvariant() -eq $ExpectedHash) 'HTTP download exact original published archive bytes'
    $saved=New-Item -ItemType Directory -Path (Join-Path $Root 'saved')
    Microsoft.PowerShell.PSResourceGet\Save-PSResource -Name $Name -Version '[1.0.0]' -Repository $repo -Credential $credential -Path $saved.FullName -TrustRepository
    Import-Module (Join-Path $saved "$Name/1.0.0/$Name.psd1") -Force
    Require ((Get-OwnedDependency) -eq 'dependency λ') 'saved HTTP package Unicode execution'
    Remove-Module $Name -Force
    $missing=$false
    try{Microsoft.PowerShell.PSResourceGet\Find-PSResource -Name 'OwnedMissingHTTPPackage' -Repository $repo -Credential $credential -ErrorAction Stop | Out-Null}
    catch{$missing=$_.FullyQualifiedErrorId -like 'PackageNotFound*'}
    Require $missing 'V3 HTTP404 maps to PackageNotFound'
    Write-Output 'PASS original PSResourceGet HTTP V3 index/registration JSON,401 credential challenge/authentication, version selection, exact NuGet download, extracted Unicode execution and404 error mapping'
} finally {
    if($registered){Microsoft.PowerShell.PSResourceGet\Unregister-PSResourceRepository -Name $repo -ErrorAction Stop}
    Remove-Module $Name -Force -ErrorAction SilentlyContinue
    [xml]$afterXml=Get-Content $store -Raw
    $after=@($afterXml.configuration.ChildNodes | ForEach-Object {$_.OuterXml})
    foreach($entry in $before){Require ($after -contains $entry) 'pre-existing repository entry preserved'}
    Require (-not(@($afterXml.configuration.ChildNodes | Where-Object {$_.GetAttribute('Name') -eq $repo}).Count)) 'owned HTTP repository removed'
    Write-Output 'PASS owned HTTP repository cleanup and pre-existing settings preservation'
}
} catch { [Console]::Error.WriteLine($_.InvocationInfo.PositionMessage); [Console]::Error.WriteLine($_.ScriptStackTrace); [Console]::Error.WriteLine($_.Exception.ToString()); throw }
