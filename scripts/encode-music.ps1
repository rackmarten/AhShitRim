<#
.SYNOPSIS
    Builds Sweetroll FM's tracks: reads the table in music/CREDITS.md, downloads each track's source
    into music/sources/ (git-ignored) if it isn't there yet, and encodes it to
    music/sweetrollfm/<File> as 44.1 kHz stereo xWMA, which Skyrim plays.
    Needs xWMAEncode.exe from the DirectX SDK (June 2010), found in the SDK's install folder or on
    PATH, and ffmpeg on PATH (xWMAEncode only reads WAV). -Force re-encodes tracks already there.
.EXAMPLE
    scripts/encode-music.ps1
#>
param([switch] $Force)

. "$PSScriptRoot/common.ps1"

$SdkUrl = 'https://www.microsoft.com/en-us/download/details.aspx?id=6812'

function Find-XwmaEncode {
    $candidates = @()
    if ($env:DXSDK_DIR) { $candidates += Join-Path $env:DXSDK_DIR 'Utilities/bin/x86/xWMAEncode.exe' }
    $candidates += Join-Path ${env:ProgramFiles(x86)} 'Microsoft DirectX SDK (June 2010)/Utilities/bin/x86/xWMAEncode.exe'
    $candidates += Join-Path $env:ProgramFiles 'Microsoft DirectX SDK (June 2010)/Utilities/bin/x86/xWMAEncode.exe'
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) { return $candidate }
    }
    $onPath = Get-Command xWMAEncode.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    throw "xWMAEncode.exe not found. Install the DirectX SDK (June 2010) from $SdkUrl (it lands in Utilities\bin\x86 of the SDK folder), or put xWMAEncode.exe on PATH."
}

# The tracks: the rows of music/CREDITS.md's table, by its header's column names.
function Read-Credits($path) {
    $rows = @(Get-Content -Encoding utf8 $path | Where-Object { $_ -match '^\|' } | ForEach-Object {
            , @($_.Trim().Trim('|').Split('|') | ForEach-Object { $_.Trim() })
        })
    if ($rows.Count -lt 3) { throw "No track table in $path." }
    $header = $rows[0]
    foreach ($row in $rows[2..($rows.Count - 1)]) {
        $track = @{}
        for ($i = 0; $i -lt $header.Count; $i++) { $track[$header[$i]] = $row[$i] }
        if ($track['License'] -ne 'CC0') { throw "$($track['File']) isn't CC0 in $path." }
        $track
    }
}

$xwma = Find-XwmaEncode
if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
    throw 'ffmpeg not found on PATH (xWMAEncode only reads WAV). Install it, e.g. winget install Gyan.FFmpeg.'
}

$musicDir = Join-Path $RepoRoot 'music'
$sources = Join-Path $musicDir 'sources'
$output = Join-Path $musicDir 'sweetrollfm'
New-Item -ItemType Directory -Force $sources, $output | Out-Null

foreach ($track in Read-Credits (Join-Path $musicDir 'CREDITS.md')) {
    $file = $track['File']
    if ($file -notmatch '^\d\d-[a-z0-9-]+\.xwm$') { throw "Bad file name $file in music/CREDITS.md (NN-slug.xwm)." }
    $xwm = Join-Path $output $file
    if ((Test-Path $xwm) -and -not $Force) { Write-Host "$file is there"; continue }

    $url = $track['Download']
    $source = Join-Path $sources ([IO.Path]::GetFileNameWithoutExtension($file) + [IO.Path]::GetExtension(([Uri]$url).AbsolutePath))
    if (-not (Test-Path $source)) {
        Write-Host "Downloading $($track['Title']) by $($track['Artist'])"
        Invoke-WebRequest -Uri $url -OutFile $source -UseBasicParsing
    }

    $wav = Join-Path ([IO.Path]::GetTempPath()) ([IO.Path]::GetFileNameWithoutExtension($file) + '.wav')
    try {
        Invoke-Checked 'ffmpeg' @('-hide_banner', '-loglevel', 'error', '-y', '-i', $source, '-ar', '44100', '-ac', '2', '-sample_fmt', 's16', $wav)
        Invoke-Checked $xwma @('-b', '160000', $wav, $xwm)
    } finally {
        Remove-Item -ErrorAction SilentlyContinue $wav
    }
    Write-Host "Encoded $file"
}
Write-Host "Done: $output"
