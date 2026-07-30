<#
.SYNOPSIS
    Builds the Milah desktop application on Windows with a Qt 6 MinGW kit.

.DESCRIPTION
    Discovers Qt, MinGW, CMake and Ninja (preferring the copies bundled with a
    Qt installation), builds the ZLIB and QuaZip dependencies, configures and
    builds Milah, optionally runs the test suite, and assembles a portable
    folder with windeployqt.

    Nothing needs to be on PATH beforehand.

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
    [string]$QtRoot = 'C:\Qt',
    [string]$MinGwBin,
    [string]$CMake,
    [string]$Ninja,

    [string]$AppSourceDir,
    [string]$BuildRoot,
    [string]$PortableDir,

    [switch]$Tests,
    [switch]$SkipPortable,
    [switch]$KeepBuildArtifacts,
    [switch]$Clean,
    [switch]$NoDownload,

    [string]$ZlibVersion = '1.3.2',
    [string]$ZlibUrl,
    [string]$ZlibSha256,
    [switch]$SkipZlibBuild,
    [string]$ZlibRoot,

    [string]$QuaZipVersion = '1.7.1',
    [string]$QuaZipUrl,
    [string]$QuaZipSha256,
    [switch]$SkipQuaZipBuild,
    [string]$QuaZipRoot
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

