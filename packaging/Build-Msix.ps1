[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('x64', 'arm64')]
    [string]$Architecture,

    [string]$IdentityName = 'MrWyss.CopyPointerNotifier',
    [string]$Publisher = 'CN=MrWyss',
    [string]$PublisherDisplayName = 'MrWyss',
    [string]$Version = '1.1.0.0',
    [string]$NativeExecutable,
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
$architectureRoot = Join-Path $outputRoot $Architecture
$layout = Join-Path $architectureRoot 'layout'
$settingsOutput = Join-Path $layout 'settings'
$packagePath = Join-Path $outputRoot (
    "CopyPointerNotifier_{0}_{1}.msix" -f $Version, $Architecture)

function Get-PeMachine([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    try {
        $reader = [IO.BinaryReader]::new($stream)
        $stream.Position = 0x3c
        $peOffset = $reader.ReadInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "'$Path' is not a PE file."
        }
        return $reader.ReadUInt16()
    } finally {
        $stream.Dispose()
    }
}

function Escape-Xml([string]$Value) {
    return [Security.SecurityElement]::Escape($Value)
}

function Find-BuildTool([string]$Name) {
    # Try Windows SDK first
    $sdkBin = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\$Name"
    if (Test-Path $sdkBin) { return $sdkBin }
    # Try NuGet packages
    $nugetRoot = if ($env:NUGET_PACKAGES) {
        $env:NUGET_PACKAGES
    } else {
        Join-Path $HOME '.nuget\packages'
    }
    $packageRoot = Join-Path $nugetRoot 'microsoft.windows.sdk.buildtools'
    if (-not (Test-Path $packageRoot)) {
        $packageRoot = Join-Path $repoRoot 'packages\Microsoft.Windows.SDK.BuildTools.10.0.28000.2526'
    }
    $tool = Get-ChildItem -LiteralPath $packageRoot -Filter $Name -File -Recurse |
        Where-Object { $_.FullName -match '\\x64\\' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (-not $tool) {
        throw "$Name was not found. Install the Windows SDK or restore NuGet packages."
    }
    return $tool.FullName
}

if (-not $NativeExecutable) {
    $candidates = if ($Architecture -eq 'x64') {
        @(
            (Join-Path $repoRoot 'build\Release\CopyPointerNotifier.exe'),
            (Join-Path $repoRoot 'build\CopyPointerNotifier.exe')
        )
    } else {
        @(
            (Join-Path $repoRoot 'build-arm64\Release\CopyPointerNotifier.exe'),
            (Join-Path $repoRoot 'build-arm64\CopyPointerNotifier.exe')
        )
    }
    $NativeExecutable = $candidates |
        Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
}
if (-not $NativeExecutable -or -not (Test-Path -LiteralPath $NativeExecutable)) {
    throw "No $Architecture native executable was found. Build it first or pass -NativeExecutable."
}
$NativeExecutable = [IO.Path]::GetFullPath($NativeExecutable)

$expectedMachine = if ($Architecture -eq 'x64') { 0x8664 } else { 0xaa64 }
$nativeMachine = Get-PeMachine $NativeExecutable
if ($nativeMachine -ne $expectedMachine) {
    throw ("Native executable architecture mismatch: expected 0x{0:X4}, found 0x{1:X4}." -f
        $expectedMachine, $nativeMachine)
}

if (Test-Path -LiteralPath $architectureRoot) {
    Remove-Item -LiteralPath $architectureRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $settingsOutput -Force | Out-Null

$runtime = "win-$Architecture"
$settingsProject = Join-Path $repoRoot 'settings\CopyPointerNotifier.Settings.vcxproj'
$settingsPlatform = if ($Architecture -eq 'x64') { 'x64' } else { 'ARM64' }

$msbuild = Get-ChildItem 'C:\BuildTools2026\MSBuild\Current\Bin\MSBuild.exe' -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $msbuild) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $msbuild = & $vswhere -latest -products * `
            -requires Microsoft.Component.MSBuild `
            -find 'MSBuild\**\Bin\MSBuild.exe' |
            Select-Object -First 1
    }
}
if (-not $msbuild) {
    $msbuild = (Get-Command MSBuild.exe -ErrorAction Stop).Source
}

