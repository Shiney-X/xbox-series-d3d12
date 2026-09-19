param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repositoryRoot 'out\package\xbox-uwp'
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path $OutputDirectory).Path

$msbuildCommand = Get-Command 'MSBuild.exe' -ErrorAction SilentlyContinue
if ($null -eq $msbuildCommand) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw 'MSBuild.exe and vswhere.exe were not found. Install Visual Studio 2022 with UWP C++ tools.'
    }
    $msbuildPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($msbuildPath)) {
        throw 'A Visual Studio installation containing MSBuild was not found.'
    }
} else {
    $msbuildPath = $msbuildCommand.Source
}

$subject = 'CN=xbox-series-d3d12-dev'
$certificate = New-SelfSignedCertificate `
    -Type Custom `
    -Subject $subject `
    -FriendlyName 'xbox-series-d3d12 ephemeral sideload certificate' `
    -CertStoreLocation 'Cert:\CurrentUser\My' `
    -KeyAlgorithm RSA `
    -KeyLength 2048 `
    -HashAlgorithm SHA256 `
    -KeyExportPolicy Exportable `
    -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')

try {
    $certificatePath = Join-Path $OutputDirectory 'xbox-series-d3d12-dev.cer'
    Export-Certificate -Cert $certificate -FilePath $certificatePath -Force | Out-Null

    $project = Join-Path $repositoryRoot 'platform\xbox_uwp\xbox_phase0_uwp.vcxproj'
    & $msbuildPath $project `
        /restore `
        /m `
        /p:Configuration=$Configuration `
        /p:Platform=x64 `
        /p:AppxBundle=Never `
        /p:AppxPackageSigningEnabled=true `
        /p:PackageCertificateThumbprint=$($certificate.Thumbprint) `
        /p:GenerateAppxPackageOnBuild=true `
        /p:UapAppxPackageBuildMode=SideloadOnly `
        /p:AppxPackageDir="$OutputDirectory\AppPackages\"
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed with exit code $LASTEXITCODE."
    }
} finally {
    Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($certificate.Thumbprint)" -Force -ErrorAction SilentlyContinue
}

Write-Host "Signed sideload package and public certificate: $OutputDirectory"