function Resolve-QtBin {
    param(
        [string]$RequestedQtBin,
        [string]$Root,
        [switch]$UseUserProfileFallback
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

    $candidateRoots = @($Root)
    if ($UseUserProfileFallback) {
        $candidateRoots += (Join-Path $HOME 'Qt')
    }

    foreach ($candidateRoot in $candidateRoots) {
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

    throw "No Qt MinGW kit with qmake.exe and windeployqt.exe was found under: $($candidateRoots -join ', '). Pass -QtBin, for example -QtBin `"$HOME\Qt\6.11.1\mingw_64\bin`"."
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
        [string]$QtRootPath
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedMinGwBin)) {
        $resolved = Resolve-RequiredPath $RequestedMinGwBin 'MinGW bin directory'
        if (-not (Test-MinGwBin $resolved)) {
            throw "MinGW bin directory is missing gcc.exe, g++.exe or mingw32-make.exe: $resolved"
        }
        return $resolved
    }

    $toolsRoot = Join-Path $QtRootPath 'Tools'
    if (-not (Test-Path -LiteralPath $toolsRoot -PathType Container)) {
        throw "MinGW bin directory was not provided and the Qt Tools directory was not found: $toolsRoot. Pass -MinGwBin, for example -MinGwBin `"$HOME\Qt\Tools\mingw1310_64\bin`"."
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

    if (-not $selected) {
        throw "No Qt-bundled MinGW toolchain was found under $toolsRoot. Pass -MinGwBin, for example -MinGwBin `"$HOME\Qt\Tools\mingw1310_64\bin`"."
    }

    return $selected.Bin
}

function Resolve-BundledTool {
    param(
        [string]$RequestedPath,
        [string]$QtRootPath,
        [string[]]$RelativeCandidates,
        [string]$CommandName,
        [string]$Description
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        return Resolve-RequiredPath $RequestedPath $Description
    }

    foreach ($relative in $RelativeCandidates) {
        $candidate = Join-Path $QtRootPath $relative
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).ProviderPath
        }
    }

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "$Description was not found in the Qt installation or on PATH. Install it, or pass the matching parameter explicitly."
}

function Get-FileSha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-CachedDownload {
    param(
        [string]$Url,
        [string]$Sha256,
        [string]$DownloadsDir,
        [string]$Description,
        [string]$FileNameOverride,
        [switch]$Offline
    )

    if ([string]::IsNullOrWhiteSpace($Url)) {
        throw "$Description URL was not provided."
    }

    New-Item -ItemType Directory -Force -Path $DownloadsDir | Out-Null

    $fileName = $FileNameOverride
    if ([string]::IsNullOrWhiteSpace($fileName)) {
        $fileName = Split-Path -Leaf ([Uri]$Url).AbsolutePath
    }
    if ([string]::IsNullOrWhiteSpace($fileName)) {
        throw "Could not determine a file name for $Description from URL: $Url"
    }

    $downloadPath = Join-Path $DownloadsDir $fileName
    if (-not (Test-Path -LiteralPath $downloadPath)) {
        if ($Offline) {
            throw "$Description is missing and -NoDownload was set: $downloadPath"
        }
        Write-Host "Downloading $Description from $Url"
        Invoke-WebRequest -Uri $Url -OutFile $downloadPath
    }
    else {
        Write-Host "Using cached $Description`: $downloadPath"
    }

    if (-not [string]::IsNullOrWhiteSpace($Sha256)) {
        $actualHash = Get-FileSha256 $downloadPath
        if ($actualHash -ne $Sha256.ToLowerInvariant()) {
            throw "$Description SHA256 mismatch for $downloadPath. Expected $Sha256 but found $actualHash."
        }
    }

    return $downloadPath
}

function Expand-DependencyArchive {
    param(
        [string]$ArchivePath,
        [string]$DestinationPath,
        [string]$Description
    )

    New-Item -ItemType Directory -Force -Path $DestinationPath | Out-Null

    if ($ArchivePath.EndsWith('.zip', [System.StringComparison]::OrdinalIgnoreCase)) {
        Expand-Archive -LiteralPath $ArchivePath -DestinationPath $DestinationPath -Force
        return
    }

    if ($ArchivePath.EndsWith('.tar.gz', [System.StringComparison]::OrdinalIgnoreCase) -or
        $ArchivePath.EndsWith('.tgz', [System.StringComparison]::OrdinalIgnoreCase) -or
        $ArchivePath.EndsWith('.tar', [System.StringComparison]::OrdinalIgnoreCase)) {
        $tarCommand = Get-Command 'tar' -ErrorAction SilentlyContinue
        if (-not $tarCommand) {
            throw "tar was not found on PATH; it is required to unpack the $Description archive."
        }
        Invoke-LoggedCommand -FilePath $tarCommand.Source -Arguments @('-xf', $ArchivePath, '-C', $DestinationPath) -WorkingDirectory $DestinationPath
        return
    }

    throw "Unsupported $Description archive type: $ArchivePath"
}

function Find-ExtractedSourceDir {
    param([string]$ExtractRoot)

    $sourceDir = Get-ChildItem -LiteralPath $ExtractRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'CMakeLists.txt') } |
        Select-Object -First 1

    if (-not $sourceDir) {
        return $null
    }

    return $sourceDir.FullName
}

