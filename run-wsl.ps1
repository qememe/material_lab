$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    wsl.exe -- bash -lc 'cd ~/sim_destroy && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j 6 && ctest --test-dir build --output-on-failure && ./build/material_lab'
    if ($LASTEXITCODE -ne 0) { throw "WSL build or application failed ($LASTEXITCODE)." }
} finally { Pop-Location }
