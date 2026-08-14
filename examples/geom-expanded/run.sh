#!/usr/bin/env bash
# Build and run the csharp example end-to-end, from this directory.
#
#   ./run.sh
#
# Stages (each is skipped if its output already exists, so re-runs are quick):
#   1. rosetta_gen + the generator driver  -> bindings/csharp/  (needs clang-p2996)
#   2. cmake build of the native shim      -> libcsgeom.{dylib,so}        (stock C++20)
#   3. dotnet run of run/run.csproj (csgeom.cs + example_csharp.cs), with the
#      native library on the loader path, targeting your installed SDK.
set -euo pipefail
cd "$(dirname "$0")"

# --- dotnet on PATH (the macOS pkg installs here but doesn't always export it) ---
command -v dotnet >/dev/null 2>&1 || export PATH="/usr/local/share/dotnet:$PATH"
if ! command -v dotnet >/dev/null 2>&1; then
    echo "error: dotnet not found (install the .NET SDK, or add it to PATH)" >&2
    exit 1
fi

# --- 1. generate the binding (clang-p2996) ---
if [ ! -f bindings/csharp/csgeom.cs ]; then
    echo ">> generating binding from manifest.json"
    ../../bin/rosetta_gen manifest.json gen
    cmake -S gen -B gen/build
    cmake --build gen/build -j
    ./generator bindings
fi

# --- 2. native shim (stock C++20) ---
echo ">> building native library"
cmake -S bindings/csharp -B bindings/csharp/build
cmake --build bindings/csharp/build -j

# --- 3. run, targeting the installed SDK and pointing the loader at the lib ---
TF="net$(dotnet --version | cut -d. -f1).0"
LIBDIR="$PWD/bindings/csharp/build"
case "$(uname -s)" in
    Darwin) export DYLD_LIBRARY_PATH="$LIBDIR${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" ;;
    *)      export LD_LIBRARY_PATH="$LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ;;
esac
echo ">> dotnet run (TargetFramework=$TF)"
exec dotnet run --project run -c Release -p:DemoTF="$TF"