function Get-ExtractedSourceDir {
    param(
        [string]$ArchivePath,
        [string]$ExtractRoot,
        [string]$Description
    )

    # Keyed on the unpacked sources actually being there, not on the directory
    # existing: an interrupted run leaves an empty extract directory behind, and
    # testing only for that would skip the extraction forever after.
    $sourceDir = Find-ExtractedSourceDir -ExtractRoot $ExtractRoot
    if ($sourceDir) {
        return $sourceDir
    }

    Expand-DependencyArchive `
        -ArchivePath $ArchivePath `
        -DestinationPath $ExtractRoot `
        -Description $Description

    $sourceDir = Find-ExtractedSourceDir -ExtractRoot $ExtractRoot
    if (-not $sourceDir) {
        throw "Could not find a $Description CMake source directory under: $ExtractRoot"
    }

    return $sourceDir
}

function Invoke-DependencyCMake {
    param(
        [string]$CMakeCommand,
        [string]$NinjaCommand,
        [string]$SourceDir,
        [string]$BuildDir,
        [string]$InstallDir,
        [string]$ToolchainBin,
        [string]$ConfigurationName,
        [string[]]$ExtraArguments,
        [string]$WorkRoot
    )

    $cCompiler = Join-Path $ToolchainBin 'gcc.exe'
    $cxxCompiler = Join-Path $ToolchainBin 'g++.exe'

    $cachePath = Join-Path $BuildDir 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
        $cacheIsStale =
            (-not (Test-CMakePathEquals -Actual (Get-CMakeCacheValue -CachePath $cachePath -Name 'CMAKE_INSTALL_PREFIX') -Expected $InstallDir)) -or
            (-not (Test-CMakePathEquals -Actual (Get-CMakeCacheValue -CachePath $cachePath -Name 'CMAKE_C_COMPILER') -Expected $cCompiler)) -or
            (-not (Test-CMakePathEquals -Actual (Get-CMakeCacheValue -CachePath $cachePath -Name 'CMAKE_MAKE_PROGRAM') -Expected $NinjaCommand))

        if ($cacheIsStale) {
            Write-Host "Discarding stale CMake build cache: $BuildDir"
            Remove-Item -LiteralPath $BuildDir -Recurse -Force
        }
    }

    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

    $arguments = @(
        '-S', (Convert-ToCMakePath $SourceDir),
        '-B', (Convert-ToCMakePath $BuildDir),
        '-G', 'Ninja',
        "-DCMAKE_MAKE_PROGRAM=$(Convert-ToCMakePath $NinjaCommand)",
        "-DCMAKE_C_COMPILER=$(Convert-ToCMakePath $cCompiler)",
        "-DCMAKE_CXX_COMPILER=$(Convert-ToCMakePath $cxxCompiler)",
        "-DCMAKE_BUILD_TYPE=$ConfigurationName",
        "-DCMAKE_INSTALL_PREFIX=$(Convert-ToCMakePath $InstallDir)"
    ) + $ExtraArguments

    Invoke-LoggedCommand -FilePath $CMakeCommand -Arguments $arguments -WorkingDirectory $WorkRoot
    Invoke-LoggedCommand -FilePath $CMakeCommand -Arguments @('--build', (Convert-ToCMakePath $BuildDir)) -WorkingDirectory $WorkRoot
    Invoke-LoggedCommand -FilePath $CMakeCommand -Arguments @('--install', (Convert-ToCMakePath $BuildDir)) -WorkingDirectory $WorkRoot
}

function Build-Zlib {
    param(
        [string]$Version,
        [string]$Url,
        [string]$Sha256,
        [string]$DownloadsDir,
        [string]$WorkRoot,
        [string]$CMakeCommand,
        [string]$NinjaCommand,
        [string]$ToolchainBin,
        [string]$ConfigurationName,
        [switch]$Offline
    )

    Write-Step "Preparing ZLIB $Version"

    if ([string]::IsNullOrWhiteSpace($Url)) {
        $Url = "https://github.com/madler/zlib/releases/download/v$Version/zlib-$Version.tar.gz"
    }

    $archivePath = Get-CachedDownload `
        -Url $Url `
        -Sha256 $Sha256 `
        -DownloadsDir $DownloadsDir `
        -Description 'ZLIB archive' `
        -Offline:$Offline

    $extractRoot = Join-Path $WorkRoot 'src'
    $buildDir = Join-Path $WorkRoot 'build'
    $installDir = Join-Path $WorkRoot 'install'

    $sourceDir = Get-ExtractedSourceDir `
        -ArchivePath $archivePath `
        -ExtractRoot $extractRoot `
        -Description 'ZLIB'

    Invoke-DependencyCMake `
        -CMakeCommand $CMakeCommand `
        -NinjaCommand $NinjaCommand `
        -SourceDir $sourceDir `
        -BuildDir $buildDir `
        -InstallDir $installDir `
        -ToolchainBin $ToolchainBin `
        -ConfigurationName $ConfigurationName `
        -ExtraArguments @() `
        -WorkRoot $WorkRoot

    return $installDir
}

function Get-ZlibRuntimeDll {
    param([string]$Root)

    foreach ($pattern in @('libz.dll', 'libzlib.dll', 'zlib1.dll', 'zlib.dll')) {
        $file = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter $pattern -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($file) {
            return $file.FullName
        }
    }

    return $null
}

