<#
.SYNOPSIS
    Builds the Milah desktop application on Windows with a Qt 6 MinGW kit.

.DESCRIPTION
    Finds Qt, MinGW, CMake and Ninja (preferring the copies bundled with a Qt
    installation), then hands over to CMake: the windows-mingw preset in
    app/CMakePresets.json configures and builds, ctest runs the suite, and
    `cmake --install` assembles a portable folder with Qt's own deployment
    script. QuaZip and zlib are fetched and built by CMake itself.

    Qt is looked for under C:\Qt first and then %USERPROFILE%\Qt, which is where
    the online installer puts a system-wide and a per-user installation
    respectively. -QtRoot pins the search to one directory instead.

    Nothing needs to be on PATH beforehand. On Linux and macOS, or with the
    tools already on PATH, the presets can be used directly:

        cmake --preset windows-mingw -DCMAKE_PREFIX_PATH=<Qt kit>
        cmake --build --preset windows-mingw
        ctest --preset windows-mingw

.EXAMPLE
    .\build_milah_windows.ps1 -Tests

.EXAMPLE
    .\build_milah_windows.ps1 -Clean -Configuration Debug
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',

    [string]$QtBin,
    # Empty means "search the usual places": see Get-QtRootCandidates.
    [string]$QtRoot,
    [string]$MinGwBin,
    [string]$CMake,
    [string]$Ninja,

    [string]$AppSourceDir,
    [string]$BuildRoot,
    [string]$PortableDir,

    [switch]$Tests,
    [switch]$SkipPortable,
    [switch]$Clean,
    # Build from the QuaZip and zlib sources CMake already fetched, without
    # touching the network.
    [switch]$NoDownload
)

$ErrorActionPreference = 'Stop'

function Write-Step {
    param([string]$Message)
    Write-Host ''
    Write-Host "==> $Message"
}

function Resolve-RequiredPath {
    param(
        [string]$Path,
        [string]$Description
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "$Description was not provided."
    }

    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction SilentlyContinue
    if (-not $resolved) {
        throw "$Description was not found: $Path"
    }

    return $resolved.ProviderPath
}

function Convert-ToCMakePath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $Path
    }

    return $Path.Replace('\', '/')
}

function Normalize-CMakePath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $Path
    }

    return $Path.Replace('/', '\').TrimEnd('\')
}

function Test-CMakePathEquals {
    param(
        [string]$Actual,
        [string]$Expected
    )

    if ([string]::IsNullOrWhiteSpace($Actual) -and [string]::IsNullOrWhiteSpace($Expected)) {
        return $true
    }

    return (Normalize-CMakePath $Actual) -eq (Normalize-CMakePath $Expected)
}

function Get-CMakeCacheValue {
    param(
        [string]$CachePath,
        [string]$Name
    )

    if (-not (Test-Path -LiteralPath $CachePath -PathType Leaf)) {
        return $null
    }

    $escapedName = [regex]::Escape($Name)
    $line = Get-Content -LiteralPath $CachePath |
        Where-Object { $_ -match "^$escapedName(?::[^=]+)?=(.*)$" } |
        Select-Object -First 1

    if ($line -match "^$escapedName(?::[^=]+)?=(.*)$") {
        return $Matches[1]
    }

    return $null
}

function Invoke-LoggedCommand {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory
    )

    $display = "$FilePath $($Arguments -join ' ')"
    Write-Host $display

    Push-Location $WorkingDirectory
    # Tools like cmake write progress and warnings to stderr. Under
    # $ErrorActionPreference = 'Stop' that would abort the build as soon as the
    # stream is redirected, so success is judged by the exit code alone.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        # Straight to the host, so a caller inside a function does not collect
        # the tool's output as part of that function's return value.
        & $FilePath @Arguments | Out-Host
        if ($LASTEXITCODE -ne 0) {
            $ErrorActionPreference = $previousPreference
            throw "Command failed with exit code $LASTEXITCODE`: $display"
        }
    }
    finally {
        $ErrorActionPreference = $previousPreference
        Pop-Location
    }
}

