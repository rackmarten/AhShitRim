<#
.SYNOPSIS
    Uploads a file to VirusTotal (API v3), waits for its scan and writes the result as JSON:
    sha256, the report's url, status ("completed", "queued" when it took longer than -WaitMinutes,
    "skipped" without an API key) and the engines' stats (malicious, suspicious, undetected...).
    The API key comes from VT_API_KEY (a free VirusTotal account's key; the release workflow passes
    the repository secret of that name). VirusTotal shares what it's sent with the security
    community, so only send what's public anyway, such as a release zip.
    PowerShell 7 (Invoke-RestMethod -Form).
.EXAMPLE
    $env:VT_API_KEY = '<key>'; scripts/virustotal.ps1 -Path out/release/AhShitRim-0.1.0.zip -Out out/release/virustotal.json
#>
param(
    [Parameter(Mandatory)] [string] $Path,
    [Parameter(Mandatory)] [string] $Out,
    [int] $WaitMinutes = 20
)

$ErrorActionPreference = 'Stop'
$sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
$result = [ordered] @{ file = Split-Path -Leaf $Path; sha256 = $sha256; url = "https://www.virustotal.com/gui/file/$sha256"; status = 'skipped'; stats = $null }

function Save-Result {
    $result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $Out -Encoding utf8
    Write-Host "VirusTotal: $($result.status), $($result.url)"
}

if (-not $env:VT_API_KEY) {
    Write-Host '::warning::No VT_API_KEY, so the release zip was not scanned on VirusTotal. Add the repository secret VT_API_KEY (docs/DEV_SETUP.md, "Cutting a release").'
    Save-Result
    return
}

$api = 'https://www.virustotal.com/api/v3'
$headers = @{ 'x-apikey' = $env:VT_API_KEY }
# Files over 32 MB go to an upload URL of their own; the free API allows 4 requests a minute.
$uploadUrl = if ((Get-Item -LiteralPath $Path).Length -gt 32MB) { (Invoke-RestMethod -Headers $headers "$api/files/upload_url").data } else { "$api/files" }
$analysis = (Invoke-RestMethod -Method Post -Uri $uploadUrl -Headers $headers -Form @{ file = Get-Item -LiteralPath $Path }).data.id
Write-Host "Uploaded $($result.file) to VirusTotal (analysis $analysis), waiting for the scan"

$result.status = 'queued'
$deadline = (Get-Date).AddMinutes($WaitMinutes)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 30
    $attributes = (Invoke-RestMethod -Headers $headers "$api/analyses/$analysis").data.attributes
    if ($attributes.status -eq 'completed') {
        $result.status = 'completed'
        $result.stats = $attributes.stats
        break
    }
}
Save-Result