function Get-QuaZipRuntimeDll {
    param([string]$Root)

    foreach ($pattern in @('libquazip1-qt6.dll', 'quazip1-qt6.dll', 'libquazip*.dll', 'quazip*.dll')) {
        $file = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter $pattern -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($file) {
            return $file.FullName
        }
    }

    return $null
}

function Build-QuaZip {
    param(
        [string]$Version,
        [string]$Url,
        [string]$Sha256,
        [string]$DownloadsDir,
        [string]$WorkRoot,
        [string]$CMakeCommand,
        [string]$NinjaCommand,
        [string]$ToolchainBin,
        [string]$QtPrefixPath,
        [string]$ZlibInstallRoot,
        [string]$ConfigurationName,
        [switch]$Offline
    )

    Write-Step "Preparing QuaZIP $Version"

    if ([string]::IsNullOrWhiteSpace($Url)) {
        $Url = "https://github.com/stachenov/quazip/archive/refs/tags/v$Version.zip"
    }

    $archivePath = Get-CachedDownload `
        -Url $Url `
        -Sha256 $Sha256 `
        -DownloadsDir $DownloadsDir `
        -Description 'QuaZIP archive' `
        -FileNameOverride "quazip-$Version.zip" `
        -Offline:$Offline

    $extractRoot = Join-Path $WorkRoot 'src'
    $buildDir = Join-Path $WorkRoot 'build'
    $installDir = Join-Path $WorkRoot 'install'

    $sourceDir = Get-ExtractedSourceDir `
        -ArchivePath $archivePath `
        -ExtractRoot $extractRoot `
        -Description 'QuaZIP'

    $utf8NoBom = New-Object System.Text.UTF8Encoding -ArgumentList $false

    # QuaZip reaches for Qt6::Core5Compat to get QTextCodec. A Qt kit installed
    # without the Qt 5 Compatibility Module does not have it, and QuaZip falls
    # back to QStringConverter perfectly well, so the detection is disabled.
    $quazipCMakeLists = Join-Path $sourceDir 'CMakeLists.txt'
    $cmakeListsText = Get-Content -LiteralPath $quazipCMakeLists -Raw
    if ($cmakeListsText -match '(?m)^\s*find_package[^\n]*Core5Compat') {
        Write-Host 'Patching QuaZIP CMakeLists.txt: disabling Core5Compat detection'
        $patchedText = $cmakeListsText -replace '(?m)^(\s*find_package[^\n]*Core5Compat[^\n]*)', '# $1'
        [System.IO.File]::WriteAllText($quazipCMakeLists, $patchedText, $utf8NoBom)
    }

    # The installed QuaZip-Qt6Config.cmake demands Core5Compat unconditionally,
    # whatever QUAZIP_ENABLE_QTEXTCODEC was set to, so every consumer would fail
    # to configure. Patch the template the config is generated from.
    $quazipConfigTemplate = Join-Path $sourceDir 'quazip\QuaZipConfig.cmake.in'
    if (Test-Path -LiteralPath $quazipConfigTemplate -PathType Leaf) {
        $templateText = Get-Content -LiteralPath $quazipConfigTemplate -Raw
        if ($templateText -match 'COMPONENTS\s+Core\s+Core5Compat') {
            Write-Host 'Patching QuaZIP package config template: dropping the Core5Compat dependency'
            $patchedTemplate = $templateText -replace 'COMPONENTS\s+Core\s+Core5Compat', 'COMPONENTS Core'
            [System.IO.File]::WriteAllText($quazipConfigTemplate, $patchedTemplate, $utf8NoBom)
        }
    }

    $prefixPath = @($QtPrefixPath, $ZlibInstallRoot) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        ForEach-Object { Convert-ToCMakePath $_ }

    Invoke-DependencyCMake `
        -CMakeCommand $CMakeCommand `
        -NinjaCommand $NinjaCommand `
        -SourceDir $sourceDir `
        -BuildDir $buildDir `
        -InstallDir $installDir `
        -ToolchainBin $ToolchainBin `
        -ConfigurationName $ConfigurationName `
        -ExtraArguments @(
            "-DCMAKE_PREFIX_PATH=$($prefixPath -join ';')",
            '-DQUAZIP_QT_MAJOR_VERSION=6',
            '-DQUAZIP_ENABLE_TESTS=OFF',
            '-DQUAZIP_ENABLE_QTEXTCODEC=OFF',
            '-DQUAZIP_BZIP2=OFF',
            '-DQUAZIP_INSTALL=ON',
            '-DBUILD_SHARED_LIBS=ON'
        ) `
        -WorkRoot $WorkRoot

    return $installDir
}