function Convert-ToVersion {
    param([string]$Text)

    try {
        return [Version]$Text
    }
    catch {
        return [Version]'0.0.0'
    }
}

function Test-QtBin {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Container)) {
        return $false
    }

    foreach ($tool in @('qmake.exe', 'windeployqt.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Path $tool) -PathType Leaf)) {
            return $false
        }
    }

    return $true
}

function Get-QtRootCandidates {
    param([string]$RequestedRoot)

    # An explicit -QtRoot means that directory and no other: someone naming a
    # root wants the kit in it, not whichever one happens to be found first.
    if (-not [string]::IsNullOrWhiteSpace($RequestedRoot)) {
        return @($RequestedRoot)
    }

    # The system-wide installation leads, with the per-user directory behind it:
    # where a machine has both, C:\Qt is the shared one to build against.
    return @('C:\Qt', (Join-Path $HOME 'Qt'))
}

function Resolve-QtBin {
    param(
        [string]$RequestedQtBin,
        [string[]]$Roots
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedQtBin)) {
        $resolvedQtBin = Resolve-RequiredPath $RequestedQtBin 'Qt bin directory'
        if (-not (Test-QtBin $resolvedQtBin)) {
            throw "Qt bin directory is missing qmake.exe or windeployqt.exe: $resolvedQtBin"
        }
        $parent = Split-Path -Parent (Split-Path -Parent $resolvedQtBin)
        return [PSCustomObject]@{
            Bin = $resolvedQtBin
            Root = (Split-Path -Parent $parent)
        }
    }

    foreach ($candidateRoot in $Roots) {
        if ([string]::IsNullOrWhiteSpace($candidateRoot) -or
            -not (Test-Path -LiteralPath $candidateRoot -PathType Container)) {
            continue
        }

        $resolvedRoot = (Resolve-Path -LiteralPath $candidateRoot).ProviderPath
        $candidates = Get-ChildItem -LiteralPath $resolvedRoot -Directory -ErrorAction SilentlyContinue |
            ForEach-Object {
                $versionDir = $_
                Get-ChildItem -LiteralPath $versionDir.FullName -Directory -Filter 'mingw*' -ErrorAction SilentlyContinue |
                    ForEach-Object {
                        $binDir = Join-Path $_.FullName 'bin'
                        if (Test-QtBin $binDir) {
                            [PSCustomObject]@{
                                Bin = (Resolve-Path -LiteralPath $binDir).ProviderPath
                                Root = $resolvedRoot
                                Version = Convert-ToVersion $versionDir.Name
                                IsQt6 = $versionDir.Name -like '6.*'
                                Is64Bit = $_.Name -match '(^|_)64($|_)|64$'
                            }
                        }
                    }
            }

        $selected = $candidates |
            Sort-Object -Property @{ Expression = 'IsQt6'; Descending = $true },
                                  @{ Expression = 'Version'; Descending = $true },
                                  @{ Expression = 'Is64Bit'; Descending = $true },
                                  @{ Expression = 'Bin'; Descending = $false } |
            Select-Object -First 1

        if ($selected) {
            return $selected
        }
    }

    throw "No Qt MinGW kit with qmake.exe and windeployqt.exe was found under: $($Roots -join ', '). Pass -QtBin, for example -QtBin `"$HOME\Qt\6.11.1\mingw_64\bin`"."
}

function Test-MinGwBin {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Container)) {
        return $false
    }

    foreach ($tool in @('gcc.exe', 'g++.exe', 'mingw32-make.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Path $tool) -PathType Leaf)) {
            return $false
        }
    }

    return $true
}

function Resolve-MinGwBin {
    param(
        [string]$RequestedMinGwBin,
        [string[]]$QtRootPaths
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedMinGwBin)) {
        $resolved = Resolve-RequiredPath $RequestedMinGwBin 'MinGW bin directory'
        if (-not (Test-MinGwBin $resolved)) {
            throw "MinGW bin directory is missing gcc.exe, g++.exe or mingw32-make.exe: $resolved"
        }
        return $resolved
    }

    # Every candidate root is tried, not just the one the kit came from: Qt and
    # its Tools directory are installed separately and need not share a root.
    $searched = @()
    foreach ($qtRootPath in $QtRootPaths) {
        $toolsRoot = Join-Path $qtRootPath 'Tools'
        $searched += $toolsRoot
        if (-not (Test-Path -LiteralPath $toolsRoot -PathType Container)) {
            continue
        }

        $selected = Get-ChildItem -LiteralPath $toolsRoot -Directory -Filter 'mingw*_64' -ErrorAction SilentlyContinue |
            ForEach-Object {
                $binDir = Join-Path $_.FullName 'bin'
                if (Test-MinGwBin $binDir) {
                    $versionText = '0'
                    if ($_.Name -match 'mingw(\d+)') { $versionText = $Matches[1] }
                    [PSCustomObject]@{
                        Bin = (Resolve-Path -LiteralPath $binDir).ProviderPath
                        Version = [int64]$versionText
                    }
                }
            } |
            Sort-Object -Property @{ Expression = 'Version'; Descending = $true } |
            Select-Object -First 1

        if ($selected) {
            return $selected.Bin
        }
    }

    throw "No Qt-bundled MinGW toolchain was found under: $($searched -join ', '). Pass -MinGwBin, for example -MinGwBin `"$HOME\Qt\Tools\mingw1310_64\bin`"."
}

