[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$Ue4ssRoot,

  [string]$BuildDirectory = "build-production",
  [string]$OutputDirectory = "artifacts"
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$expectedCommit = "c838a8acaade1a0f860bdf249f039e58f4e10088"
$windowsSdk = "10.0.26100.0"
$resolvedUe4ss = (Resolve-Path -LiteralPath $Ue4ssRoot).Path
$resolvedBuild = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $BuildDirectory))

if (-not (Test-Path -LiteralPath (Join-Path $resolvedUe4ss "CMakeLists.txt") -PathType Leaf)) {
  throw "UE4SS source path does not contain CMakeLists.txt: $resolvedUe4ss"
}
if (-not (Test-Path -LiteralPath (Join-Path $resolvedUe4ss "deps/first/Unreal/CMakeLists.txt") -PathType Leaf)) {
  throw "Authorized UEPseudo sources are missing. Initialize the pinned UE4SS submodules with an Epic-linked GitHub account."
}
$actualCommit = (& git -C $resolvedUe4ss rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $expectedCommit) {
  throw "Unsupported UE4SS revision. Expected $expectedCommit; found '$actualCommit'."
}

& cmake -S $repositoryRoot -B $resolvedBuild -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_SYSTEM_VERSION=$windowsSdk" `
  "-DPALCENTER_BUILD_TESTS=OFF" `
  "-DPALCENTER_UE4SS_BUILD_MODE=PRODUCTION" `
  "-DPALCENTER_UE4SS_ROOT=$resolvedUe4ss"
if ($LASTEXITCODE -ne 0) { throw "Production CMake configuration failed." }

& cmake --build $resolvedBuild --config Release --target PalCenterCompanion
if ($LASTEXITCODE -ne 0) { throw "Production DLL build failed." }

$dlls = @(Get-ChildItem -LiteralPath $resolvedBuild -Filter "main.dll" -Recurse -File)
if ($dlls.Count -ne 1) { throw "Expected exactly one production main.dll; found $($dlls.Count)." }

& (Join-Path $PSScriptRoot "package-production.ps1") `
  -BuildDirectory $resolvedBuild `
  -OutputDirectory $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw "Production package creation failed." }
