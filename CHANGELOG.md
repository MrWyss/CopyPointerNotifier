# Changelog

All notable changes to Copy Pointer Notifier are documented here.

## 1.1.1

### Added

- Added an About section to Settings.
- Made the displayed version a link to the GitHub repository.
- Added release-channel identification for Microsoft Store, self-signed test,
  and unpackaged development builds.

### Improved

- Reduced per-move processing for smoother pointer tracking.
- Rebuilt application icons for sharper rendering at each Windows scale.
- Added transparent, unplated icon variants for the taskbar and Start menu.

### Packaging

- Corrected the Microsoft Store package identity.
- Added repeatable generation of signed test packages and unsigned Store
  submission artifacts.
- Added architecture and signature validation for x64 and ARM64 packages.

## 1.1.0

### Added

- Added ordered clipboard-format rules with custom one- or two-character
  indicator glyphs.
- Added clipboard-format capture, rule reordering, disabling, and previews.
- Redesigned Settings with Appearance, Rules, and Advanced pages.
