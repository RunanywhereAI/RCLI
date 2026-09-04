param(
    [Parameter(Mandatory = $false)]
    [string]$BuildDir = "build",

    [Parameter(Mandatory = $false)]
    [string]$Version = "",

    [Parameter(Mandatory = $false)]
    [string]$KitDir = "",

    # Names the archive. install.ps1 asks for wally-<ver>-windows-arm64.zip on an
    # ARM64 host and falls back to x86_64, so the arm64 build must use exactly
    # that spelling or the native archive is never found.
    [Parameter(Mandatory = $false)]
    [ValidateSet("windows-x86_64", "windows-arm64")]
    [string]$Platform = "windows-x86_64"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$CliRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not [IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $CliRoot $BuildDir
}
$BuildDir = (Resolve-Path $BuildDir).Path

if ([string]::IsNullOrWhiteSpace($Version)) {
    $cm = Get-Content (Join-Path $CliRoot "CMakeLists.txt") -Raw
    if ($cm -match 'project\(wally VERSION ([0-9.]+)') { $Version = $Matches[1] }
}
$Version = $Version.TrimStart("v")
if ([string]::IsNullOrWhiteSpace($Version)) { throw "cannot resolve Wally version" }

$Binary = @(
    (Join-Path $BuildDir "wally.exe"),
    (Join-Path $BuildDir "Release\wally.exe"),
    (Join-Path $BuildDir "RelWithDebInfo\wally.exe")
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Binary) {
    $Binary = Get-ChildItem -Path $BuildDir -Filter "wally.exe" -File -Recurse |
        Select-Object -ExpandProperty FullName -First 1
}
if (-not $Binary) { throw "wally.exe was not found under $BuildDir" }

if ([string]::IsNullOrWhiteSpace($KitDir)) {
    $KitDir = $env:WALLY_SDK_KIT
}
if ([string]::IsNullOrWhiteSpace($KitDir)) {
    $KitDir = $env:CMAKE_PREFIX_PATH
}

$DistDir = Join-Path $CliRoot "dist"
$StageRoot = Join-Path $DistDir "stage"
$Stage = Join-Path $StageRoot "wally-$Platform"
$BinDir = Join-Path $Stage "bin"
$Zip = Join-Path $DistDir "wally-$Version-$Platform.zip"

Remove-Item $Stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item $BinDir -ItemType Directory -Force | Out-Null
Copy-Item $Binary (Join-Path $BinDir "wally.exe")
$Readme = Join-Path $CliRoot "README.md"
if (Test-Path $Readme) { Copy-Item $Readme (Join-Path $Stage "README.md") }

function Copy-Dll([string]$Src) {
    if ([string]::IsNullOrWhiteSpace($Src) -or -not (Test-Path $Src)) { return }
    $dest = Join-Path $BinDir (Split-Path $Src -Leaf)
    if (-not (Test-Path $dest)) { Copy-Item $Src $dest }
}

# Kit third_party (onnxruntime.dll) plus anything next to the built exe.
if ($KitDir -and (Test-Path (Join-Path $KitDir "third_party"))) {
    Get-ChildItem (Join-Path $KitDir "third_party") -Filter "*.dll" -File -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Dll $_.FullName }
}
Get-ChildItem -Path (Split-Path $Binary -Parent) -Filter "*.dll" -File -ErrorAction SilentlyContinue |
    ForEach-Object { Copy-Dll $_.FullName }

$OldPath = $env:PATH
try {
    $env:PATH = "$BinDir;$OldPath"
    & (Join-Path $BinDir "wally.exe") version
    if ($LASTEXITCODE -ne 0) { throw "packaged wally version smoke failed" }
} finally {
    $env:PATH = $OldPath
}

New-Item $DistDir -ItemType Directory -Force | Out-Null
Remove-Item $Zip, "$Zip.sha256" -Force -ErrorAction SilentlyContinue
Compress-Archive -Path $Stage -DestinationPath $Zip -CompressionLevel Optimal
$Hash = (Get-FileHash $Zip -Algorithm SHA256).Hash.ToLowerInvariant()
"$Hash  $([IO.Path]::GetFileName($Zip))" |
    Set-Content -Path "$Zip.sha256" -Encoding ascii -NoNewline
Write-Host "Packaged: $Zip"
