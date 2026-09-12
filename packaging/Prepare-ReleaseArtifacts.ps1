[CmdletBinding()]
param(
    [string]$Version = '1.1.1.0'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$unsignedDirectory = Join-Path $repoRoot 'artifacts\msix'
$signedDirectory = Join-Path $repoRoot 'artifacts\msix-signed'
$submissionDirectory = Join-Path $repoRoot "artifacts\store-submission-$Version"
$certificateDirectory = Join-Path $repoRoot 'artifacts\certificates'
$certificatePath = Join-Path $certificateDirectory 'CopyPointerNotifier-Test.cer'

$x64Name = "CopyPointerNotifier_${Version}_x64.msix"
$arm64Name = "CopyPointerNotifier_${Version}_arm64.msix"
$bundleName = "CopyPointerNotifier_${Version}.msixbundle"
$requiredPackages = @($x64Name, $arm64Name, $bundleName)

foreach ($name in $requiredPackages) {
    $path = Join-Path $unsignedDirectory $name
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing unsigned release package '$path'. Build both architectures and the bundle first."
    }
}

foreach ($directory in @($signedDirectory, $submissionDirectory)) {
    if (Test-Path -LiteralPath $directory) {
        Remove-Item -LiteralPath $directory -Recurse -Force
    }
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

foreach ($name in @($x64Name, $arm64Name)) {
    $signedPackage = Join-Path $signedDirectory $name
    Copy-Item -LiteralPath (Join-Path $unsignedDirectory $name) -Destination $signedPackage
    & (Join-Path $PSScriptRoot 'Sign-TestPackage.ps1') -Package $signedPackage
}

& (Join-Path $PSScriptRoot 'Build-MsixBundle.ps1') `
    -Version $Version `
    -OutputDirectory $signedDirectory
$signedBundle = Join-Path $signedDirectory $bundleName
& (Join-Path $PSScriptRoot 'Sign-TestPackage.ps1') -Package $signedBundle

Copy-Item -LiteralPath $certificatePath -Destination (
    Join-Path $signedDirectory 'CopyPointerNotifier-TestCertificate.cer')

$installScript = @"
#Requires -RunAsAdministrator

`$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

`$root = Split-Path -Parent `$MyInvocation.MyCommand.Path
`$certificate = Join-Path `$root 'CopyPointerNotifier-TestCertificate.cer'
`$bundle = Join-Path `$root '$bundleName'

Import-Certificate -FilePath `$certificate -CertStoreLocation 'Cert:\LocalMachine\Root' | Out-Null
Import-Certificate -FilePath `$certificate -CertStoreLocation 'Cert:\LocalMachine\TrustedPeople' | Out-Null

Add-AppxPackage -Path `$bundle -ForceApplicationShutdown
Write-Host 'Copy Pointer Notifier $Version installed.'
"@
[IO.File]::WriteAllText(
    (Join-Path $signedDirectory 'Install-TestPackage.ps1'),
    $installScript,
    [Text.UTF8Encoding]::new($false))

$testReadme = @"
COPY POINTER NOTIFIER - SIGNED TEST PACKAGE

This folder is for installation testing only. Do not upload these signed files
to Partner Center.

The .cer file contains the public certificate only. It does not contain the
private signing key.

To install on another test machine:

1. Copy this entire folder to the machine.
2. Open PowerShell as Administrator.
3. Run:

   Set-ExecutionPolicy -Scope Process Bypass
   .\Install-TestPackage.ps1

The installer trusts the test certificate for the local machine and installs
the architecture-appropriate package from the bundle.

The Windows App Runtime 1.8 framework dependency must be available on the test
machine.
"@
[IO.File]::WriteAllText(
    (Join-Path $signedDirectory 'README.txt'),
    $testReadme,
    [Text.UTF8Encoding]::new($false))

Copy-Item -LiteralPath (Join-Path $unsignedDirectory $bundleName) `
    -Destination $submissionDirectory
Copy-Item -LiteralPath (Join-Path $repoRoot 'store-listing.md') `
    -Destination $submissionDirectory

$screenshotsDirectory = Join-Path $submissionDirectory 'screenshots'
$promotionalDirectory = Join-Path $submissionDirectory 'promotional-images'
New-Item -ItemType Directory -Path $screenshotsDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $promotionalDirectory -Force | Out-Null

1..5 | ForEach-Object {
    $image = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'docs\images') `
        -Filter ("StoreScreenshot-{0:D2}-*.png" -f $_) -File)
    if ($image.Count -ne 1) {
        throw "Expected one Store screenshot for index $_, found $($image.Count)."
    }
    Copy-Item -LiteralPath $image.FullName -Destination $screenshotsDirectory
}

6..7 | ForEach-Object {
    $image = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'docs\images') `
        -Filter ("StoreScreenshot-{0:D2}-*.png" -f $_) -File)
    if ($image.Count -ne 1) {
        throw "Expected one promotional image for index $_, found $($image.Count)."
    }
    Copy-Item -LiteralPath $image.FullName -Destination $promotionalDirectory
}

foreach ($directory in @($signedDirectory, $submissionDirectory)) {
    $checksums = Get-ChildItem -LiteralPath $directory -File -Recurse |
        Where-Object Name -NotLike 'SHA256SUMS.txt' |
        Sort-Object FullName |
        ForEach-Object {
            $hash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
            $relativePath = [IO.Path]::GetRelativePath($directory, $_.FullName)
            "$($hash.Hash)  $relativePath"
        }
    [IO.File]::WriteAllLines(
        (Join-Path $directory 'SHA256SUMS.txt'),
        $checksums,
        [Text.UTF8Encoding]::new($false))
}

Write-Host ''
Write-Host "Signed test package : $signedDirectory"
Write-Host "Store submission    : $submissionDirectory"
