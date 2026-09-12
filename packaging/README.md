# MSIX packaging

The package contains the native tray process and the native C++/WinRT WinUI Settings process. MSIX builds use the shared Windows App Runtime 1.8 framework package without embedding any managed runtime.

The packaged app uses the manifest-declared Windows startup task. Unpackaged builds continue to use the current-user `Run` registry entry.

The Microsoft Store installs the Windows App Runtime framework dependency declared by the package. No .NET runtime is required.

With version 1.1.1.0 and the native Settings implementation, each architecture package is approximately 0.5 MiB and the combined x64/ARM64 bundle is approximately 1 MiB.

The Store package appears in the Windows Start menu. Hiding it with `AppListEntry="none"` would classify it as a headless app and require the Partner Center `HeadlessAppBypass` waiver.

## Assets

`packaging\Assets` is generated, not hand-edited. Run `tools\Build-Icons.ps1`
(requires Inkscape) after changing `assets\AppIcon.svg`; every PNG is rendered
straight from the SVG at its final pixel size, so nothing is ever upscaled.

The folder holds only qualifier-named files (`.scale-100` … `.scale-400` and
`Square44x44Logo.targetsize-*`). `Build-Msix.ps1` therefore runs `makepri` to
index them into a single `resources.pri` before packing; without that index
Windows cannot resolve the qualified names the manifest refers to.

The `_altform-unplated` variants are what keep the taskbar and Start icons
transparent — without them Windows draws the icon on a system accent colored
plate. `_altform-lightunplated` is intentionally absent: Windows falls back to
the unplated file, which reads correctly on light backgrounds too.

`BackgroundColor="transparent"` is deliberate. Windows 11 Start and taskbar use
the unplated 44x44 assets, so they stay transparent. The App Installer dialog
and Windows 10 live tiles ignore transparency and fall back to the system accent
color; replacing the value with a fixed color would remove that at the cost of a
permanent colored plate on every surface.

Only the three logos the manifest references are generated. `Square71x71Logo`,
`Square310x310Logo` and `Wide310x150Logo` exist purely for resizable Windows 10
Start tiles, which Windows 11 never displays, so they are not shipped.

## Local install testing

Store submissions stay unsigned. After building both architecture packages and
the bundle, prepare the signed test files and Store upload folder together:

```powershell
.\packaging\Prepare-ReleaseArtifacts.ps1 -Version '1.1.1.0'
```

The command recreates:

- `artifacts\msix-signed`, containing signed x64 and ARM64 packages, a signed
  bundle, the public test certificate, checksums, and an installation script.
- `artifacts\store-submission-1.1.1.0`, containing the unsigned Store bundle,
  the current `store-listing.md`, screenshots, promotional images, and
  checksums.

Never upload files from `artifacts\msix-signed` to Partner Center. To install the
test build, open an elevated PowerShell prompt and run:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\artifacts\msix-signed\Install-TestPackage.ps1
```

Start menu and taskbar icons are cached aggressively. If a stale icon persists
after reinstalling, restart Explorer.

## Store identity

The reserved Microsoft Store identity is:

- Package/Identity/Name: `MrWyss.CopyPointerNotification`
- Package/Identity/Publisher: `CN=F7578173-2D1D-45C0-A422-4858557D8E62`
- Publisher display name: `MrWyss`
- Package family name: `MrWyss.CopyPointerNotification_ata2kafnqgxze`

These values must match the app's **Product identity** page in Partner Center.

## Build x64

```powershell
.\packaging\Build-Msix.ps1 `
  -Architecture x64 `
  -IdentityName '<Package identity name>' `
  -Publisher '<Publisher ID>' `
  -PublisherDisplayName '<Publisher display name>' `
  -Version '1.1.1.0'
```

## Build ARM64

Build the native executable with Visual Studio 2026's Host x64 to ARM64 C++ tools first:

```powershell
cmake -S . -B build-arm64 -A ARM64
cmake --build build-arm64 --config Release
```

Then package it:

```powershell
.\packaging\Build-Msix.ps1 `
  -Architecture arm64 `
  -IdentityName '<Package identity name>' `
  -Publisher '<Publisher ID>' `
  -PublisherDisplayName '<Publisher display name>' `
  -Version '1.1.1.0'
```

## Bundle

After both architecture packages exist:

```powershell
.\packaging\Build-MsixBundle.ps1 -Version '1.1.1.0'
```

Microsoft Store submission packages should remain unsigned; Partner Center signs
accepted packages. Use `-CertificateThumbprint` only when a signed package is
needed for local installation testing with a trusted development certificate.
Store submissions must use the exact Partner Center identity and monotonically
increasing four-part versions.
