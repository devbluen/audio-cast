<#
.SYNOPSIS
    Builds AudioCast for Windows and Linux and puts the release files in dist/.

.DESCRIPTION
    Windows: compiled with Visual Studio (C++ workload) and CMake.
    Linux:   cross-compiled from Windows with Zig, which this script downloads
             into .tools/ on first use. No Linux machine, WSL or Docker needed.

    asio, libopus and LAME are downloaded and built by CMake. The first build
    takes a few minutes; later builds reuse everything.

    Result:
      dist/
        windows/components/audio-cast.dll    open.mp
        windows/plugins/audio-cast.dll       SA-MP
        windows/tools/audio-cast-check.exe
        linux/components/audio-cast.so       open.mp
        linux/plugins/audio-cast.so          SA-MP
        linux/tools/audio-cast-check
        include/audio-cast.inc
        examples/radio.pwn
        audio-cast-<version>-windows-x86.zip
        audio-cast-<version>-linux-x86.zip

.PARAMETER Target
    all (default), windows or linux.

.PARAMETER Version
    Version embedded in the binaries and the zip names (MAJOR.MINOR.PATCH).

.PARAMETER Clean
    Deletes the build folders first.

.PARAMETER SkipTests
    Does not build or run the tests (Windows only; Linux binaries cannot run here).

.EXAMPLE
    .\build.ps1

.EXAMPLE
    .\build.ps1 -Target linux -Version 1.1.0 -Clean
#>
[CmdletBinding()]
param(
    [ValidateSet('all', 'windows', 'linux')]
    [string]$Target = 'all',

    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version = '1.0.0',

    [switch]$Clean,

    [switch]$SkipTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Invoke-WebRequest is many times slower with its progress bar.
$ProgressPreference = 'SilentlyContinue'

$Root = $PSScriptRoot
$BuildRoot = Join-Path $Root 'build'
$ToolsRoot = Join-Path $Root '.tools'
$DistRoot = Join-Path $Root 'dist'

$ZigRelease = @{
    Name   = 'zig-0.15.2'
    Url    = 'https://ziglang.org/download/0.15.2/zig-x86_64-windows-0.15.2.zip'
    Sha256 = '3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c'
}
$NinjaRelease = @{
    Name   = 'ninja-1.13.2'
    Url    = 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip'
    Sha256 = '07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65'
}
# Oldest glibc the Linux build may run on.
$LinuxTarget = 'x86-linux-gnu.2.17'

function Write-Step([string]$Message) {
    Write-Host ''
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Invoke-Native([string]$FilePath, [string[]]$Arguments) {
    # Out-Host: show the output instead of returning it from the caller.
    & $FilePath @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$([IO.Path]::GetFileName($FilePath)) failed (exit code $LASTEXITCODE)"
    }
}

function Reset-Directory([string]$Path) {
    if (Test-Path $Path) { Remove-Item $Path -Recurse -Force }
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

# Downloads a zip, checks its SHA-256 and extracts it into .tools/<name>.
function Get-Tool([hashtable]$Release) {
    $destination = Join-Path $ToolsRoot $Release.Name
    if (Test-Path $destination) { return $destination }

    New-Item -ItemType Directory -Path $ToolsRoot -Force | Out-Null
    $archive = Join-Path $ToolsRoot "$($Release.Name).zip"
    Write-Step "Downloading $($Release.Name)"
    Invoke-WebRequest -Uri $Release.Url -OutFile $archive -UseBasicParsing

    $hash = (Get-FileHash $archive -Algorithm SHA256).Hash
    if ($hash -ne $Release.Sha256.ToUpperInvariant()) {
        Remove-Item $archive -Force
        throw "$($Release.Name): download is corrupt (SHA-256 mismatch)"
    }

    $staging = "$destination.partial"
    if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
    Expand-Archive -Path $archive -DestinationPath $staging
    Move-Item $staging $destination
    Remove-Item $archive -Force
    return $destination
}

function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $found = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json
    $instance = @($found) | Select-Object -First 1
    if (-not $instance) { return $null }

    $generators = @{ 16 = 'Visual Studio 16 2019'; 17 = 'Visual Studio 17 2022'; 18 = 'Visual Studio 18 2026' }
    $major = [int]($instance.installationVersion.Split('.')[0])
    if (-not $generators.ContainsKey($major)) {
        throw "Unsupported Visual Studio version $($instance.installationVersion)"
    }
    return [pscustomobject]@{
        Path      = $instance.installationPath
        Name      = $instance.displayName
        Generator = $generators[$major]
    }
}

function Find-CMake {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $vs = Find-VisualStudio
    if ($vs) {
        $bundled = Join-Path $vs.Path 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path $bundled) { return $bundled }
    }
    throw 'CMake was not found. Install it from https://cmake.org/download/ or add the "C++ CMake tools" component to Visual Studio.'
}

