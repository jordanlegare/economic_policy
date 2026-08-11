# Windows standalone executable

The Windows build produces a self-contained x64 executable named:

`Canada-US-Diplomatic-Policy-Studio.exe`

The browser UI (`web/`) and `data/calibration/current.snapshot.csv` are embedded at build time. The executable can therefore be copied to an otherwise empty directory and launched without carrying the repository beside it.

Writable negotiation state is intentionally **not** embedded. On Windows it is stored under:

`%LOCALAPPDATA%\CanadaPolicyStudio\`

That directory contains the materialized calibration bootstrap and the local append-only Diplomat Room event log. It is still a research workflow location, not an accredited store for protected or classified material.

## Build with Visual Studio / MSVC

From a Developer PowerShell or ordinary PowerShell with Visual Studio Build Tools installed:

```powershell
cmake -S . -B build-windows -A x64
cmake --build build-windows --config Release --parallel
ctest --test-dir build-windows -C Release --output-on-failure
```

The executable is written to:

```text
build-windows\Release\Canada-US-Diplomatic-Policy-Studio.exe
```

MSVC builds use the static C/C++ runtime (`/MT` in Release), so the downloaded executable does not require a separately installed Visual C++ redistributable for this project.

## Cross-compile from Linux with MinGW-w64

Install MinGW-w64, then configure a separate build tree:

```bash
sudo apt update
sudo apt install mingw-w64 python3 cmake

cmake -S . -B build-mingw \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++

cmake --build build-mingw --parallel
```

For MinGW builds, CMake adds `-static -static-libgcc -static-libstdc++` to the application link step. The resulting file is normally:

```text
build-mingw/Canada-US-Diplomatic-Policy-Studio.exe
```

You can verify it from Linux with:

```bash
file build-mingw/Canada-US-Diplomatic-Policy-Studio.exe
```

## Launch behavior

Double-clicking the Windows executable starts the local server on port 8080 and opens the default browser at:

`http://localhost:8080`

Command-line options:

```text
Canada-US-Diplomatic-Policy-Studio.exe [port] [--no-browser] [--browser] [--bind-all]
```

Examples:

```powershell
.\Canada-US-Diplomatic-Policy-Studio.exe
.\Canada-US-Diplomatic-Policy-Studio.exe 9090
.\Canada-US-Diplomatic-Policy-Studio.exe 8080 --no-browser
```

`--bind-all` exposes the HTTP listener on all network interfaces. Do not use it casually: the server has no production authentication or transport-security layer.

## GitHub Actions artifact

The `Windows standalone executable` workflow builds and tests the x64 Release executable on `windows-latest`. Its smoke test deliberately launches the `.exe` from an empty directory and verifies that:

- the embedded index and JavaScript assets are served;
- the embedded calibration snapshot loads;
- a full policy evaluation returns the 5,000-draw robustness layer;
- no repository `web/` or calibration file was copied into the standalone test directory.

The workflow uploads:

- `Canada-US-Diplomatic-Policy-Studio.exe`
- `Canada-US-Diplomatic-Policy-Studio.exe.sha256`

under the artifact name `Canada-US-Diplomatic-Policy-Studio-windows-x64`.
