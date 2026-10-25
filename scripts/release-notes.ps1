<#
.SYNOPSIS
    Writes a release's SHA256SUMS.txt (the zip and every file in it) next to the zip, and its release
    notes: the version's CHANGELOG.md section, then how to check the download: the zip's SHA-256, the
    build provenance attestation (-Attested), the VirusTotal report (scripts/virustotal.ps1's JSON)
    and the two files setup downloads, with their own VirusTotal reports. The release workflow
    (.github/workflows/release.yml) runs it after scripts/package.ps1.
.EXAMPLE
    scripts/release-notes.ps1 -Zip out/release/AhShitRim-0.1.0.zip -VirusTotal out/release/virustotal.json -Attested
#>
param(
    [Parameter(Mandatory)] [string] $Zip,
    [string] $VirusTotal,
    [switch] $Attested,
    [string] $Out
)

. "$PSScriptRoot/common.ps1"
. (Join-Path $RepoRoot 'launcher/install.ps1')
$ErrorActionPreference = 'Stop'

$zipName = Split-Path -Leaf $Zip
if ($zipName -notmatch '^AhShitRim-(.+)\.zip$') { throw "$zipName isn't a release zip (AhShitRim-<version>.zip)." }
$version = $Matches[1]
$dir = Split-Path -Parent (Resolve-Path -LiteralPath $Zip).Path
if (-not $Out) { $Out = Join-Path $dir 'release-notes.md' }
$repo = if ($env:GITHUB_REPOSITORY) { $env:GITHUB_REPOSITORY } else { 'rackmarten/AhShitRim' }

# SHA256SUMS.txt in sha256sum's format: the zip, then every file inside it by its path in the zip.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zipHash = (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant()
$sums = @("$zipHash  $zipName")
$archive = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Zip).Path)
try {
    foreach ($entry in $archive.Entries | Where-Object { $_.Name } | Sort-Object FullName) {
        $stream = $entry.Open()
        try { $sums += "$((Get-FileHash -InputStream $stream -Algorithm SHA256).Hash.ToLowerInvariant())  $($entry.FullName)" } finally { $stream.Dispose() }
    }
} finally { $archive.Dispose() }
[IO.File]::WriteAllLines((Join-Path $dir 'SHA256SUMS.txt'), [string[]] $sums, (New-Object Text.UTF8Encoding $false))

# The version's section of CHANGELOG.md, without its heading.
$changelog = Get-Content -Raw (Join-Path $RepoRoot 'CHANGELOG.md')
$pattern = '(?ms)^## \[' + [regex]::Escape($version) + '\][^\n]*\n(.*?)(?=^## \[|\z)'
$section = if ($changelog -match $pattern) { $Matches[1].Trim() } else { "See [CHANGELOG.md](https://github.com/$repo/blob/v$version/CHANGELOG.md)." }

$lines = @($section, '', '## Check your download', '')
$lines += "- **SHA-256** of ``$zipName``: ``$zipHash``. In PowerShell: ``Get-FileHash $zipName``. Every file in it is listed in ``SHA256SUMS.txt``."
if ($Attested) {
    $run = if ($env:GITHUB_RUN_ID) { " ([this run]($env:GITHUB_SERVER_URL/$repo/actions/runs/$env:GITHUB_RUN_ID))" } else { '' }
    $lines += "- **Built by GitHub Actions** from tag ``v$version``$run, with a signed build provenance attestation. ``gh attestation verify $zipName --repo $repo`` checks that this zip came from this repository's release workflow at that commit."
}
if ($VirusTotal -and (Test-Path -LiteralPath $VirusTotal)) {
    $scan = Get-Content -Raw -LiteralPath $VirusTotal | ConvertFrom-Json
    if ($scan.status -eq 'completed') {
        $flagged = [int] $scan.stats.malicious + [int] $scan.stats.suspicious
        $engines = $flagged + [int] $scan.stats.undetected + [int] $scan.stats.harmless
        $lines += if ($flagged -eq 0) {
            "- **VirusTotal**: [no engine flags it]($($scan.url)) (0 of $engines)."
        } else {
            "- **VirusTotal**: [$flagged of $engines engines flag it]($($scan.url)). AhShitRim's plugins hook into both games' code, which a few heuristic engines mistake for malware; every file is built from this repository's source, which anyone can read and build."
        }
    } elseif ($scan.status -eq 'queued') {
        $lines += "- **VirusTotal**: the scan was still running when this was written: [its report]($($scan.url))."
    }
}
$lines += '- **Setup downloads** two files from their projects'' own GitHub releases and checks them by SHA-256 before running anything:'
foreach ($download in $PinnedDownloads.Values | Sort-Object Name) {
    $lines += "  - [$($download.Name)]($($download.Url)), SHA-256 ``$($download.Sha256)``: [VirusTotal]($(Get-VirusTotalUrl $download.Sha256))"
}
[IO.File]::WriteAllLines($Out, [string[]] $lines, (New-Object Text.UTF8Encoding $false))
Write-Host "Wrote $(Join-Path $dir 'SHA256SUMS.txt') and $Out"