function Find-Ninja {
    $command = Get-Command ninja -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    return Join-Path (Get-Tool $NinjaRelease) 'ninja.exe'
}

function Test-ElfLibrary([string]$Path) {
    # A 32-bit x86 ELF file: magic, ELFCLASS32, machine EM_386.
    $bytes = [IO.File]::ReadAllBytes($Path)
    $isElf = $bytes.Length -gt 20 -and $bytes[0] -eq 0x7F -and $bytes[1] -eq 0x45 -and $bytes[2] -eq 0x4C -and $bytes[3] -eq 0x46
    if (-not $isElf -or $bytes[4] -ne 1 -or $bytes[18] -ne 3) {
        throw "$Path is not a 32-bit x86 Linux binary"
    }
}

function Build-Windows([string]$CMake) {
    $vs = Find-VisualStudio
    if (-not $vs) {
        throw 'Visual Studio with the "Desktop development with C++" workload was not found.'
    }
    Write-Step "Windows x86 ($($vs.Name))"

    $buildDir = Join-Path $BuildRoot 'windows-x86'
    if ($Clean -and (Test-Path $buildDir)) { Remove-Item $buildDir -Recurse -Force }
    $testing = if ($SkipTests) { 'OFF' } else { 'ON' }

    Invoke-Native $CMake @('-S', $Root, '-B', $buildDir, '-G', $vs.Generator, '-A', 'Win32', '--log-level=WARNING', '-Wno-dev', '-Wno-deprecated', '--no-warn-unused-cli',
        "-DAUDIOCAST_VERSION=$Version", "-DBUILD_TESTING=$testing")
    Invoke-Native $CMake @('--build', $buildDir, '--config', 'Release', '--parallel', '--', '/nologo', '/verbosity:minimal')

    if (-not $SkipTests) {
        Write-Step 'Windows tests'
        $ctest = Join-Path (Split-Path $CMake) 'ctest.exe'
        Invoke-Native $ctest @('--test-dir', $buildDir, '-C', 'Release', '--output-on-failure', '--progress')
    }

    return [pscustomobject]@{
        Plugin = Join-Path $buildDir 'plugins\Release\audio-cast.dll'
        Tool   = Join-Path $buildDir 'Release\audio-cast-check.exe'
    }
}

