# MSIX packaging

The package contains the native tray process and the native C++/WinRT WinUI Settings process. MSIX builds use the shared Windows App Runtime 1.8 framework package without embedding any managed runtime.

The packaged app uses the manifest-declared Windows startup task. Unpackaged builds continue to use the current-user `Run` registry entry.

The Microsoft Store installs the Windows App Runtime framework dependency declared by the package. No .NET runtime is required.

With version 1.1.0.0 and the native Settings implementation, each architecture package is approximately 0.5 MiB and the combined x64/ARM64 bundle is approximately 1 MiB.

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
plate.

## Store identity

Copy the following values from the app's **Product identity** page in Partner Center and pass them to each architecture build:

- Package/Identity/Name
- Package/Identity/Publisher
- Publisher display name

The manifest defaults are suitable only for inspecting an unsigned local package; they do not represent the reserved Store identity.

## Build x64

```powershell
.\packaging\Build-Msix.ps1 `
  -Architecture x64 `
  -IdentityName '<Package identity name>' `
  -Publisher '<Publisher ID>' `
  -PublisherDisplayName '<Publisher display name>' `
  -Version '1.1.0.0'
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
  -Version '1.1.0.0'
```

## Bundle

After both architecture packages exist:

```powershell
.\packaging\Build-MsixBundle.ps1 -Version '1.1.0.0'
```

Microsoft Store submission packages should remain unsigned; Partner Center signs
accepted packages. Use `-CertificateThumbprint` only when a signed package is
needed for local installation testing with a trusted development certificate.
Store submissions must use the exact Partner Center identity and monotonically
increasing four-part versions.
