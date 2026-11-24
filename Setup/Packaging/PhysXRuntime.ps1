# Shared by source-checkout and portable-editor packaging.
function Copy-PhysXRuntime {
    param(
        [Parameter(Mandatory = $true)][string]$EngineRoot,
        [Parameter(Mandatory = $true)][string]$Destination
    )
    $sdk = Join-Path $EngineRoot 'Source/ThirdParty/PhysX'
    if (Test-Path -LiteralPath (Join-Path $sdk 'include/PxPhysicsAPI.h')) {
        $dllDirectory = Join-Path $sdk 'lib/x64/release'
        $license = Join-Path $sdk 'LICENSE.md'
    } else {
        $dllDirectory = $EngineRoot
        $license = Join-Path $EngineRoot 'ThirdPartyLicenses/PhysX/LICENSE.md'
    }
    $dlls = @('PhysX_64.dll', 'PhysXCommon_64.dll', 'PhysXFoundation_64.dll', 'PhysXCooking_64.dll')
    foreach ($name in $dlls) {
        $file = Join-Path $dllDirectory $name
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing bundled PhysX runtime: $file" }
        Copy-Item -LiteralPath $file -Destination $Destination -Force
    }
    if (-not (Test-Path -LiteralPath $license -PathType Leaf)) { throw "Missing PhysX license: $license" }
    $licenseDirectory = Join-Path $Destination 'ThirdPartyLicenses/PhysX'
    New-Item -ItemType Directory -Path $licenseDirectory -Force | Out-Null
    Copy-Item -LiteralPath $license -Destination $licenseDirectory -Force
}