function Get-ObjdumpDllNames {
    param(
        [string]$ExecutablePath,
        [string]$ToolchainBin
    )

    $objdump = Join-Path $ToolchainBin 'objdump.exe'
    if (-not (Test-Path -LiteralPath $objdump -PathType Leaf)) {
        return @()
    }

    $names = @()
    $output = & $objdump -p $ExecutablePath 2>$null
    foreach ($line in $output) {
        if ($line -match 'DLL Name:\s*(.+)$') {
            $names += $Matches[1].Trim()
        }
    }

    return $names | Sort-Object -Unique
}

function Copy-ToolchainRuntimeDlls {
    param(
        [string]$ToolchainBin,
        [string]$PortableRoot,
        [string[]]$Executables
    )

    $runtimeDlls = [ordered]@{}
    foreach ($pattern in @('libgcc_s_*.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
        Get-ChildItem -LiteralPath $ToolchainBin -File -Filter $pattern -ErrorAction SilentlyContinue |
            ForEach-Object { $runtimeDlls[$_.Name.ToLowerInvariant()] = $_.FullName }
    }

    foreach ($executable in $Executables) {
        foreach ($dllName in (Get-ObjdumpDllNames -ExecutablePath $executable -ToolchainBin $ToolchainBin)) {
            $source = Join-Path $ToolchainBin $dllName
            if (Test-Path -LiteralPath $source -PathType Leaf) {
                $runtimeDlls[$dllName.ToLowerInvariant()] = (Resolve-Path -LiteralPath $source).ProviderPath
            }
        }
    }

    foreach ($source in $runtimeDlls.Values) {
        $destination = Join-Path $PortableRoot (Split-Path -Leaf $source)
        if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
            Write-Host "Copying toolchain runtime DLL: $(Split-Path -Leaf $source)"
            Copy-Item -LiteralPath $source -Destination $destination -Force
        }
    }
}

function Assert-PortableQtRuntime {
    param(
        [string]$PortableRoot,
        [string[]]$DllNames
    )

    $platformPlugin = Join-Path $PortableRoot 'platforms\qwindows.dll'
    if (-not (Test-Path -LiteralPath $platformPlugin -PathType Leaf)) {
        throw "windeployqt did not deploy platforms\qwindows.dll under $PortableRoot. Check that -QtBin points to the Qt kit that built Milah."
    }

    $missing = @()
    foreach ($dllName in $DllNames) {
        if (-not (Test-Path -LiteralPath (Join-Path $PortableRoot $dllName) -PathType Leaf)) {
            $missing += $dllName
        }
    }

    if ($missing.Count -gt 0) {
        throw "Qt runtime deployment is incomplete under $PortableRoot. Missing: $($missing -join ', ')."
    }
}

# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path -LiteralPath $ScriptDir).ProviderPath

Write-Host "Repository root: $RepoRoot"

if ([string]::IsNullOrWhiteSpace($AppSourceDir)) {
    $AppSourceDir = Join-Path $RepoRoot 'app'
}
else {
    $AppSourceDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($AppSourceDir)
}

if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path $RepoRoot 'build'
}
else {
    $BuildRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildRoot)
}

