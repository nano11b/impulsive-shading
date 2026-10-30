$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$base = 'https://github.com/nano11b/impulsive-shading/releases/latest/download'
$archive = 'Impulsive-Shading-Benchmark-Win64.zip'
$tempDir = Join-Path $env:TEMP ('impulsive-update-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $tempDir | Out-Null
    $versionPath = Join-Path $tempDir 'version.txt'
    try { Invoke-WebRequest "$base/version.txt" -OutFile $versionPath -MaximumRedirection 10 }
    catch { Write-Warning 'Update check unavailable; using installed version.'; return }
    $version = (Get-Content -Raw $versionPath).Trim()
    $installed = Join-Path $root 'version.txt'
    if ((Test-Path $installed) -and (Get-Content -Raw $installed).Trim() -eq $version) { return }
    $zip = Join-Path $tempDir $archive
    Invoke-WebRequest "$base/$archive" -OutFile $zip -MaximumRedirection 10
    $hashFile = Join-Path $tempDir "$archive.sha256"
    Invoke-WebRequest "$base/$archive.sha256" -OutFile $hashFile -MaximumRedirection 10
    $expected = ((Get-Content -Raw $hashFile) -split '\s+')[0].ToLowerInvariant()
    $actual = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $expected) { throw 'Update checksum mismatch.' }
    $unpacked = Join-Path $tempDir 'unpacked'
    Expand-Archive -LiteralPath $zip -DestinationPath $unpacked
    foreach ($name in 'Impulsive Shading.exe', 'Run Benchmark.ps1', 'Start Benchmark.cmd', 'Update-Benchmark.ps1', 'version.txt') {
        if (-not (Test-Path (Join-Path $unpacked $name))) { throw "Incomplete update: $name" }
    }
    Get-ChildItem -LiteralPath $unpacked -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $root $_.Name) -Force
    }
    Write-Host "Updated to $version"
} finally {
    if (Test-Path $tempDir) { Remove-Item -LiteralPath $tempDir -Recurse -Force }
}
