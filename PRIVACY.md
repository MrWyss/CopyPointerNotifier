# Privacy Policy for Copy Pointer Notifier

**Last updated: 23 August 2026**

Copy Pointer Notifier ("the app") is a Windows tray utility that displays a
brief on‑screen indicator near your pointer whenever you copy something to the
clipboard. This policy explains what the app does and does not do with your
information.

## Summary

**Copy Pointer Notifier does not collect, transmit, or share any personal data.**
Everything the app uses stays on your device. The app has no accounts, no
servers, no telemetry, no advertising, and makes no network connections.

## What the app accesses

- **Clipboard change events and format types only.** To decide which indicator
  to show (for example: text, rich text, image, files, or other content), the
  app checks the *type* of data on the clipboard. It does **not** read, copy,
  store, or transmit the actual contents of your clipboard.

## What the app stores, and where

All data is stored locally on your own computer. Nothing is uploaded anywhere.

- **Your settings** (such as indicator position, size, color, animation, and
  visibility duration) are saved in the Windows Registry under
  `HKEY_CURRENT_USER\Software\MrWyss\CopyPointerNotifier`.
- **Optional "start with Windows" setting**, if you enable it, is written to the
  standard Windows startup location
  (`HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`).
- **Backup files.** If you choose "Back up…" in Settings, the app writes your
  settings to a JSON file at a location you select. If you choose "Restore…",
  the app reads a JSON file you select. These files are created and used only at
  your request and remain entirely under your control.

## What the app does not do

- It does **not** send any data over the internet.
- It does **not** use analytics, tracking, telemetry, or advertising.
- It does **not** collect personal information, usage statistics, or the
  contents of your clipboard, documents, or files.
- It does **not** share data with any third parties.

## Permissions

Copy Pointer Notifier runs as a full‑trust desktop application so it can monitor
clipboard change notifications and draw its indicator overlay on the desktop.
These capabilities are used solely for the app's stated function and not for
collecting information.

## Data retention and removal

Because all data is local, you remain in full control:

- Removing your settings: uninstall the app, or delete the registry key
  `HKEY_CURRENT_USER\Software\MrWyss\CopyPointerNotifier`.
- Removing the startup entry: turn off "start with Windows" in Settings, or
  delete the corresponding value under the Windows `Run` key.
- Removing backups: delete any JSON backup files you created.

## Children's privacy

The app does not collect any data from anyone, including children.

## Changes to this policy

If this policy changes, the updated version will be published with the app. The
"Last updated" date above indicates the most recent revision.

## Contact

For questions about this privacy policy, please use the support contact
information listed on the app's Microsoft Store page.
