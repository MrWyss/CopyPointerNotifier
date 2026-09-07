<#
.SYNOPSIS
    Regenerates every raster icon in the repository from assets\AppIcon.svg.

.DESCRIPTION
    Each PNG is rendered by Inkscape directly at its final pixel size, so no
    bitmap is ever resampled. The script produces:

      * packaging\Assets\*.png - the full MSIX asset set (scale-100 .. scale-400
        plus targetsize variants, including the _altform-unplated files that
        stop Windows from drawing the taskbar icon on an accent colored plate).
      * assets\AppIcon.png     - 512x512 master PNG.
      * assets\AppIcon.ico     - multi resolution Win32 icon.
      * settings\Assets\*      - copies used by the settings executable.

.PARAMETER InkscapePath
    Path to inkscape.com. Auto-detected when omitted.
#>
[CmdletBinding()]
param(
    [string]$InkscapePath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$sourceSvg = Join-Path $repoRoot 'assets\AppIcon.svg'
$packagingAssets = Join-Path $repoRoot 'packaging\Assets'
$settingsAssets = Join-Path $repoRoot 'settings\Assets'

if (-not (Test-Path -LiteralPath $sourceSvg)) {
    throw "Source artwork '$sourceSvg' was not found."
}

if (-not $InkscapePath) {
    $candidates = @(
        'C:\Program Files\Inkscape\bin\inkscape.com',
        'C:\Program Files (x86)\Inkscape\bin\inkscape.com'
    )
    $command = Get-Command 'inkscape.com' -ErrorAction SilentlyContinue
    if ($command) { $candidates = @($command.Source) + $candidates }
    $InkscapePath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $InkscapePath -or -not (Test-Path -LiteralPath $InkscapePath)) {
    throw 'Inkscape was not found. Install it (winget install Inkscape.Inkscape) or pass -InkscapePath.'
}

Add-Type -AssemblyName System.Drawing

# MSIX scale qualifiers required for crisp rendering at every display scaling.
$scales = @(100, 125, 150, 200, 400)

# Sizes the shell asks for when it needs a plain icon (taskbar, jump lists,
# Alt+Tab, search results, ...). Windows downscales from the next larger entry,
# so these request points are enough without shipping every documented size.
$targetSizes = @(16, 24, 32, 48, 256)

# The manifest only references these three logos. Square71x71, Square310x310 and
# Wide310x150 exist solely for resizable Windows 10 Start tiles, which Windows 11
# never shows, so they are deliberately not generated.
# Tiles keep the artwork inside a safety margin; the 44x44 logo is full bleed.
$tiles = @(
    @{ Name = 'Square44x44Logo';   Width = 44;  Height = 44;  Padding = 1.00 },
    @{ Name = 'Square150x150Logo'; Width = 150; Height = 150; Padding = 0.80 },
    @{ Name = 'StoreLogo';         Width = 50;  Height = 50;  Padding = 0.80 }
)

$icoSizes = @(16, 20, 24, 32, 40, 48, 64, 96, 128, 256)

function Get-ScaledSize([int]$Base, [int]$Scale) {
    return [int][math]::Round($Base * $Scale / 100.0, [MidpointRounding]::AwayFromZero)
}

# --- Collect every distinct render size, then render each of them exactly once.

$renderSizes = [Collections.Generic.HashSet[int]]::new()
foreach ($tile in $tiles) {
    foreach ($scale in $scales) {
        $height = Get-ScaledSize $tile.Height $scale
        [void]$renderSizes.Add([int][math]::Round($height * $tile.Padding))
    }
}
foreach ($size in $targetSizes) { [void]$renderSizes.Add($size) }
foreach ($size in $icoSizes) { [void]$renderSizes.Add($size) }
[void]$renderSizes.Add(512)

$renders = @{}
$renderRoot = Join-Path ([IO.Path]::GetTempPath()) ("AppIconRender_" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $renderRoot -Force | Out-Null

function Save-Composite([int]$Width, [int]$Height, [int]$Artwork, [string]$Path) {
    $canvas = [Drawing.Bitmap]::new($Width, $Height, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $canvas.SetResolution(96, 96)
        $graphics = [Drawing.Graphics]::FromImage($canvas)
        try {
            $graphics.Clear([Drawing.Color]::Transparent)
            # Unscaled blit: the artwork was rendered at exactly this size.
            $graphics.DrawImageUnscaled(
                $renders[$Artwork],
                [int][math]::Floor(($Width - $Artwork) / 2.0),
                [int][math]::Floor(($Height - $Artwork) / 2.0))
        } finally {
            $graphics.Dispose()
        }
        $canvas.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $canvas.Dispose()
    }
}

try {
    $commands = foreach ($size in ($renderSizes | Sort-Object)) {
        $target = Join-Path $renderRoot "$size.png"
        "file-open:$sourceSvg; export-area-page; export-background-opacity:0; " +
        "export-width:$size; export-height:$size; export-filename:$target; export-do; file-close"
    }
    Write-Host "Rendering $($renderSizes.Count) sizes from AppIcon.svg ..."
    $commands + 'quit' | & $InkscapePath --shell | Out-Null

    foreach ($size in $renderSizes) {
        $path = Join-Path $renderRoot "$size.png"
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Inkscape did not produce a ${size}x${size} render."
        }
        $renders[$size] = [Drawing.Bitmap]::FromFile($path)
        # Inkscape stamps a DPI into the PNG; GDI+ would honour it and rescale.
        $renders[$size].SetResolution(96, 96)
        if ($renders[$size].Width -ne $size -or $renders[$size].Height -ne $size) {
            throw "Render for $size has unexpected size $($renders[$size].Width)x$($renders[$size].Height)."
        }
    }

    # --- MSIX assets -----------------------------------------------------

    if (Test-Path -LiteralPath $packagingAssets) {
        Remove-Item -LiteralPath $packagingAssets -Recurse -Force
    }
    New-Item -ItemType Directory -Path $packagingAssets -Force | Out-Null

    $written = 0
    foreach ($tile in $tiles) {
        foreach ($scale in $scales) {
            $width = Get-ScaledSize $tile.Width $scale
            $height = Get-ScaledSize $tile.Height $scale
            $artwork = [int][math]::Round($height * $tile.Padding)
            Save-Composite $width $height $artwork (
                Join-Path $packagingAssets ("{0}.scale-{1}.png" -f $tile.Name, $scale))
            $written++
        }
    }

    # Unplated variants keep the taskbar background transparent instead of
    # letting Windows plate the icon with the system accent color. The
    # lightunplated form is omitted on purpose: Windows falls back to the
    # unplated file, which reads correctly on light backgrounds too.
    foreach ($size in $targetSizes) {
        foreach ($form in @('', '_altform-unplated')) {
            Save-Composite $size $size $size (
                Join-Path $packagingAssets (
                    "Square44x44Logo.targetsize-{0}{1}.png" -f $size, $form))
            $written++
        }
    }

    Write-Host "Wrote $written files to packaging\Assets."

    # --- Win32 icon and master PNG ---------------------------------------

    $appIconPng = Join-Path $repoRoot 'assets\AppIcon.png'
    Save-Composite 512 512 512 $appIconPng

    $icoPath = Join-Path $repoRoot 'assets\AppIcon.ico'
    $images = foreach ($size in $icoSizes) {
        $stream = [IO.MemoryStream]::new()
        try {
            $renders[$size].Save($stream, [Drawing.Imaging.ImageFormat]::Png)
            [pscustomobject]@{ Size = $size; Bytes = $stream.ToArray() }
        } finally {
            $stream.Dispose()
        }
    }

    $ico = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($ico)
    try {
        $writer.Write([uint16]0)               # reserved
        $writer.Write([uint16]1)               # type: icon
        $writer.Write([uint16]$images.Count)
        $offset = 6 + 16 * $images.Count
        foreach ($image in $images) {
            $writer.Write([byte]($image.Size % 256))   # 256 is stored as 0
            $writer.Write([byte]($image.Size % 256))
            $writer.Write([byte]0)             # palette entries
            $writer.Write([byte]0)             # reserved
            $writer.Write([uint16]1)           # color planes
            $writer.Write([uint16]32)          # bits per pixel
            $writer.Write([uint32]$image.Bytes.Length)
            $writer.Write([uint32]$offset)
            $offset += $image.Bytes.Length
        }
        foreach ($image in $images) { $writer.Write($image.Bytes) }
        $writer.Flush()
        [IO.File]::WriteAllBytes($icoPath, $ico.ToArray())
    } finally {
        $writer.Dispose()
    }

    New-Item -ItemType Directory -Path $settingsAssets -Force | Out-Null
    Copy-Item -LiteralPath $icoPath -Destination (Join-Path $settingsAssets 'AppIcon.ico') -Force
    Copy-Item -LiteralPath $appIconPng -Destination (Join-Path $settingsAssets 'AppIcon.png') -Force

    Write-Host 'Wrote assets\AppIcon.png, assets\AppIcon.ico and the settings copies.'
} finally {
    foreach ($bitmap in $renders.Values) { $bitmap.Dispose() }
    Remove-Item -LiteralPath $renderRoot -Recurse -Force -ErrorAction SilentlyContinue
}
