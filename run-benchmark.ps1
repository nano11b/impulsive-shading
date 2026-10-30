param(
    [string]$Shader,
    [switch]$All,
    [double]$Seconds = 3,
    [double]$Warmup = 1,
    [string]$SiteUrl = 'https://marks.seven-labs.com',
    [string]$Output = (Join-Path $PSScriptRoot 'build/benchmark-latest.json'),
    [switch]$SkipUpload
)

$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'build/Release/Impulsive Shading.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Application not found at $exe. Build the Release target first."
}
if (-not $SkipUpload) {
    $token = [Environment]::GetEnvironmentVariable('IMPULSIVE_MARKS_TOKEN')
    if ([string]::IsNullOrWhiteSpace($token)) {
        throw "Set IMPULSIVE_MARKS_TOKEN or use -SkipUpload."
    }
}

$arguments = @('--benchmark', $Output, '--benchmark-seconds', $Seconds.ToString([cultureinfo]::InvariantCulture),
    '--benchmark-warmup', $Warmup.ToString([cultureinfo]::InvariantCulture))
if ($All) { $arguments += '--benchmark-all' }
if ($Shader) { $arguments += @('--shader', $Shader) }

& $exe @arguments
if ($LASTEXITCODE -ne 0) { throw "Benchmark failed with exit code $LASTEXITCODE. The report was not uploaded." }
if ($SkipUpload) { Write-Host "Benchmark saved to $Output"; return }

$uri = $SiteUrl.TrimEnd('/') + '/api/reports'
$body = Get-Content -LiteralPath $Output -Raw
$response = Invoke-RestMethod -Uri $uri -Method Post -ContentType 'application/json' -Headers @{
    'Authorization' = "Bearer $token"
} -Body $body
Write-Host "Uploaded benchmark $($response.id) to $SiteUrl"
