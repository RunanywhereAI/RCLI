$ErrorActionPreference = 'Stop'

$Repo = 'RunanywhereAI/RCLI'
# There is no Homebrew here, so this installer does the whole job itself rather
# than handing off to a package manager: download the release zip, check it,
# unpack it, put it on PATH.
$InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\rcli'

# Windows PowerShell picks the older protocols on some builds and api.github.com
# refuses anything below TLS 1.2.
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
# Invoke-WebRequest spends most of a large download redrawing its progress bar.
$ProgressPreference = 'SilentlyContinue'

function Write-Info([string]$Message) {
    Write-Host '==> ' -ForegroundColor Blue -NoNewline
    Write-Host $Message
}
function Write-Ok([string]$Message) {
    Write-Host '==> ' -ForegroundColor Green -NoNewline
    Write-Host $Message
}
function Write-Warn([string]$Message) {
    Write-Host 'Warning: ' -ForegroundColor Yellow -NoNewline
    Write-Host $Message
}
# throw rather than exit, because the documented way to run this is
# `irm ... | iex`: an exit there closes the user's console, taking the error
# message with it.
function Fail([string]$Message) {
    throw "Error: $Message"
}

Write-Info 'Checking latest RCLI release...'
try {
    $Release = Invoke-RestMethod "https://api.github.com/repos/$Repo/releases/latest"
} catch {
    Fail 'Could not determine latest release version. Check your internet connection.'
}
$Version = "$($Release.tag_name)" -replace '^v', ''
if (-not $Version) { Fail 'Could not determine latest release version. Check your internet connection.' }
Write-Info "Latest version: v$Version"

# PROCESSOR_ARCHITECTURE reports the process, not the machine, so a 32-bit host
# under WOW64 says x86 while ARCHITEW6432 names what is really underneath.
$Arch = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
if ($Arch -ne 'AMD64') { Fail "RCLI requires 64-bit x86 Windows. Detected: $Arch" }

$AssetName = "rcli-$Version-windows-x86_64.zip"
$Asset = $Release.assets | Where-Object { $_.name -eq $AssetName } | Select-Object -First 1
if (-not $Asset) {
    Fail "v$Version does not publish $AssetName. Open an issue at https://github.com/$Repo/issues"
}
$ShaAsset = $Release.assets | Where-Object { $_.name -eq "$AssetName.sha256" } | Select-Object -First 1

$Temp = Join-Path ([IO.Path]::GetTempPath()) ('rcli-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Temp -Force | Out-Null
try {
    $Zip = Join-Path $Temp $AssetName
    Write-Info "Downloading $AssetName..."
    try {
        Invoke-WebRequest -Uri $Asset.browser_download_url -OutFile $Zip
    } catch {
        Fail "Could not download $($Asset.browser_download_url)"
    }

    if ($ShaAsset) {
        # Through a file rather than straight into a variable: the asset is
        # served as octet-stream and the web cmdlets hand back bytes for that,
        # not the line of text this needs.
        $ShaFile = Join-Path $Temp $ShaAsset.name
        Invoke-WebRequest -Uri $ShaAsset.browser_download_url -OutFile $ShaFile
        # Both lowercased rather than relying on -ne being case-insensitive:
        # Get-FileHash returns uppercase and the sidecar is written lowercase,
        # so the comparison only looks wrong until you know that rule.
        $Expected = ((Get-Content -Raw -LiteralPath $ShaFile) -split '\s+')[0].ToLowerInvariant()
        $Actual = (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($Actual -ne $Expected) {
            Fail "Checksum mismatch on $AssetName. Expected $Expected, got $Actual. Do not use this download."
        }
        Write-Ok 'Checksum verified'
    } else {
        Write-Warn "The release publishes no $AssetName.sha256, so the download could not be verified."
    }

    Write-Info "Installing RCLI v$Version to $InstallDir..."
    Expand-Archive -LiteralPath $Zip -DestinationPath $Temp -Force
    $Unpacked = Join-Path $Temp "rcli-$Version-windows-x86_64\libexec"
    if (-not (Test-Path -LiteralPath $Unpacked)) {
        Fail "$AssetName does not have the layout this installer expects. Open an issue at https://github.com/$Repo/issues"
    }

    # libexec is the package's own layout, the one the Homebrew formula installs
    # and symlinks a bin/rcli at. Nothing to symlink here, so its contents land
    # directly in the install directory and that directory goes on PATH: rcli.exe
    # finds its DLLs by being in the same folder as them.
    Remove-Item -LiteralPath $InstallDir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    Copy-Item -Path (Join-Path $Unpacked '*') -Destination $InstallDir -Recurse -Force
} finally {
    Remove-Item -LiteralPath $Temp -Recurse -Force -ErrorAction SilentlyContinue
}

$Exe = Join-Path $InstallDir 'rcli.exe'
if (-not (Test-Path -LiteralPath $Exe)) { Fail "Installation failed. rcli.exe is not in $InstallDir." }
& $Exe --version | Out-Null
if ($LASTEXITCODE -ne 0) { Fail "Installation failed. rcli.exe is installed but does not run." }

# The user's own PATH, never the machine's: this installs under LOCALAPPDATA for
# one account and needs no administrator to do it.
$UserPath = [Environment]::GetEnvironmentVariable('Path', 'User')
$Entries = @()
if ($UserPath) { $Entries = @($UserPath -split ';' | Where-Object { $_ }) }
if ($Entries -contains $InstallDir) {
    Write-Ok "$InstallDir is already on your PATH"
} else {
    [Environment]::SetEnvironmentVariable('Path', (($Entries + $InstallDir) -join ';'), 'User')
    Write-Ok "Added $InstallDir to your PATH"
}

Write-Ok "RCLI v$Version installed successfully"
Write-Host ''
Write-Warn 'Open a new terminal before running rcli. This one was started with the old PATH.'
Write-Host ''
Write-Info 'Getting started:'
Write-Host '    rcli list --all              every model in the catalog'
Write-Host '    rcli pull qwen3-0.6b         download one'
Write-Host '    rcli run qwen3-0.6b          talk to it, /? for commands'
Write-Host '    rcli engines                 which backends are available here'
Write-Host ''
Write-Host '  Models download on demand into %LOCALAPPDATA%\RunAnywhere'
