[CmdletBinding()]
param(
    [string]$Version = '1.1.0.0',
    [string]$OutputDirectory,
    [string]$CertificateThumbprint
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = if ($OutputDirectory) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else {
    Join-Path $repoRoot 'artifacts\msix'
}
$bundleInput = Join-Path $outputRoot 'bundle-input'
$bundlePath = Join-Path $outputRoot (
    "CopyPointerNotifier_{0}.msixbundle" -f $Version)

$packages = @(
    (Join-Path $outputRoot ("CopyPointerNotifier_{0}_x64.msix" -f $Version)),
    (Join-Path $outputRoot ("CopyPointerNotifier_{0}_arm64.msix" -f $Version))
)
foreach ($package in $packages) {
    if (-not (Test-Path -LiteralPath $package)) {
        throw "Missing package '$package'. Build both architectures first."
    }
}

if (Test-Path -LiteralPath $bundleInput) {
    Remove-Item -LiteralPath $bundleInput -Recurse -Force
}
New-Item -ItemType Directory -Path $bundleInput -Force | Out-Null
Copy-Item -LiteralPath $packages -Destination $bundleInput

$nugetRoot = if ($env:NUGET_PACKAGES) {
    $env:NUGET_PACKAGES
} else {
    Join-Path $HOME '.nuget\packages'
}
$toolRoot = Join-Path $nugetRoot 'microsoft.windows.sdk.buildtools'
$makeAppx = (Get-ChildItem -LiteralPath $toolRoot -Filter makeappx.exe -File -Recurse |
    Where-Object { $_.FullName -match '\\x64\\' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1).FullName
if (-not $makeAppx) {
    throw 'MakeAppx was not found. Restore the Settings project first.'
}

if (Test-Path -LiteralPath $bundlePath) {
    Remove-Item -LiteralPath $bundlePath -Force
}
& $makeAppx bundle /o /d $bundleInput /p $bundlePath
if ($LASTEXITCODE -ne 0) {
    throw 'MakeAppx bundle failed.'
}

if ($CertificateThumbprint) {
    $signTool = (Get-ChildItem -LiteralPath $toolRoot -Filter signtool.exe -File -Recurse |
        Where-Object { $_.FullName -match '\\x64\\' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1).FullName
    & $signTool sign /sha1 $CertificateThumbprint /fd SHA256 $bundlePath
    if ($LASTEXITCODE -ne 0) {
        throw 'SignTool failed.'
    }
}

Remove-Item -LiteralPath $bundleInput -Recurse -Force
$sizeMiB = [math]::Round((Get-Item -LiteralPath $bundlePath).Length / 1MB, 2)
Write-Host "Created $bundlePath ($sizeMiB MiB)"
Write-Output $bundlePath
