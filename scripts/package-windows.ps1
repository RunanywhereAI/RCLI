# Builds the release archive for Windows.
#
#   scripts\package-windows.ps1 [-BuildDir build] [-SdkDir ..\runanywhere-sdks]
#
# Same shape as the tarballs scripts/package.sh produces, so one top-level
# directory holding libexec/. What differs is where the shared libraries sit:
# Windows resolves a DLL from the executable's own directory and there is no
# rpath to send it anywhere else, so they go beside rcli.exe rather than into a
# lib/ subdirectory the way the Linux package does.
#
# Collecting them at all is the reason this script exists. onnxruntime and
# sherpa are DLLs on Windows rather than static archives, and CI only gets away
# with it because the build step leaves both directories on PATH. A package that
# does the same thing works on the machine that built it and nowhere else.
param(
    [string]$BuildDir = "build",
    [string]$SdkDir = ""
)

$ErrorActionPreference = 'Stop'

$ProjectDir = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $ProjectDir $BuildDir
}
if (-not $SdkDir) {
    # CI checks the SDK out as this repo's sibling, and so does a local
    # development tree; -SdkDir is for anything that does neither.
    $SdkDir = Join-Path (Split-Path -Parent $ProjectDir) 'runanywhere-sdks'
}

# CMakeLists carries the release version, but a prerelease tag adds a suffix
# CMake's VERSION field cannot hold (v0.4.1-beta.1). RCLI_VERSION lets the
# caller name the archive after the tag instead, so the asset and the release
# agree.
$Version = $env:RCLI_VERSION
if (-not $Version) {
    $VersionLine = Select-String -Path (Join-Path $ProjectDir 'CMakeLists.txt') `
        -Pattern 'project\(rcli VERSION ([0-9.]+)' | Select-Object -First 1
    if (-not $VersionLine) {
        throw "no 'project(rcli VERSION ...)' line in CMakeLists.txt"
    }
    $Version = $VersionLine.Matches[0].Groups[1].Value
}

$Platform = 'windows-x86_64'
$Name = "rcli-$Version-$Platform"
$Dist = Join-Path $ProjectDir 'dist'
$Stage = Join-Path $Dist $Name
$Libexec = Join-Path $Stage 'libexec'

$Binary = Join-Path $BuildDir 'rcli.exe'
if (-not (Test-Path -LiteralPath $Binary)) {
    throw "$Binary not found. Run cmake --build $BuildDir first."
}

Write-Host "Packaging rcli v$Version for $Platform"

Remove-Item -LiteralPath $Dist -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $Libexec -Force | Out-Null
Copy-Item -LiteralPath $Binary -Destination (Join-Path $Libexec 'rcli.exe')
Write-Host '  + libexec/rcli.exe'

# FetchContent unpacks the pinned ONNX Runtime somewhere under
# build/_deps/onnxruntime-src/lib, and the sherpa bundle carries a second, older
# copy of the same DLL. Stage the pinned one first and skip sherpa's, so the
# engine and the runtime it was built against stay together.
$OnnxCandidates = @(Get-ChildItem -Path $BuildDir -Filter 'onnxruntime.dll' -File -Recurse -ErrorAction SilentlyContinue)
$Onnx = $OnnxCandidates | Where-Object { $_.FullName -match 'onnxruntime-src' } | Select-Object -First 1
if (-not $Onnx) {
    $Onnx = $OnnxCandidates | Select-Object -First 1
}
if (-not $Onnx) {
    throw "onnxruntime.dll was not found under $BuildDir. The ONNX engine did not configure, so this build cannot be packaged."
}
Copy-Item -LiteralPath $Onnx.FullName -Destination (Join-Path $Libexec 'onnxruntime.dll')
Write-Host '  + libexec/onnxruntime.dll'

$SherpaLib = Join-Path $SdkDir 'core\third_party\sherpa-onnx-windows\lib'
if (-not (Test-Path -LiteralPath $SherpaLib)) {
    throw "$SherpaLib does not exist. Run core\scripts\windows\download-sherpa-onnx.bat in the SDK checkout, or pass -SdkDir."
}
Get-ChildItem -Path $SherpaLib -Filter '*.dll' -File |
    Where-Object { $_.Name -ne 'onnxruntime.dll' } |
    ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $Libexec $_.Name)
        Write-Host "  + libexec/$($_.Name)"
    }
if (-not (Test-Path -LiteralPath (Join-Path $Libexec 'sherpa-onnx-c-api.dll'))) {
    throw "sherpa-onnx-c-api.dll is not in $SherpaLib. That directory holds the static bundle, not the shared one the Windows build links."
}

$System32 = Join-Path $env:SystemRoot 'System32'

# Whatever else the binary imports has to travel too, and on the runner the
# MinGW runtime lives on the toolchain's own PATH, so the build and every CI
# check pass without it and only a user's machine notices. Read the import table
# rather than naming files, for the reason package.sh reads ldd: a hardcoded
# list goes stale in silence.
$Objdump = Get-Command 'objdump' -ErrorAction SilentlyContinue
if ($Objdump) {
    $Pending = New-Object System.Collections.Queue
    $Pending.Enqueue((Join-Path $Libexec 'rcli.exe'))
    $Seen = @{}
    while ($Pending.Count -gt 0) {
        $Current = $Pending.Dequeue()
        $Dump = & $Objdump.Source -p $Current
        if ($LASTEXITCODE -ne 0) {
            throw "objdump -p failed on $Current"
        }
        foreach ($Line in ($Dump | Select-String -Pattern '^\s*DLL Name:\s*(\S+)')) {
            $Dep = $Line.Matches[0].Groups[1].Value
            if ($Seen.ContainsKey($Dep.ToLowerInvariant())) { continue }
            $Seen[$Dep.ToLowerInvariant()] = $true

            if (Test-Path -LiteralPath (Join-Path $Libexec $Dep)) { continue }
            # Anything Windows itself provides stays out, the same way the Linux
            # package leaves glibc to the host.
            if (Test-Path -LiteralPath (Join-Path $System32 $Dep)) { continue }

            $Found = $env:PATH -split ';' |
                Where-Object { $_ } |
                ForEach-Object { Join-Path $_ $Dep } |
                Where-Object { Test-Path -LiteralPath $_ -ErrorAction SilentlyContinue } |
                Select-Object -First 1
            if (-not $Found) {
                Write-Warning "$Current imports $Dep and it is neither in System32 nor on PATH"
                continue
            }
            Copy-Item -LiteralPath $Found -Destination (Join-Path $Libexec $Dep)
            Write-Host "  + libexec/$Dep"
            $Pending.Enqueue((Join-Path $Libexec $Dep))
        }
    }
} else {
    Write-Warning 'objdump is not on PATH, so the import table went unread. The staged run below is the only thing left that can catch a missing DLL.'
}

# The check that earns its keep. Windows searches the executable's own directory
# before anything on PATH, so narrowing PATH to the system directories asks the
# staged copy to run on nothing but what it ships with. Every other check here
# passes on a machine that has the build tree; this one does not.
$OldPath = $env:PATH
Push-Location $Libexec
try {
    $env:PATH = "$System32;$env:SystemRoot"
    & '.\rcli.exe' --version
    if ($LASTEXITCODE -ne 0) {
        throw "the staged binary does not run on its own; a DLL is missing from libexec"
    }
} finally {
    $env:PATH = $OldPath
    Pop-Location
}

$Zip = Join-Path $Dist "$Name.zip"
Compress-Archive -Path $Stage -DestinationPath $Zip -CompressionLevel Optimal
$Sha256 = (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant()

Write-Host ''
Write-Host ("  dist/$Name.zip  ({0:N1} MB)" -f ((Get-Item -LiteralPath $Zip).Length / 1MB))
Write-Host "  sha256 `"$Sha256`""
