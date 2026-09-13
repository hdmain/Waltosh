# Build (if needed) and run a local MinGW build. Qt DLLs must be on PATH.
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path $PSScriptRoot -Parent

$qtRoot = $env:QT_ROOT
if (-not $qtRoot) {
    if (Test-Path "C:\Qt\6.9.2\mingw_64") {
        $qtRoot = "C:\Qt\6.9.2\mingw_64"
    } else {
        Write-Error "Set QT_ROOT to your Qt mingw_64 folder, e.g. C:\Qt\6.9.2\mingw_64"
    }
}

$mingwBin = Join-Path (Split-Path (Split-Path $qtRoot -Parent) -Parent) "Tools\mingw1310_64\bin"
if (-not (Test-Path (Join-Path $mingwBin "g++.exe"))) {
    $mingwBin = "C:\Qt\Tools\mingw1310_64\bin"
}
if (-not (Test-Path (Join-Path $mingwBin "g++.exe"))) {
    Write-Error "MinGW not found. Expected Tools\mingw1310_64 next to Qt, or C:\Qt\Tools\mingw1310_64"
}

$env:PATH = "$mingwBin;$qtRoot\bin;" + $env:PATH

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error "cmake not found on PATH."
}

$buildDir = Join-Path $repoRoot "build"
$exe = Join-Path $buildDir "Waltosh.exe"

if (-not (Test-Path (Join-Path $buildDir "CMakeCache.txt"))) {
    Write-Host "Configuring CMake..."
    & cmake -S $repoRoot -B $buildDir -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$qtRoot"
    if ($LASTEXITCODE -ne 0) {
        Write-Error "CMake configure failed."
    }
}

Write-Host "Building Waltosh..."
& cmake --build $buildDir --target Waltosh -j
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed. If Waltosh.exe is running, close it and retry."
}

if (-not (Test-Path $exe)) {
    Write-Error "Build finished but Waltosh.exe was not found at $exe"
}

$process = Start-Process -FilePath $exe -WorkingDirectory $buildDir -Wait -PassThru
if ($process.ExitCode -ne 0) {
    $code = "0x{0:X8}" -f [uint32]$process.ExitCode
    Write-Error "WALTOSH exited with code $code."
}