& $msbuild $settingsProject `
    /restore `
    /p:RestorePackagesConfig=true `
    /p:Configuration=Release `
    /p:Platform=$settingsPlatform `
    "/p:SolutionDir=$repoRoot\" `
    /p:OutDir="$settingsOutput\" `
    /p:IntDir="$architectureRoot\obj\" `
    /verbosity:minimal `
    /nologo
if ($LASTEXITCODE -ne 0) {
    throw "The native Settings build failed."
}

Get-ChildItem -LiteralPath $settingsOutput -File |
    Where-Object { $_.Extension -in '.exp', '.lib', '.pdb' } |
    Remove-Item -Force

$settingsMachine = Get-PeMachine (
    Join-Path $settingsOutput 'CopyPointerNotifier.Settings.exe')
if ($settingsMachine -ne $expectedMachine) {
    throw ("Settings executable architecture mismatch: expected 0x{0:X4}, found 0x{1:X4}." -f
        $expectedMachine, $settingsMachine)
}

Copy-Item -LiteralPath $NativeExecutable -Destination (
    Join-Path $layout 'CopyPointerNotifier.exe')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Assets') -Destination $layout -Recurse

$template = Get-Content -LiteralPath (
    Join-Path $PSScriptRoot 'AppxManifest.xml.in') -Raw
$manifest = $template.
    Replace('__IDENTITY_NAME__', (Escape-Xml $IdentityName)).
    Replace('__PUBLISHER__', (Escape-Xml $Publisher)).
    Replace('__PUBLISHER_DISPLAY_NAME__', (Escape-Xml $PublisherDisplayName)).
    Replace('__VERSION__', (Escape-Xml $Version)).
    Replace('__ARCHITECTURE__', $Architecture)
[IO.File]::WriteAllText(
    (Join-Path $layout 'AppxManifest.xml'),
    $manifest,
    [Text.UTF8Encoding]::new($false))

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
if (Test-Path -LiteralPath $packagePath) {
    Remove-Item -LiteralPath $packagePath -Force
}

# The Assets folder ships scale-* and targetsize-* qualified files. Windows only
# resolves those through a resource index, so build one before packing.
$makePri = Find-BuildTool 'makepri.exe'
$priConfig = Join-Path $architectureRoot 'obj\priconfig.xml'
New-Item -ItemType Directory -Path (Split-Path -Parent $priConfig) -Force | Out-Null
& $makePri createconfig /cf $priConfig /dq en-US /o
if ($LASTEXITCODE -ne 0) {
    throw 'MakePri createconfig failed.'
}

# Drop the auto resource package rules so every scale ends up in a single
# resources.pri; split .pri files would be ignored in a non-bundle package.
$priXml = [xml](Get-Content -LiteralPath $priConfig -Raw)
$packagingNode = $priXml.resources.SelectSingleNode('packaging')
if ($packagingNode) {
    [void]$priXml.resources.RemoveChild($packagingNode)
    $priXml.Save($priConfig)
}

& $makePri new /pr $layout /cf $priConfig /of (Join-Path $layout 'resources.pri') `
    /mn (Join-Path $layout 'AppxManifest.xml') /o
if ($LASTEXITCODE -ne 0) {
    throw 'MakePri failed.'
}

$makeAppx = Find-BuildTool 'makeappx.exe'
& $makeAppx pack /o /d $layout /p $packagePath
if ($LASTEXITCODE -ne 0) {
    throw 'MakeAppx failed.'
}

if ($CertificateThumbprint) {
    $signTool = Find-BuildTool 'signtool.exe'
    & $signTool sign /sha1 $CertificateThumbprint /fd SHA256 $packagePath
    if ($LASTEXITCODE -ne 0) {
        throw 'SignTool failed.'
    }
}

$sizeMiB = [math]::Round((Get-Item -LiteralPath $packagePath).Length / 1MB, 2)
Write-Host "Created $packagePath ($sizeMiB MiB)"
Write-Output $packagePath
