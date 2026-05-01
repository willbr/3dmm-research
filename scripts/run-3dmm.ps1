# Configure + build + deploy + launch a 3DMMForever build in one shot.
#
# Usage from a plain PowerShell at repo root:
#   .\scripts\run-3dmm.ps1                       # x64 Debug, then launch
#   .\scripts\run-3dmm.ps1 -Arch x86             # x86 Debug, then launch
#   .\scripts\run-3dmm.ps1 -Config Release       # x64 Release
#   .\scripts\run-3dmm.ps1 -NoLaunch             # build only, don't run
#   .\scripts\run-3dmm.ps1 -SkipBuild            # use existing binary, just deploy + launch
#
# Steps:
#   1. invoke-vcvars for the chosen arch (requires VCVars module: github.com/bruxisma/VCVars).
#   2. cmake --preset $arch:msvc:$config (creates build/ or build-x64/).
#   3. cmake --build that preset's binaryDir, target studio.
#   4. Kill any running 3dmovie processes so the on-disk file isn't locked.
#   5. Copy the freshly built exe into dist\Microsoft Kids\3D Movie Maker\
#      (so it sits next to the chunked data files). x64 lands as 3dmovie-x64.exe
#      to coexist with the x86 3dmovie.exe.
#   6. Launch unless -NoLaunch.
[CmdletBinding()]
param(
  [ValidateSet('x86', 'x64')] [string] $Arch = 'x64',
  [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')] [string] $Config = 'Debug',
  [switch] $NoLaunch,
  [switch] $SkipBuild
)

# Don't use $ErrorActionPreference='Stop' here -- it makes PowerShell treat
# *any* native-command stderr line as a terminating ErrorRecord (e.g. cmake's
# WARNING from the bren_zb x64 gate would fail the script even though cmake
# returned 0). Use explicit $LASTEXITCODE checks instead.

# Resolve repo root from this script's location so it works no matter where it's invoked from.
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
Set-Location $RepoRoot

$vcArch = if ($Arch -eq 'x86') { 'x86' } else { 'AMD64' }
$buildDir = if ($Arch -eq 'x86') { 'build' } else { 'build-x64' }
$preset = "{0}:msvc:{1}" -f $Arch, $Config.ToLower()

if (-not $SkipBuild) {
  Write-Host "[run-3dmm] vcvars: -TargetArch $vcArch -HostArch AMD64" -ForegroundColor Cyan
  if (-not (Get-Command invoke-vcvars -ErrorAction SilentlyContinue)) {
    throw "invoke-vcvars not found. Install the VCVars PowerShell module: github.com/bruxisma/VCVars"
  }
  pushvc (invoke-vcvars -TargetArch $vcArch -HostArch AMD64)

  Write-Host "[run-3dmm] cmake --preset $preset" -ForegroundColor Cyan
  cmake --preset $preset
  if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

  Write-Host "[run-3dmm] cmake --build $buildDir --target studio" -ForegroundColor Cyan
  cmake --build $buildDir --target studio
  # The post-build mirror to "C:\Program Files (x86)\3DMMForever\" can fail
  # with permission-denied if a previous run is still holding the install
  # copy open -- harmless, the exe in $buildDir is what matters.
  if ($LASTEXITCODE -ne 0) {
    Write-Warning "[run-3dmm] cmake build returned $LASTEXITCODE -- if it's only the post-build mirror that failed, the binary in $buildDir is still good"
  }
}

$srcExe = Join-Path $RepoRoot "$buildDir\3dmovie.exe"
if (-not (Test-Path $srcExe)) {
  throw "[run-3dmm] no exe at $srcExe -- has the build run?"
}

# Kill any prior 3dmovie* so the destination isn't locked.
Get-Process | Where-Object { $_.Name -like '3dmovie*' } | ForEach-Object {
  Write-Host "[run-3dmm] stopping running PID $($_.Id) ($($_.Name))" -ForegroundColor Yellow
  Stop-Process -Id $_.Id -Force
}
Start-Sleep -Milliseconds 250

$dataDir = Join-Path $RepoRoot 'dist\Microsoft Kids\3D Movie Maker'
if (-not (Test-Path $dataDir)) {
  throw "[run-3dmm] data dir not found: $dataDir -- run a full install first (cmake --build $buildDir --target install)"
}

# x86 keeps the original filename; x64 uses a -x64 suffix so both can coexist
# in the same data dir.
$dstName = if ($Arch -eq 'x86') { '3dmovie.exe' } else { '3dmovie-x64.exe' }
$dstExe = Join-Path $dataDir $dstName
Copy-Item $srcExe $dstExe -Force
Write-Host "[run-3dmm] deployed: $dstExe" -ForegroundColor Green

if ($NoLaunch) {
  Write-Host "[run-3dmm] -NoLaunch set; skipping launch" -ForegroundColor Cyan
  exit 0
}

# Clear any prior crash log so the next dialog (if any) is easy to find.
$crashLog = Join-Path $env:TEMP '3dmmforever-crash.txt'
Remove-Item $crashLog -ErrorAction SilentlyContinue

Write-Host "[run-3dmm] launching $dstExe (cwd: $dataDir)" -ForegroundColor Green
Start-Process -FilePath $dstExe -WorkingDirectory $dataDir
