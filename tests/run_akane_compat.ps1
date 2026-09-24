# Run from a Visual Studio x64 developer shell. No game or NVIDIA runtime is loaded.
$ErrorActionPreference = 'Stop'
$out = Join-Path ([IO.Path]::GetTempPath()) ('akane-compat-' + [guid]::NewGuid())
New-Item -ItemType Directory $out | Out-Null
try {
    $exe = Join-Path $out 'akane_composition_policy_smoke.exe'
    & cl.exe /nologo /std:c++20 /EHsc /DNOMINMAX "/Fo$out/akane.obj" "/Fe$exe" `
        "$PSScriptRoot/akane_composition_policy_smoke.cpp"
    if ($LASTEXITCODE) { throw 'Akane policy smoke did not compile' }
    & $exe
    if ($LASTEXITCODE) { throw 'Akane policy smoke failed' }

    $warp = Join-Path $out 'dxgi_window_size_smoke.exe'
    & cl.exe /nologo /std:c++20 /EHsc /DNOMINMAX "/Fo$out/dxgi.obj" "/Fe$warp" `
        "$PSScriptRoot/dxgi_window_size_smoke.cpp" d3d11.lib dxgi.lib dcomp.lib user32.lib
    if ($LASTEXITCODE) { throw 'DXGI/WARP composition smoke did not compile' }
    & $warp
    if ($LASTEXITCODE) { throw 'DXGI/WARP composition smoke failed' }
} finally {
    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    $resolvedOutput = (Resolve-Path -LiteralPath $out).Path
    if (-not $resolvedOutput.StartsWith($temporaryRoot + 'akane-compat-',
                                       [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected test directory: $resolvedOutput"
    }
    Remove-Item -LiteralPath $resolvedOutput -Recurse -Force
}
