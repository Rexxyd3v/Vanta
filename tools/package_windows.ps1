param(
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version = '1.0.0',
    [string]$BuildDir = 'bin/release-build',
    [string]$MsysBin = 'C:/msys64/ucrt64/bin',
    [string]$Compiler = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
$build = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$game = Join-Path $build 'Vanta.exe'
if (!(Test-Path -LiteralPath $game)) { throw "Build the game first: $game" }
if (!$Compiler) {
    $candidates = @((Join-Path $root 'bin/packaging/InnoSetup/ISCC.exe'), 'C:/Program Files (x86)/Inno Setup 6/ISCC.exe', 'C:/Program Files/Inno Setup 6/ISCC.exe')
    $Compiler = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (!$Compiler -or !(Test-Path -LiteralPath $Compiler)) { throw 'Install Inno Setup 6 or pass -Compiler with the full ISCC.exe path.' }
$objdump = Join-Path $MsysBin 'objdump.exe'
$strip = Join-Path $MsysBin 'strip.exe'
if (!(Test-Path -LiteralPath $objdump)) { throw "MSYS2 objdump is missing: $objdump" }
$release = Join-Path $root "bin/release/$Version"
$stage = Join-Path $release ('payload-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -LiteralPath $game -Destination (Join-Path $stage 'Vanta.exe')
Copy-Item -LiteralPath (Join-Path $root 'assets') -Destination (Join-Path $stage 'assets') -Recurse
# Inspect PE imports recursively; package every non-system dependency, including DLL dependencies.
$queue = [Collections.Generic.Queue[string]]::new()
$queue.Enqueue((Join-Path $stage 'Vanta.exe'))
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
while ($queue.Count) {
    $binary = $queue.Dequeue()
    $imports = & $objdump -p $binary
    if ($LASTEXITCODE -ne 0) { throw "Could not inspect $binary" }
    foreach ($line in $imports) {
        if ($line -notmatch 'DLL Name:\s*(\S+)') { continue }
        $name = $Matches[1]
        if (!$seen.Add($name)) { continue }
        $runtime = Join-Path $MsysBin $name
        if (Test-Path -LiteralPath $runtime) {
            $copy = Join-Path $stage $name
            Copy-Item -LiteralPath $runtime -Destination $copy
            $queue.Enqueue($copy)
        } elseif ($name -notmatch '^(api-ms-win-|ext-ms-win-)' -and !(Test-Path -LiteralPath (Join-Path "$env:SystemRoot/System32" $name))) {
            throw "Unresolved runtime dependency: $name"
        }
    }
}
if (Test-Path -LiteralPath $strip) {
    & $strip --strip-unneeded (Join-Path $stage 'Vanta.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Could not strip release executable.' }
}
$source = (& git -C $root rev-parse HEAD).Trim()
@("VANTA $Version", 'Publisher: R Gamestudio', "Source commit: $source", 'Platform: Windows x64', 'Includes game assets and all non-system runtime DLLs.') | Set-Content -LiteralPath (Join-Path $stage 'BUILD-INFO.txt') -Encoding utf8
& $Compiler "/DAppVersion=$Version" "/DSourceDir=$stage" "/DReleaseDir=$release" (Join-Path $root 'packaging/windows.iss')
if ($LASTEXITCODE -ne 0) { throw "Installer compiler failed: $LASTEXITCODE" }
$installer = Join-Path $release "VANTA-$Version-Setup.exe"
$hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  VANTA-$Version-Setup.exe" | Set-Content -LiteralPath (Join-Path $release 'SHA256SUMS.txt') -Encoding ascii
@{ version=$Version; sourceCommit=$source; payload=$stage; installer=$installer; sha256=$hash } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $release 'manifest.json') -Encoding utf8
Write-Output "Installer ready: $installer"
Write-Output "Payload: $stage"
Write-Output "SHA256: $hash"
