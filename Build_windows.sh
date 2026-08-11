#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build-windows"
OUTPUT_DIR="${ROOT_DIR}/dist-windows"

EXE_NAME="Canada-US-Diplomatic-Policy-Studio.exe"

echo "==> Canada-U.S. Diplomatic Policy Studio"
echo "==> Building Windows x64 executable from Debian"
echo

# -------------------------------------------------------------------
# Dependencies
# -------------------------------------------------------------------

required_commands=(
    cmake
    python3
    x86_64-w64-mingw32-gcc
    x86_64-w64-mingw32-g++
)

missing=()

for cmd in "${required_commands[@]}"; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        missing+=("$cmd")
    fi
done

if (( ${#missing[@]} > 0 )); then
    echo "Missing build dependencies:"
    printf '  - %s\n' "${missing[@]}"
    echo
    echo "Install them with:"
    echo
    echo "  sudo apt update"
    echo "  sudo apt install -y \\"
    echo "      build-essential \\"
    echo "      cmake \\"
    echo "      ninja-build \\"
    echo "      python3 \\"
    echo "      mingw-w64"
    echo
    exit 1
fi

# -------------------------------------------------------------------
# Clean build
# -------------------------------------------------------------------

echo "==> Cleaning previous Windows build"
rm -rf "${BUILD_DIR}"
rm -rf "${OUTPUT_DIR}"

mkdir -p "${BUILD_DIR}"
mkdir -p "${OUTPUT_DIR}"

# -------------------------------------------------------------------
# Configure
# -------------------------------------------------------------------

echo "==> Configuring MinGW-w64 Release build"

cmake \
    -S "${ROOT_DIR}" \
    -B "${BUILD_DIR}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres

# -------------------------------------------------------------------
# Build
# -------------------------------------------------------------------

echo "==> Building"

cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

# -------------------------------------------------------------------
# Locate executable
# -------------------------------------------------------------------

EXE_PATH="$(find "${BUILD_DIR}" \
    -type f \
    -name "${EXE_NAME}" \
    -print \
    -quit)"

if [[ -z "${EXE_PATH}" ]]; then
    echo "ERROR: Windows executable was not produced."
    echo
    echo "Executables found:"
    find "${BUILD_DIR}" -type f -iname '*.exe' -print || true
    exit 1
fi

# -------------------------------------------------------------------
# Copy distribution artifact
# -------------------------------------------------------------------

cp "${EXE_PATH}" "${OUTPUT_DIR}/${EXE_NAME}"

# -------------------------------------------------------------------
# Inspect binary
# -------------------------------------------------------------------

echo
echo "==> Windows binary:"
file "${OUTPUT_DIR}/${EXE_NAME}"

# Check imports when objdump is available.
if command -v x86_64-w64-mingw32-objdump >/dev/null 2>&1; then
    echo
    echo "==> Imported DLLs:"
    x86_64-w64-mingw32-objdump \
        -p "${OUTPUT_DIR}/${EXE_NAME}" \
        | grep 'DLL Name:' \
        || true
fi

# -------------------------------------------------------------------
# SHA-256
# -------------------------------------------------------------------

echo
echo "==> Generating SHA-256 checksum"

(
    cd "${OUTPUT_DIR}"
    sha256sum "${EXE_NAME}" > "${EXE_NAME}.sha256"
)

echo
echo "============================================================"
echo " BUILD COMPLETE"
echo "============================================================"
echo
echo "Executable:"
echo "  ${OUTPUT_DIR}/${EXE_NAME}"
echo
echo "Checksum:"
echo "  ${OUTPUT_DIR}/${EXE_NAME}.sha256"
echo
echo "SHA-256:"
cat "${OUTPUT_DIR}/${EXE_NAME}.sha256"
echo
echo "Copy the .exe to a Windows x64 machine and run it."
echo "It should open the policy studio in the default browser."
echo
