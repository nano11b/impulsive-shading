param(
    [double]$Seconds = 3,
    [double]$Warmup = 1,
    [string]$SiteUrl = 'https://marks.seven-labs.com',
    [switch]$SkipUpload
)

$ErrorActionPreference = 'Stop'
if ($Seconds -lt 2 -or $Seconds -gt 300) { throw 'Seconds must be between 2 and 300.' }
if ($Warmup -lt 0.5 -or $Warmup -gt 60) { throw 'Warmup must be between 0.5 and 60.' }

$exe = Join-Path $PSScriptRoot 'Impulsive Shading.exe'
$report = Join-Path $PSScriptRoot 'benchmark-result.json'
if (-not (Test-Path -LiteralPath $exe)) { throw 'Impulsive Shading.exe is missing from this folder.' }
if ($env:IMPULSIVE_SKIP_UPDATE -ne '1') {
    $updater = Join-Path $PSScriptRoot 'Update-Benchmark.ps1'
    if (Test-Path $updater) {
        try { & $updater } catch { Write-Warning "Update failed; using installed version: $_" }
    }
}

Write-Host 'Running all 21 shaders at 1280 x 720 with VSync off. Keep the benchmark window visible.'
& $exe --benchmark-all --benchmark $report --benchmark-seconds $Seconds.ToString([cultureinfo]::InvariantCulture) --benchmark-warmup $Warmup.ToString([cultureinfo]::InvariantCulture)
if ($LASTEXITCODE -ne 0) { throw 'The benchmark failed. No results were submitted.' }

if ($SkipUpload) {
    Write-Host "Results saved to $report"
    return
}

$uri = $SiteUrl.TrimEnd('/') + '/api/community-reports'
$body = Get-Content -LiteralPath $report -Raw
try {
    $response = Invoke-RestMethod -Uri $uri -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 30
    Write-Host "Results submitted: $($response.id)"
    Write-Host "View them at $SiteUrl"
} catch {
    Write-Warning "Upload failed. Your results remain at $report. $($_.Exception.Message)"
    throw
}
