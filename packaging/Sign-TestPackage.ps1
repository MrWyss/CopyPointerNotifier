<#
.SYNOPSIS
    Creates a self-signed test certificate and signs a local MSIX package.

.DESCRIPTION
    Only for local install testing. The certificate subject must match the
    Publisher in the package manifest exactly, otherwise Windows rejects the
    signature. Store submissions stay unsigned - Partner Center signs them.

    The script writes the private .pfx and public .cer under
    artifacts\certificates. The certificate has to be trusted before the MSIX
    will install.

.PARAMETER Package
    Path to the .msix or .msixbundle to sign.

.PARAMETER Publisher
    Certificate subject. Must equal the manifest Publisher value.
#>
[CmdletBinding()]
param(
    [string]$Package,
    [string]$Publisher = 'CN=F7578173-2D1D-45C0-A422-4858557D8E62',
    [string]$FriendlyName = 'Copy Pointer Notifier Test Certificate',
    [string]$PfxPassword = 'CopyPointerNotifier'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $Package) {
    $Package = Get-ChildItem -LiteralPath (Join-Path $repoRoot 'artifacts\msix') `
        -Filter '*.msix*' -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Package -or -not (Test-Path -LiteralPath $Package)) {
    throw 'No package found. Build one with packaging\Build-Msix.ps1 or pass -Package.'
}
$Package = [IO.Path]::GetFullPath($Package)

function Find-BuildTool([string]$Name) {
    $sdkBin = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\$Name"
    if (Test-Path $sdkBin) { return $sdkBin }
    $sdk = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Filter $Name -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (-not $sdk) { throw "$Name was not found. Install the Windows SDK." }
    return $sdk.FullName
}

$certDirectory = Join-Path $repoRoot 'artifacts\certificates'
New-Item -ItemType Directory -Path $certDirectory -Force | Out-Null
$pfxPath = Join-Path $certDirectory 'CopyPointerNotifier-Test.pfx'
$cerPath = Join-Path $certDirectory 'CopyPointerNotifier-Test.cer'

$certificate = Get-ChildItem Cert:\CurrentUser\My |
    Where-Object { $_.Subject -eq $Publisher -and $_.NotAfter -gt (Get-Date) } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

if (-not $certificate) {
    Write-Host "Creating a self-signed code signing certificate for $Publisher ..."
    $certificate = New-SelfSignedCertificate `
        -Type Custom `
        -Subject $Publisher `
        -FriendlyName $FriendlyName `
        -KeyUsage DigitalSignature `
        -KeyAlgorithm RSA `
        -KeyLength 2048 `
        -CertStoreLocation 'Cert:\CurrentUser\My' `
        -NotAfter (Get-Date).AddYears(3) `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
} else {
    Write-Host "Reusing certificate $($certificate.Thumbprint)."
}

$securePassword = ConvertTo-SecureString -String $PfxPassword -Force -AsPlainText
Export-PfxCertificate -Cert $certificate -FilePath $pfxPath -Password $securePassword | Out-Null
Export-Certificate -Cert $certificate -FilePath $cerPath -Type CERT | Out-Null

$signTool = Find-BuildTool 'signtool.exe'
& $signTool sign /fd SHA256 /a /f $pfxPath /p $PfxPassword $Package
if ($LASTEXITCODE -ne 0) {
    throw 'SignTool failed.'
}

Write-Host ''
Write-Host "Signed  : $Package"
Write-Host "Cert    : $cerPath"
Write-Host ''
Write-Host 'Trust the certificate once (elevated), then install:'
Write-Host "  Import-Certificate -FilePath '$cerPath' -CertStoreLocation Cert:\LocalMachine\TrustedPeople"
Write-Host "  Add-AppxPackage '$Package'"