if ([string]::IsNullOrWhiteSpace($PortableDir)) {
    $PortableDir = Join-Path $RepoRoot 'milah-portable'
}
else {
    $PortableDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PortableDir)
}

$ResolvedAppSourceDir = Resolve-RequiredPath $AppSourceDir 'Milah app source directory'
Resolve-RequiredPath (Join-Path $ResolvedAppSourceDir 'CMakeLists.txt') 'Milah CMake project' | Out-Null

Write-Step 'Resolving toolchain'

$OriginalPath = $env:PATH
$QtRootWasProvided = $PSBoundParameters.ContainsKey('QtRoot')
$QtInfo = Resolve-QtBin -RequestedQtBin $QtBin -Root $QtRoot -UseUserProfileFallback:(-not $QtRootWasProvided)
$QtBin = $QtInfo.Bin
$ResolvedQtRoot = $QtInfo.Root
$QtPrefixPath = (Resolve-Path -LiteralPath (Join-Path $QtBin '..')).ProviderPath
$MinGwBin = Resolve-MinGwBin -RequestedMinGwBin $MinGwBin -QtRootPath $ResolvedQtRoot

$CMakeCommand = Resolve-BundledTool `
    -RequestedPath $CMake `
    -QtRootPath $ResolvedQtRoot `
    -RelativeCandidates @('Tools\CMake_64\bin\cmake.exe', 'Tools\CMake\bin\cmake.exe') `
    -CommandName 'cmake' `
    -Description 'cmake executable'

$NinjaCommand = Resolve-BundledTool `
    -RequestedPath $Ninja `
    -QtRootPath $ResolvedQtRoot `
    -RelativeCandidates @('Tools\Ninja\ninja.exe') `
    -CommandName 'ninja' `
    -Description 'ninja executable'

$CTestCommand = Join-Path (Split-Path -Parent $CMakeCommand) 'ctest.exe'
$WindeployqtCommand = Resolve-RequiredPath (Join-Path $QtBin 'windeployqt.exe') 'windeployqt executable'

$env:PATH = "$QtBin;$MinGwBin;$OriginalPath"

Write-Host "Qt kit:      $QtBin"
Write-Host "Qt root:     $ResolvedQtRoot"
Write-Host "MinGW:       $MinGwBin"
Write-Host "CMake:       $CMakeCommand"
Write-Host "Ninja:       $NinjaCommand"
Write-Host "App source:  $ResolvedAppSourceDir"

$DepsDir = Join-Path $BuildRoot 'deps'
$DownloadsDir = Join-Path $DepsDir 'downloads'
$ZlibWorkRoot = Join-Path $DepsDir "zlib-$ZlibVersion"
$QuaZipWorkRoot = Join-Path $DepsDir "quazip-$QuaZipVersion"
$AppBuildDir = Join-Path $BuildRoot ("milah-" + $Configuration.ToLowerInvariant())

if ($Clean) {
    Write-Step 'Cleaning previous build output'
    foreach ($stale in @($AppBuildDir, $PortableDir)) {
        if (Test-Path -LiteralPath $stale -PathType Container) {
            Write-Host "Removing $stale"
            Remove-Item -LiteralPath $stale -Recurse -Force
        }
    }
    Write-Host "Kept dependency cache: $DepsDir"
}

# --- ZLIB -----------------------------------------------------------------

if ($SkipZlibBuild) {
    if ([string]::IsNullOrWhiteSpace($ZlibRoot)) {
        throw '-SkipZlibBuild requires -ZlibRoot.'
    }
    $ZlibRoot = Resolve-RequiredPath $ZlibRoot 'ZLIB install root'
    Write-Step "Using existing ZLIB: $ZlibRoot"
}
elseif (-not [string]::IsNullOrWhiteSpace($ZlibRoot)) {
    $ZlibRoot = Resolve-RequiredPath $ZlibRoot 'ZLIB install root'
    Write-Step "Using existing ZLIB: $ZlibRoot"
}
else {
    $ZlibRoot = Build-Zlib `
        -Version $ZlibVersion `
        -Url $ZlibUrl `
        -Sha256 $ZlibSha256 `
        -DownloadsDir $DownloadsDir `
        -WorkRoot $ZlibWorkRoot `
        -CMakeCommand $CMakeCommand `
        -NinjaCommand $NinjaCommand `
        -ToolchainBin $MinGwBin `
        -ConfigurationName $Configuration `
        -Offline:$NoDownload
}

