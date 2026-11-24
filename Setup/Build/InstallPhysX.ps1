#requires -Version 5.1
# Build the study guide's PhysX 4.1 branch and replace the vendored CPU SDK.
[CmdletBinding()]
param(
    [string]$CMake,
    [string]$Generator = 'Visual Studio 18 2026',
    [ValidateRange(1, 32)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$engineRoot = [IO.Path]::GetFullPath((Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$revision = 'a2c0428acab643e60618c681b501e86f7fd558cc'
$repository = 'https://github.com/NVIDIAGameWorks/PhysX.git'
$checkout = Join-Path $engineRoot 'Build/Dependencies/PhysX-4.1'
$build = Join-Path $engineRoot 'Build/Dependencies/PhysX-4.1-build'
$staging = Join-Path $engineRoot 'Build/Dependencies/PhysX-4.1-sdk'
$backup = Join-Path $engineRoot 'Build/Dependencies/PhysX-sdk-backup'
$sdk = Join-Path $engineRoot 'Source/ThirdParty/PhysX'
$previousCompilerOptions = $env:_CL_
try {
    if (-not $CMake) {
        $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
        if ($command) { $CMake = $command.Source }
        else {
            $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
            $CMake = & $vswhere -latest -prerelease -products '*' -find 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' | Select-Object -First 1
        }
    }
    if (-not $CMake -or -not (Test-Path -LiteralPath $CMake)) { throw 'CMake not found. Install the Visual Studio C++ CMake tools or pass -CMake.' }
    if (-not (Test-Path -LiteralPath $checkout)) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $checkout) -Force | Out-Null
        $previousLfsSetting = $env:GIT_LFS_SKIP_SMUDGE
        try {
            $env:GIT_LFS_SKIP_SMUDGE = '1'
            & git clone --depth 1 --filter=blob:none --sparse --branch 4.1 $repository $checkout
            if ($LASTEXITCODE -ne 0) { throw 'PhysX source download failed.' }
            & git -C $checkout sparse-checkout set physx/include physx/source physx/compiler/public physx/buildtools pxshared/include externals/cmakemodules
            if ($LASTEXITCODE -ne 0) { throw 'PhysX source checkout failed.' }
        } finally { $env:GIT_LFS_SKIP_SMUDGE = $previousLfsSetting }
    }
    $actualRevision = & git -C $checkout rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $actualRevision -ne $revision) { throw 'Unexpected PhysX source revision. Use the pinned official 4.1 revision.' }
    $source = (Join-Path $checkout 'physx').Replace('\', '/')
    $pxshared = (Join-Path $checkout 'pxshared').Replace('\', '/')
    $modules = (Join-Path $checkout 'externals/cmakemodules').Replace('\', '/')
    $configure = @(
        '-S', "$source/compiler/public", '-B', $build, '-G', $Generator, '-A', 'x64',
        "-DPHYSX_ROOT_DIR=$source", "-DPXSHARED_PATH=$pxshared",
        "-DCMAKEMODULES_PATH=$modules", '-DCMAKEMODULES_VERSION=1.27',
        '-DTARGET_BUILD_PLATFORM=windows', '-DPX_OUTPUT_ARCH=x86', "-DPX_OUTPUT_LIB_DIR=$source", "-DPX_OUTPUT_BIN_DIR=$source",
        '-DPX_GENERATE_STATIC_LIBRARIES=OFF', '-DNV_USE_STATIC_WINCRT=OFF', '-DNV_USE_DEBUG_WINCRT=ON',
        '-DPX_BUILDSNIPPETS=OFF', '-DPX_BUILDPUBLICSAMPLES=OFF', '-DPX_CMAKE_SUPPRESS_REGENERATION=ON',
        # This CPU build does not need upstream's prebuilt GPU DLLs or their LFS payloads.
        '-DGPU_DLL_COPIED=ON', '-DCMAKE_POLICY_VERSION_MINIMUM=3.5'
    )
    & $CMake @configure
    if ($LASTEXITCODE -ne 0) { throw 'PhysX CMake configuration failed.' }

    # Old upstream /WX rejects newer MSVC diagnostics. /Z7 also avoids mixed PDB tool versions.
    $env:_CL_ = (($previousCompilerOptions + ' /Z7 /WX-').Trim())
    foreach ($configuration in @('debug', 'release')) {
        & $CMake --build $build --config $configuration --parallel $Jobs -- /nologo /verbosity:minimal /nodeReuse:false
        if ($LASTEXITCODE -ne 0) { throw "PhysX $configuration build failed." }
    }

    # Validate every destructive/move target before replacing the SDK. Staging and backup stay in Build.
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $engineRoot 'Build')) + [IO.Path]::DirectorySeparatorChar
    foreach ($target in @($staging, $backup)) {
        if (-not [IO.Path]::GetFullPath($target).StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "SDK staging path is outside Build: $target"
        }
    }
    if ([IO.Path]::GetFullPath($sdk) -ne (Join-Path $engineRoot 'Source/ThirdParty/PhysX')) { throw 'Unexpected SDK destination.' }
    if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
    New-Item -ItemType Directory -Path $staging -Force | Out-Null
    Copy-Item -LiteralPath "$source/include" -Destination $staging -Recurse -Force
    # PhysX 4.1 separates Foundation headers into PxShared; ship a single consumer include root.
    Copy-Item -LiteralPath "$pxshared/include/foundation" -Destination (Join-Path $staging 'include') -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $checkout 'LICENSE.md') -Destination $staging -Force

    $libraries = @('PhysX_64', 'PhysXCommon_64', 'PhysXFoundation_64', 'PhysXCooking_64',
        'PhysXExtensions_static_64', 'PhysXPvdSDK_static_64', 'PhysXCharacterKinematic_static_64', 'PhysXVehicle_static_64')
    $dlls = @('PhysX_64', 'PhysXCommon_64', 'PhysXFoundation_64', 'PhysXCooking_64')
    $hashes = @{}
    foreach ($configuration in @('debug', 'release')) {
        $artifacts = @(Get-ChildItem -LiteralPath "$source/bin" -Recurse -Filter PhysX_64.dll -File |
            Where-Object { $_.Directory.Name -eq $configuration })
        if ($artifacts.Count -ne 1) { throw "Expected one PhysX $configuration output directory, found $($artifacts.Count)." }
        $destination = Join-Path $staging "lib/x64/$configuration"
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        foreach ($name in $libraries) { Copy-Item -LiteralPath (Join-Path $artifacts[0].DirectoryName "$name.lib") -Destination $destination -Force }
        foreach ($name in $dlls) { Copy-Item -LiteralPath (Join-Path $artifacts[0].DirectoryName "$name.dll") -Destination $destination -Force }
        $hashes[$configuration] = @{}
        foreach ($file in Get-ChildItem -LiteralPath $destination -File) {
            $hashes[$configuration][$file.Name] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        }
    }
    $hashes | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $staging 'SHA256SUMS.json') -Encoding UTF8
    [ordered]@{
        version = '4.1.2'; branch = '4.1'; revision = $revision; repository = $repository
        platform = 'windows-x64'; generator = $Generator; cpuOnly = $true
        debugRuntime = 'MDd'; releaseRuntime = 'MD'
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $staging 'SDK.json') -Encoding UTF8

    if (Test-Path -LiteralPath $backup) { Remove-Item -LiteralPath $backup -Recurse -Force }
    if (Test-Path -LiteralPath $sdk) { Move-Item -LiteralPath $sdk -Destination $backup }
    try { Move-Item -LiteralPath $staging -Destination $sdk }
    catch {
        if ((Test-Path -LiteralPath $backup) -and -not (Test-Path -LiteralPath $sdk)) {
            Move-Item -LiteralPath $backup -Destination $sdk
        }
        throw
    }
    Write-Host "Bundled PhysX 4.1.2: $sdk"
} catch {
    [Console]::Error.WriteLine("PhysX installation failed: $($_.Exception.Message)")
    exit 1
} finally {
    $env:_CL_ = $previousCompilerOptions
}
