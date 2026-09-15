param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path $SourceRoot).Path
$path = Join-Path $root 'OptiScaler/hooks/DxgiFactory_Hooks.cpp'
$text = [IO.File]::ReadAllText($path).Replace("`r`n", "`n")
$marker = 'Akane compatibility: DirectComposition passthrough'

if ($text.Contains($marker)) {
    Write-Host 'Akane DirectComposition passthrough is already present upstream.'
    return
}

$old = @'
    const bool passThrough = State::Instance().vulkanCreatingSC || _skipFGSwapChainCreation ||
                             pDevice == nullptr || pDesc == nullptr || ppSwapChain == nullptr ||
                             pDesc->Width < 100 || pDesc->Height < 100;
'@.TrimEnd("`r","`n")

$new = @'
    const bool akaneComposition =
        pDesc != nullptr &&
        pDesc->Width == 720 &&
        pDesc->Height == 1000 &&
        _wcsicmp(Util::ExePath().filename().c_str(), L"tlou-ii.exe") == 0;

    const bool passThrough = State::Instance().vulkanCreatingSC || _skipFGSwapChainCreation ||
                             pDevice == nullptr || pDesc == nullptr || ppSwapChain == nullptr ||
                             pDesc->Width < 100 || pDesc->Height < 100 || akaneComposition;

    if (akaneComposition)
        LOG_INFO("Akane compatibility: DirectComposition passthrough {}x{}", pDesc->Width, pDesc->Height);
'@.TrimEnd("`r","`n")

$first = $text.IndexOf($old, [StringComparison]::Ordinal)
if ($first -lt 0) {
    throw 'Could not locate the v0.8.4-style CreateSwapChainForComposition passthrough expression. Upstream changed; refusing to guess.'
}
$second = $text.IndexOf($old, $first + $old.Length, [StringComparison]::Ordinal)
if ($second -ge 0) {
    throw 'CreateSwapChainForComposition passthrough expression matched more than once. Refusing an ambiguous Akane patch.'
}

$text = $text.Substring(0, $first) + $new + $text.Substring($first + $old.Length)
[IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))

if (-not ([IO.File]::ReadAllText($path)).Contains($marker)) {
    throw 'Akane patch verification failed.'
}
Write-Host 'TLOU2 Akane DirectComposition compatibility patch applied.'