$ZlibDll = Get-ZlibRuntimeDll -Root $ZlibRoot
if ([string]::IsNullOrWhiteSpace($ZlibDll)) {
    throw "No ZLIB runtime DLL was found under: $ZlibRoot"
}

# --- QuaZIP ---------------------------------------------------------------

if ($SkipQuaZipBuild) {
    if ([string]::IsNullOrWhiteSpace($QuaZipRoot)) {
        throw '-SkipQuaZipBuild requires -QuaZipRoot.'
    }
    $QuaZipRoot = Resolve-RequiredPath $QuaZipRoot 'QuaZIP install root'
    Write-Step "Using existing QuaZIP: $QuaZipRoot"
}
elseif (-not [string]::IsNullOrWhiteSpace($QuaZipRoot)) {
    $QuaZipRoot = Resolve-RequiredPath $QuaZipRoot 'QuaZIP install root'
    Write-Step "Using existing QuaZIP: $QuaZipRoot"
}
else {
    $QuaZipRoot = Build-QuaZip `
        -Version $QuaZipVersion `
        -Url $QuaZipUrl `
        -Sha256 $QuaZipSha256 `
        -DownloadsDir $DownloadsDir `
        -WorkRoot $QuaZipWorkRoot `
        -CMakeCommand $CMakeCommand `
        -NinjaCommand $NinjaCommand `
        -ToolchainBin $MinGwBin `
        -QtPrefixPath $QtPrefixPath `
        -ZlibInstallRoot $ZlibRoot `
        -ConfigurationName $Configuration `
        -Offline:$NoDownload
}

$QuaZipDll = Get-QuaZipRuntimeDll -Root $QuaZipRoot
if ([string]::IsNullOrWhiteSpace($QuaZipDll)) {
    throw "No QuaZIP runtime DLL was found under: $QuaZipRoot"
}

# --- Milah ----------------------------------------------------------------

Write-Step "Configuring Milah ($Configuration)"

$PrefixPath = @($QtPrefixPath, $QuaZipRoot, $ZlibRoot) | ForEach-Object { Convert-ToCMakePath $_ }
$TestsFlag = 'OFF'
if ($Tests) { $TestsFlag = 'ON' }

$configureArguments = @(
    '-S', (Convert-ToCMakePath $ResolvedAppSourceDir),
    '-B', (Convert-ToCMakePath $AppBuildDir),
    '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$(Convert-ToCMakePath $NinjaCommand)",
    "-DCMAKE_C_COMPILER=$(Convert-ToCMakePath (Join-Path $MinGwBin 'gcc.exe'))",
    "-DCMAKE_CXX_COMPILER=$(Convert-ToCMakePath (Join-Path $MinGwBin 'g++.exe'))",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_PREFIX_PATH=$($PrefixPath -join ';')",
    "-DMILAH_BUILD_TESTS=$TestsFlag",
    "-DMILAH_REPO_ROOT=$(Convert-ToCMakePath $RepoRoot)"
)

$appCachePath = Join-Path $AppBuildDir 'CMakeCache.txt'
if (Test-Path -LiteralPath $appCachePath -PathType Leaf) {
    $cachedCxx = Get-CMakeCacheValue -CachePath $appCachePath -Name 'CMAKE_CXX_COMPILER'
    if (-not (Test-CMakePathEquals -Actual $cachedCxx -Expected (Join-Path $MinGwBin 'g++.exe'))) {
        Write-Host "Discarding stale Milah CMake cache: $AppBuildDir"
        Remove-Item -LiteralPath $AppBuildDir -Recurse -Force
    }
}