function Resolve-BundledTool {
    param(
        [string]$RequestedPath,
        [string[]]$QtRootPaths,
        [string[]]$RelativeCandidates,
        [string]$CommandName,
        [string]$Description
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        return Resolve-RequiredPath $RequestedPath $Description
    }

    # Root order first, then the alternatives within a root.
    foreach ($qtRootPath in $QtRootPaths) {
        foreach ($relative in $RelativeCandidates) {
            $candidate = Join-Path $qtRootPath $relative
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return (Resolve-Path -LiteralPath $candidate).ProviderPath
            }
        }
    }

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "$Description was not found under $($QtRootPaths -join ', ') or on PATH. Install it, or pass the matching parameter explicitly."
}

function Assert-PortableFolder {
    param([string]$PortableRoot)

    $required = @(
        'Milah.exe',
        'Qt6Core.dll',
        'Qt6Gui.dll',
        'Qt6Widgets.dll',
        'plugins\platforms\qwindows.dll',
        'qt.conf',
        # The lexicon is read from disk rather than compiled in, so a copy
        # without it silently loses its Strong's numbers.
        'data\hebrew_lexicon.json'
    )
    $missing = $required | Where-Object {
        -not (Test-Path -LiteralPath (Join-Path $PortableRoot $_) -PathType Leaf)
    }
    if ($missing) {
        throw "The portable folder is incomplete under $PortableRoot. Missing: $($missing -join ', ')."
    }
}

# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path -LiteralPath $ScriptDir).ProviderPath

Write-Host "Repository root: $RepoRoot"

function Resolve-OptionalDirectory {
    param([string]$Requested, [string]$Default)
    if ([string]::IsNullOrWhiteSpace($Requested)) {
        return $Default
    }
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Requested)
}

$AppSourceDir = Resolve-OptionalDirectory $AppSourceDir (Join-Path $RepoRoot 'app')
$BuildRoot = Resolve-OptionalDirectory $BuildRoot (Join-Path $RepoRoot 'build')
$PortableDir = Resolve-OptionalDirectory $PortableDir (Join-Path $RepoRoot 'milah-portable')

$ResolvedAppSourceDir = Resolve-RequiredPath $AppSourceDir 'Milah app source directory'
Resolve-RequiredPath (Join-Path $ResolvedAppSourceDir 'CMakePresets.json') 'Milah CMake presets' | Out-Null

