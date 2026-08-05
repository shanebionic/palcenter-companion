[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$BuildDirectory,

  [string]$OutputDirectory = "artifacts"
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$resolvedBuild = (Resolve-Path -LiteralPath $BuildDirectory).Path
$resolvedOutput = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
  [System.IO.Path]::GetFullPath($OutputDirectory)
} else {
  [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $OutputDirectory))
}
$stagingRoot = Join-Path $resolvedOutput "stage"
$packageRoot = Join-Path $stagingRoot "PalCenterCompanion"
$archivePath = Join-Path $resolvedOutput "PalCenterCompanion-0.3.0-win64.zip"

$productionDlls = @(Get-ChildItem -LiteralPath $resolvedBuild -Filter "main.dll" -Recurse -File)
if ($productionDlls.Count -ne 1) {
  throw "Expected exactly one production main.dll under '$resolvedBuild'. Contract-test DLLs cannot be packaged."
}
if (Get-ChildItem -LiteralPath $resolvedBuild -Filter "PalCenterCompanion-contract-test.dll" -Recurse -File) {
  throw "Contract-test output was found. Configure PALCENTER_UE4SS_BUILD_MODE=PRODUCTION in a clean build directory."
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) { throw "Visual Studio Installer vswhere.exe was not found." }
$dumpbin = & $vswhere -latest -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  -find "VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe" |
  Select-Object -First 1
if (-not $dumpbin) { throw "Unable to locate dumpbin.exe." }
$exportText = (& $dumpbin /exports $productionDlls[0].FullName) -join "`n"
foreach ($requiredExport in @("start_mod", "uninstall_mod")) {
  if ($exportText -notmatch "\b$requiredExport\b") {
    throw "Production DLL is missing required UE4SS export: $requiredExport"
  }
}

if (Test-Path -LiteralPath $stagingRoot) {
  Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}
New-Item -ItemType Directory -Path (Join-Path $packageRoot "dlls") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $packageRoot "config") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $packageRoot "LICENSES") -Force | Out-Null

Copy-Item -LiteralPath $productionDlls[0].FullName -Destination (Join-Path $packageRoot "dlls/main.dll")
Copy-Item -LiteralPath (Join-Path $repositoryRoot "config/PalCenterCompanion.ini") -Destination (Join-Path $packageRoot "config/PalCenterCompanion.ini")
Copy-Item -LiteralPath (Join-Path $repositoryRoot "extension/enabled.txt") -Destination (Join-Path $packageRoot "enabled.txt")
Copy-Item -LiteralPath (Join-Path $repositoryRoot "packaging/README.txt") -Destination (Join-Path $packageRoot "README.txt")
Copy-Item -LiteralPath (Join-Path $repositoryRoot "packaging/INSTALL.md") -Destination (Join-Path $packageRoot "INSTALL.md")
Copy-Item -LiteralPath (Join-Path $repositoryRoot "docs/ADMIN-ACTIONS-UAT.md") -Destination (Join-Path $packageRoot "ADMIN-ACTIONS-UAT.md")
Copy-Item -LiteralPath (Join-Path $repositoryRoot "packaging/LICENSES/THIRD-PARTY-NOTICES.txt") -Destination (Join-Path $packageRoot "LICENSES/THIRD-PARTY-NOTICES.txt")

$expectedFiles = @(
  "dlls/main.dll",
  "config/PalCenterCompanion.ini",
  "enabled.txt",
  "README.txt",
  "INSTALL.md",
  "ADMIN-ACTIONS-UAT.md",
  "LICENSES/THIRD-PARTY-NOTICES.txt"
)
foreach ($relativePath in $expectedFiles) {
  if (-not (Test-Path -LiteralPath (Join-Path $packageRoot $relativePath) -PathType Leaf)) {
    throw "Staging validation failed: missing $relativePath"
  }
}

New-Item -ItemType Directory -Path $resolvedOutput -Force | Out-Null
if (Test-Path -LiteralPath $archivePath) {
  Remove-Item -LiteralPath $archivePath -Force
}
Compress-Archive -LiteralPath $packageRoot -DestinationPath $archivePath -CompressionLevel Optimal

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
try {
  $entryNames = @($zip.Entries | ForEach-Object { $_.FullName.Replace("\", "/") })
  foreach ($relativePath in $expectedFiles) {
    $expectedEntry = "PalCenterCompanion/$relativePath"
    if ($entryNames -notcontains $expectedEntry) {
      throw "ZIP validation failed: missing $expectedEntry"
    }
  }
  if ($entryNames | Where-Object { $_ -match "contract-test|UEPseudo|RE-UE4SS" }) {
    throw "ZIP validation failed: source-only or unauthorized build content was included."
  }
} finally {
  $zip.Dispose()
}

$dllHash = (Get-FileHash -LiteralPath (Join-Path $packageRoot "dlls/main.dll") -Algorithm SHA256).Hash.ToLowerInvariant()
$archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$archivePath.sha256" -Value "$archiveHash  $(Split-Path -Leaf $archivePath)" -Encoding ascii

Write-Host "Production DLL: $($productionDlls[0].FullName)"
Write-Host "Production DLL SHA-256: $dllHash"
Write-Host "Package: $archivePath"
Write-Host "Package SHA-256: $archiveHash"
