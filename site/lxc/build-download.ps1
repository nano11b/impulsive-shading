$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$exe = Join-Path $workspace 'build/Release/Impulsive Shading.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "Release executable not found: $exe" }

$stage = Join-Path $workspace ('build/benchmark-package-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $stage 'Impulsive Shading.exe')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'public-run-benchmark.ps1') -Destination (Join-Path $stage 'Run Benchmark.ps1')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Update-Benchmark-Windows.ps1') -Destination (Join-Path $stage 'Update-Benchmark.ps1')
Set-Content -LiteralPath (Join-Path $stage 'version.txt') -Value $(if ($env:GITHUB_REF_NAME) { $env:GITHUB_REF_NAME } else { 'local' }) -NoNewline
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Start Benchmark.cmd') -Destination $stage
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README-download.txt') -Destination $stage

# Ship the x64 MSVC runtime DLLs beside the EXE for clean Windows machines.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio runtime location could not be found.' }
$vsInstall = (& $vswhere -latest -property installationPath).Trim()
$redistRoot = Join-Path $vsInstall 'VC/Redist/MSVC'
$runtime = Get-ChildItem -LiteralPath $redistRoot -Directory |
    Sort-Object Name -Descending |
    ForEach-Object { Get-ChildItem -LiteralPath (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue } |
    Select-Object -First 1
if (-not $runtime) { throw 'x64 Visual C++ runtime was not found.' }
foreach ($dll in 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    $source = Join-Path $runtime.FullName $dll
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing runtime file: $source" }
    Copy-Item -LiteralPath $source -Destination $stage
}

$archive = Join-Path $PSScriptRoot 'Impulsive-Shading-Benchmark-Win64.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -CompressionLevel Optimal -Force
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath (Join-Path $PSScriptRoot 'download.sha256') -Value $hash -NoNewline
Write-Host "Built $archive"
Write-Host "SHA256 $hash"