New-Item -ItemType Directory -Force -Path $AppBuildDir | Out-Null
Invoke-LoggedCommand -FilePath $CMakeCommand -Arguments $configureArguments -WorkingDirectory $BuildRoot

Write-Step "Building Milah ($Configuration)"
Invoke-LoggedCommand -FilePath $CMakeCommand -Arguments @('--build', (Convert-ToCMakePath $AppBuildDir)) -WorkingDirectory $BuildRoot

$MilahExe = Join-Path $AppBuildDir 'Milah.exe'
Resolve-RequiredPath $MilahExe 'Milah executable' | Out-Null

if ($Tests) {
    Write-Step 'Running tests'
    if (-not (Test-Path -LiteralPath $CTestCommand -PathType Leaf)) {
        throw "ctest.exe was not found next to cmake.exe: $CTestCommand"
    }
    # The test binaries link Qt and QuaZip as shared libraries.
    $env:PATH = "$QtBin;$MinGwBin;$(Split-Path -Parent $QuaZipDll);$(Split-Path -Parent $ZlibDll);$OriginalPath"
    # ctest reads each test's output through a pipe. Without this, Qt decides no
    # console is attached and sends the results to the debugger instead, so a
    # failing test reports nothing but its exit code.
    $previousConsoleAssumption = $env:QT_ASSUME_STDERR_HAS_CONSOLE
    $env:QT_ASSUME_STDERR_HAS_CONSOLE = '1'
    try {
        Invoke-LoggedCommand -FilePath $CTestCommand -Arguments @('--test-dir', (Convert-ToCMakePath $AppBuildDir), '--output-on-failure') -WorkingDirectory $BuildRoot
    }
    finally {
        $env:QT_ASSUME_STDERR_HAS_CONSOLE = $previousConsoleAssumption
        $env:PATH = "$QtBin;$MinGwBin;$OriginalPath"
    }
}

# --- Portable folder ------------------------------------------------------

if ($SkipPortable) {
    Write-Step 'Skipping portable packaging'
    Write-Host "Milah executable: $MilahExe"
    return
}

Write-Step 'Assembling portable Milah folder'

New-Item -ItemType Directory -Force -Path $PortableDir | Out-Null
$PortableExe = Join-Path $PortableDir 'Milah.exe'
Copy-Item -LiteralPath $MilahExe -Destination $PortableExe -Force
Copy-Item -LiteralPath $QuaZipDll -Destination (Join-Path $PortableDir (Split-Path -Leaf $QuaZipDll)) -Force
Copy-Item -LiteralPath $ZlibDll -Destination (Join-Path $PortableDir (Split-Path -Leaf $ZlibDll)) -Force

$windeployqtConfig = '--release'
if ($Configuration -eq 'Debug') { $windeployqtConfig = '--debug' }

Invoke-LoggedCommand `
    -FilePath $WindeployqtCommand `
    -Arguments @($windeployqtConfig, '--no-translations', '--compiler-runtime', $PortableExe) `
    -WorkingDirectory $PortableDir

Assert-PortableQtRuntime -PortableRoot $PortableDir -DllNames @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll')
Copy-ToolchainRuntimeDlls -ToolchainBin $MinGwBin -PortableRoot $PortableDir -Executables @($PortableExe)

if (-not $KeepBuildArtifacts) {
    Write-Step 'Cleaning transient build folder'
    if (Test-Path -LiteralPath $AppBuildDir -PathType Container) {
        Remove-Item -LiteralPath $AppBuildDir -Recurse -Force
    }
    Write-Host "Kept dependency cache: $DepsDir"
}

Write-Step 'Build complete'
Write-Host "Your portable Milah executable is: $PortableExe"
Write-Host 'Do not launch Milah.exe from the build folder unless the Qt runtime DLLs are on PATH.'
