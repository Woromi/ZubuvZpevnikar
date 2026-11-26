# Packs a release build of Zpěvníkář for Windows: a portable zip and an installer (Inno Setup).
# The app, its converter and the bundled package (cmake --install), KF6 SyntaxHighlighting and
# the Qt and MSVC runtime files (windeployqt). TeX Live or MiKTeX, LilyPond, Git for Windows
# (bash for the songbook build), Python and fluidsynth are not included: the user installs them.
#
#   packaging/windows/package.ps1 -Build build/release -Kf6 <KF6 prefix> -Out dist
# Needs windeployqt (Qt's bin on PATH) and, for the installer, Inno Setup 6 (ISCC.exe).
param(
    [Parameter(Mandatory)] [string] $Build,
    [Parameter(Mandatory)] [string] $Kf6,
    [Parameter(Mandatory)] [string] $Out
)
$ErrorActionPreference = 'Stop'

$versionLine = Select-String -Path (Join-Path $Build 'CMakeCache.txt') `
    -Pattern '^CMAKE_PROJECT_VERSION:STATIC=(.*)$' -ErrorAction SilentlyContinue
if (-not $versionLine) { throw "$Build is not a configured build dir" }
$version = $versionLine.Matches[0].Groups[1].Value

New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path $Out).Path
# A fresh folder: nothing of an earlier run gets into the package.
$stage = Join-Path ([System.IO.Path]::GetTempPath()) ("zpevnikar-" + [guid]::NewGuid())
$app = Join-Path $stage 'Zpevnikar'

cmake --install $Build --prefix $app
if ($LASTEXITCODE) { throw 'cmake --install failed' }
$bin = Join-Path $app 'bin'
# The build skips the converter without cargo and the package without its submodule: a release
# must have both.
foreach ($file in 'bin/chords_converter.exe', 'share/zpevnikar/zubuvzpevnik/zubuvzpevnik.sty') {
    if (-not (Test-Path (Join-Path $app $file))) {
        throw "$file is missing: the build had no cargo or no submodules"
    }
}
Copy-Item (Join-Path $Kf6 'bin/KF6SyntaxHighlighting.dll') $bin
windeployqt --release --compiler-runtime --no-system-d3d-compiler `
    (Join-Path $bin 'zpevnikar.exe') (Join-Path $bin 'KF6SyntaxHighlighting.dll')
if ($LASTEXITCODE) { throw 'windeployqt failed' }

# It must start from the package alone (a missing DLL or plugin ends it at once): without Qt's
# folders on PATH or in the Qt variables that the build's Qt installation sets.
$saved = @{}
foreach ($name in 'PATH', 'QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QT_QPA_PLATFORM') {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name)
    [Environment]::SetEnvironmentVariable($name, $null)
}
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
try {
    $process = Start-Process -FilePath (Join-Path $bin 'zpevnikar.exe') -PassThru
    $null = $process.Handle # keeps the exit code readable
    Start-Sleep -Seconds 10
    if ($process.HasExited) { throw "zpevnikar.exe ended at once (exit $($process.ExitCode))" }
    Stop-Process -Id $process.Id
    $process.WaitForExit()
} finally {
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
}

$zip = Join-Path $Out "Zpevnikar-$version-windows-x64.zip"
Compress-Archive -Path $app -DestinationPath $zip -Force
Write-Output "Built $zip"

$iscc = @(
    (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source,
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 (ISCC.exe) was not found' }
& $iscc "/DAppVersion=$version" "/DAppDir=$app" "/O$Out" (Join-Path $PSScriptRoot 'zpevnikar.iss')
if ($LASTEXITCODE) { throw 'ISCC failed' }
Write-Output "Built $Out\Zpevnikar-$version-setup.exe"
