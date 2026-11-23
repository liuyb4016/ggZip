# ggZip portable package

This package contains the GUI, CLI, classic Explorer extension, Qt libraries,
and the x64 Visual C++ runtime. It supports x64 Windows 10 version 1809 or
later and Windows 11.
The package includes the unmodified 7-Zip 26.06 engine and SFX modules under
`7z/`. No separate 7-Zip installation, environment configuration, or first-run
download is required.

Run `ggzip_gui.exe` to browse archives. Run `ggz.exe --help` for CLI commands.
Keep the executable files, DLLs, plugin directories, and `7z/` folder together.
`GGZIP_7Z` is an optional advanced override for a custom engine.

7-Zip's original distribution notice, GNU LGPL 2.1 text and unRAR license
are under `licenses/7zip/`; its complete corresponding source archive is
included under `sources/7zip/`. See `THIRD_PARTY_NOTICES.md`.

## Optional integration

Use **设置 → 文件关联** in the GUI to register ggZip as an Open-with candidate.
Registration is per-user and does not change existing default applications.
Choose ggZip yourself in Windows Settings → Apps → Default apps to enable
double-click opening and the ggZip archive icon.

For scripted registration, run from PowerShell:

```powershell
Start-Process .\ggzip_gui.exe -ArgumentList '--register-associations --quiet' -Wait
```

For the classic Explorer menu, run `regsvr32` with the full DLL path:

```powershell
regsvr32 "$PWD\ggzip_shell.dll"
```

On Windows 11 the classic menu is under **Show more options**.
To remove integration before deleting or moving this folder:

```powershell
Start-Process .\ggzip_gui.exe -ArgumentList '--unregister-associations --quiet' -Wait
regsvr32 /u "$PWD\ggzip_shell.dll"
```

Association removal only removes the registration owned by this executable path.
If another ggZip installation has taken over, its association is preserved.
If ggZip was selected as the default app, choose another default app after removal.
Passwords stored under `%APPDATA%\ggZip` are not deleted.