function Build-Linux([string]$CMake) {
    Write-Step "Linux x86 (cross-compiled with Zig for glibc $($LinuxTarget.Split('.', 2)[1])+)"

    $zigRoot = Get-Tool $ZigRelease
    $zig = (Get-ChildItem $zigRoot -Recurse -Filter 'zig.exe' | Select-Object -First 1).FullName
    if (-not $zig) { throw "zig.exe not found in $zigRoot" }
    $ninja = Find-Ninja

    # CMake needs one executable per tool; these forward to zig.exe.
    $wrappers = Join-Path $ToolsRoot 'zig-wrappers'
    New-Item -ItemType Directory -Path $wrappers -Force | Out-Null
    $commands = @{
        'zig-cc.cmd'     = "cc -target $LinuxTarget"
        'zig-c++.cmd'    = "c++ -target $LinuxTarget"
        'zig-ar.cmd'     = 'ar'
        'zig-ranlib.cmd' = 'ranlib'
    }
    foreach ($name in $commands.Keys) {
        Set-Content -Path (Join-Path $wrappers $name) -Value "@`"$zig`" $($commands[$name]) %*" -Encoding Ascii
    }

    $env:AUDIOCAST_ZIG_WRAPPERS = $wrappers
    # Keep Zig's caches inside the project instead of %LOCALAPPDATA%.
    $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $ToolsRoot 'zig-cache'
    $env:ZIG_LOCAL_CACHE_DIR = Join-Path $ToolsRoot 'zig-cache'

    $buildDir = Join-Path $BuildRoot 'linux-x86'
    if ($Clean -and (Test-Path $buildDir)) { Remove-Item $buildDir -Recurse -Force }

    Invoke-Native $CMake @('-S', $Root, '-B', $buildDir, '-G', 'Ninja', '--log-level=WARNING', '-Wno-dev', '-Wno-deprecated', '--no-warn-unused-cli',
        "-DCMAKE_MAKE_PROGRAM=$ninja",
        "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $Root 'cmake\zig-linux-x86.cmake')",
        '-DCMAKE_BUILD_TYPE=Release',
        '-DBUILD_TESTING=OFF',
        "-DAUDIOCAST_VERSION=$Version")
    Invoke-Native $CMake @('--build', $buildDir, '--parallel')

    $result = [pscustomobject]@{
        Plugin = Join-Path $buildDir 'plugins\audio-cast.so'
        Tool   = Join-Path $buildDir 'audio-cast-check'
    }
    Test-ElfLibrary $result.Plugin
    Test-ElfLibrary $result.Tool
    return $result
}

function Publish-Platform([string]$Platform, $Output, [string]$ToolName) {
    $platformDir = Join-Path $DistRoot $Platform
    Reset-Directory $platformDir
    $pluginName = Split-Path $Output.Plugin -Leaf
    foreach ($folder in 'components', 'plugins') {
        New-Item -ItemType Directory -Path (Join-Path $platformDir $folder) | Out-Null
        Copy-Item $Output.Plugin (Join-Path $platformDir "$folder\$pluginName")
    }
    New-Item -ItemType Directory -Path (Join-Path $platformDir 'tools') | Out-Null
    Copy-Item $Output.Tool (Join-Path $platformDir "tools\$ToolName")

    # Release zip: the platform folders plus the include, example and docs.
    $staging = Join-Path $BuildRoot "package-$Platform"
    Reset-Directory $staging
    Copy-Item (Join-Path $platformDir '*') $staging -Recurse
    Copy-Item (Join-Path $DistRoot 'include') $staging -Recurse
    Copy-Item (Join-Path $DistRoot 'examples') $staging -Recurse
    Copy-Item (Join-Path $Root 'README.md'), (Join-Path $Root 'README.pt-BR.md') $staging
    $zip = Join-Path $DistRoot "audio-cast-$Version-$Platform-x86.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $zip
    Remove-Item $staging -Recurse -Force
}

# ---------------------------------------------------------------------------

$started = Get-Date
$cmake = Find-CMake
Write-Host "AudioCast $Version - target: $Target"
Write-Host "CMake: $cmake"

$windows = $null
$linux = $null
if ($Target -in 'all', 'windows') { $windows = Build-Windows $cmake }
if ($Target -in 'all', 'linux') { $linux = Build-Linux $cmake }

Write-Step 'Collecting files in dist/'
New-Item -ItemType Directory -Path $DistRoot -Force | Out-Null
Reset-Directory (Join-Path $DistRoot 'include')
Reset-Directory (Join-Path $DistRoot 'examples')
Copy-Item (Join-Path $Root 'audio-cast.inc') (Join-Path $DistRoot 'include')
Copy-Item (Join-Path $Root 'examples\*.pwn') (Join-Path $DistRoot 'examples')
# Zips of an older version would only cause confusion.
Get-ChildItem $DistRoot -Filter 'audio-cast-*.zip' | Where-Object { $_.Name -notlike "audio-cast-$Version-*" } | Remove-Item -Force

if ($windows) { Publish-Platform 'windows' $windows 'audio-cast-check.exe' }
if ($linux) { Publish-Platform 'linux' $linux 'audio-cast-check' }

$elapsed = (Get-Date) - $started
Write-Host ''
Write-Host "Done in $([int]$elapsed.TotalMinutes)m $($elapsed.Seconds)s:" -ForegroundColor Green
Get-ChildItem $DistRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
    $relative = $_.FullName.Substring($DistRoot.Length + 1)
    '  dist\{0,-48} {1,8:N0} KB' -f $relative, ($_.Length / 1KB)
}