Write-Step 'Resolving toolchain'

$OriginalPath = $env:PATH
$QtRootCandidates = Get-QtRootCandidates -RequestedRoot $QtRoot
$QtInfo = Resolve-QtBin -RequestedQtBin $QtBin -Roots $QtRootCandidates
$QtBin = $QtInfo.Bin
$ResolvedQtRoot = $QtInfo.Root
$QtPrefixPath = (Resolve-Path -LiteralPath (Join-Path $QtBin '..')).ProviderPath

# The root the kit came from leads, with the remaining candidates behind it, so
# a toolchain installed under a different root is still found.
$ToolSearchRoots = @($ResolvedQtRoot) + ($QtRootCandidates | Where-Object {
    -not (Test-CMakePathEquals -Actual $_ -Expected $ResolvedQtRoot)
})

$MinGwBin = Resolve-MinGwBin -RequestedMinGwBin $MinGwBin -QtRootPaths $ToolSearchRoots

$CMakeCommand = Resolve-BundledTool `
    -RequestedPath $CMake `
    -QtRootPaths $ToolSearchRoots `
    -RelativeCandidates @('Tools\CMake_64\bin\cmake.exe', 'Tools\CMake\bin\cmake.exe') `
    -CommandName 'cmake' `
    -Description 'cmake executable'

$NinjaCommand = Resolve-BundledTool `
    -RequestedPath $Ninja `
    -QtRootPaths $ToolSearchRoots `
    -RelativeCandidates @('Tools\Ninja\ninja.exe') `
    -CommandName 'ninja' `
    -Description 'ninja executable'

$CTestCommand = Join-Path (Split-Path -Parent $CMakeCommand) 'ctest.exe'

# Qt's deployment script runs windeployqt, which finds the MinGW runtime DLLs
# it copies through PATH.
$env:PATH = "$QtBin;$MinGwBin;$OriginalPath"

Write-Host "Qt kit:      $QtBin"
Write-Host "MinGW:       $MinGwBin"
Write-Host "CMake:       $CMakeCommand"
Write-Host "Ninja:       $NinjaCommand"
Write-Host "App source:  $ResolvedAppSourceDir"

$AppBuildDir = Join-Path $BuildRoot ("milah-" + $Configuration.ToLowerInvariant())
# Outside the per-configuration build directory, so -Clean and a Debug/Release
# switch do not download QuaZip and zlib again.
$FetchContentDir = Join-Path $BuildRoot '_deps'
$CxxCompiler = Join-Path $MinGwBin 'g++.exe'

if ($Clean) {
    Write-Step 'Cleaning previous build output'
    foreach ($stale in @($AppBuildDir, $PortableDir)) {
        if (Test-Path -LiteralPath $stale -PathType Container) {
            Write-Host "Removing $stale"
            Remove-Item -LiteralPath $stale -Recurse -Force
        }
    }
    Write-Host "Kept dependency cache: $FetchContentDir"
}

# A cache from another compiler (or from the pre-preset script, which linked an
# installed QuaZip) cannot be reconfigured in place.
$appCachePath = Join-Path $AppBuildDir 'CMakeCache.txt'
if (Test-Path -LiteralPath $appCachePath -PathType Leaf) {
    $cachedCxx = Get-CMakeCacheValue -CachePath $appCachePath -Name 'CMAKE_CXX_COMPILER'
    $cachedQuaZip = Get-CMakeCacheValue -CachePath $appCachePath -Name 'QuaZip-Qt6_DIR'
    if (-not (Test-CMakePathEquals -Actual $cachedCxx -Expected $CxxCompiler) -or
        ($cachedQuaZip -and $cachedQuaZip -notmatch 'NOTFOUND|pkgRedirects')) {
        Write-Host "Discarding stale Milah CMake cache: $AppBuildDir"
        Remove-Item -LiteralPath $AppBuildDir -Recurse -Force
    }
}

