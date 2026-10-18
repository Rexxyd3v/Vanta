# Build the Windows installer

Use the MSYS2 UCRT64 dependencies listed in the root README, plus **Inno Setup 6** (ISCC.exe).

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
cmake -S . -B bin/release-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build bin/release-build -j 4
ctest --test-dir bin/release-build --output-on-failure
./tools/package_windows.ps1 -Version 1.0.0
```

Pass `-Compiler 'C:\path\to\ISCC.exe'` if the compiler is not in a standard installation directory. Pass `-MsysBin` if MSYS2 is installed elsewhere.

Output: `bin/release/1.0.0/VANTA-1.0.0-Setup.exe`, `SHA256SUMS.txt`, and a local build manifest. The package script stages a fresh payload, includes all assets, resolves runtime DLL dependencies recursively, and embeds the Windows icon and version information. It does not include local player settings, development tools, screenshots, or review videos.

The installer defaults to `%LOCALAPPDATA%\Programs\VANTA`, creates a Start menu shortcut with the correct working directory, and offers a desktop shortcut. It runs without administrator access. Settings remain in the writable game folder and are preserved on uninstall.

The GitHub workflow builds this version on a `v1.0.0` tag push, or produces an installer artifact on manual dispatch. It preserves any already published assets. Before a later release, update the project version, Windows version resource, and workflow version together.

For an explicitly authorized local publication, the publishing helper verifies the remote tag and local build manifest against the tested source commit, uploads the installer and checksum to a draft, then publishes only with `--publish`. It uses the existing Git Credential Manager sign-in; credentials are never written into this repository.
