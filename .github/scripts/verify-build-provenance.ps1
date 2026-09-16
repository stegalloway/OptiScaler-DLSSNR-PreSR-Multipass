param(
    [string]$Root = $env:GITHUB_WORKSPACE
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = (Get-Location).Path
}

$expected = $env:OPTISCALER_BUILD_SHORT_SHA

if ([string]::IsNullOrWhiteSpace($expected)) {
    throw "OPTISCALER_BUILD_SHORT_SHA is not set."
}

$expected = $expected.Trim().ToLowerInvariant()
$needle = "($expected)"

Write-Host "Expected embedded OptiScaler commit: $needle"

$candidates = @(
    Get-ChildItem `
        -LiteralPath $Root `
        -Filter "OptiScaler.dll" `
        -File `
        -Recurse `
        -ErrorAction SilentlyContinue |
    Where-Object {
        $_.FullName -notmatch '\\external\\' -and
        $_.FullName -notmatch '\\ZIP-BACKUP\\' -and
        $_.FullName -notmatch '\\packages\\'
    } |
    Sort-Object LastWriteTime -Descending
)

if ($candidates.Count -eq 0) {
    throw "No built OptiScaler.dll files were found."
}

$matched = @()

foreach ($dll in $candidates) {

    Write-Host "Checking: $($dll.FullName)"

    $bytes = [IO.File]::ReadAllBytes($dll.FullName)

    $ascii =
        [Text.Encoding]::ASCII.
        GetString($bytes).
        ToLowerInvariant()

    $unicode =
        [Text.Encoding]::Unicode.
        GetString($bytes).
        ToLowerInvariant()

    if (
        $ascii.Contains($needle) -or
        $unicode.Contains($needle)
    ) {
        $matched += $dll

        $hash = (
            Get-FileHash `
                -LiteralPath $dll.FullName `
                -Algorithm SHA256
        ).Hash

        Write-Host "PROVENANCE VERIFIED"
        Write-Host "Binary : $($dll.FullName)"
        Write-Host "Commit : $expected"
        Write-Host "SHA256 : $hash"
    }
}

if ($matched.Count -eq 0) {
    throw @"
No generated OptiScaler.dll contains the expected commit marker:

$needle

The workflow will NOT package or upload this build.
"@
}

Write-Host ""
Write-Host "Binary provenance validation passed."