Write-Step "Configuring Milah ($Configuration)"

$TestsFlag = 'OFF'
if ($Tests) { $TestsFlag = 'ON' }
$OfflineFlag = 'OFF'
if ($NoDownload) { $OfflineFlag = 'ON' }

New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
Invoke-LoggedCommand -FilePath $CMakeCommand -WorkingDirectory $ResolvedAppSourceDir -Arguments @(
    '--preset', 'windows-mingw',
    '-B', (Convert-ToCMakePath $AppBuildDir),
    "-DCMAKE_MAKE_PROGRAM=$(Convert-ToCMakePath $NinjaCommand)",
    "-DCMAKE_C_COMPILER=$(Convert-ToCMakePath (Join-Path $MinGwBin 'gcc.exe'))",
    "-DCMAKE_CXX_COMPILER=$(Convert-ToCMakePath $CxxCompiler)",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_PREFIX_PATH=$(Convert-ToCMakePath $QtPrefixPath)",
    "-DMILAH_BUILD_TESTS=$TestsFlag",
    "-DMILAH_REPO_ROOT=$(Convert-ToCMakePath $RepoRoot)",
    "-DFETCHCONTENT_BASE_DIR=$(Convert-ToCMakePath $FetchContentDir)",
    "-DFETCHCONTENT_FULLY_DISCONNECTED=$OfflineFlag"
)

Write-Step "Building Milah ($Configuration)"
Invoke-LoggedCommand -FilePath $CMakeCommand -Arguments @('--build', (Convert-ToCMakePath $AppBuildDir)) -WorkingDirectory $BuildRoot

if ($Tests) {
    Write-Step 'Running tests'
    if (-not (Test-Path -LiteralPath $CTestCommand -PathType Leaf)) {
        throw "ctest.exe was not found next to cmake.exe: $CTestCommand"
    }
    Invoke-LoggedCommand -FilePath $CTestCommand -Arguments @('--test-dir', (Convert-ToCMakePath $AppBuildDir), '--output-on-failure') -WorkingDirectory $BuildRoot
}

if ($SkipPortable) {
    Write-Step 'Skipping portable packaging'
    Write-Host "Milah executable: $(Join-Path $AppBuildDir 'Milah.exe')"
    return
}

Write-Step 'Assembling portable Milah folder'

# Rebuilding with the app still open is an easy thing to do, and Windows will
# not let a running executable be overwritten. Say so, rather than surfacing the
# raw file-sharing error from inside the install step.
$PortableExe = Join-Path $PortableDir 'Milah.exe'
if (Test-Path -LiteralPath $PortableExe -PathType Leaf) {
    try {
        $handle = [System.IO.File]::Open($PortableExe, 'Open', 'ReadWrite', 'None')
        $handle.Close()
    }
    catch [System.IO.IOException] {
        throw "Could not replace $PortableExe because it is in use. Milah is most likely still running: close it and run this script again, or pass -SkipPortable to build without repackaging."
    }
}

# Into an empty folder, so nothing an older build left there (a QuaZip DLL from
# before it was linked statically, plugins from before they moved under
# plugins\) can keep the copy working by accident.
if (Test-Path -LiteralPath $PortableDir -PathType Container) {
    Remove-Item -LiteralPath $PortableDir -Recurse -Force
}

Invoke-LoggedCommand -FilePath $CMakeCommand -WorkingDirectory $BuildRoot -Arguments @(
    '--install', (Convert-ToCMakePath $AppBuildDir),
    '--prefix', (Convert-ToCMakePath $PortableDir)
)

Assert-PortableFolder -PortableRoot $PortableDir

Write-Step 'Build complete'
Write-Host "Your portable Milah executable is: $PortableExe"
Write-Host 'Do not launch Milah.exe from the build folder unless the Qt runtime DLLs are on PATH.'
