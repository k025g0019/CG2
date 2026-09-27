[CmdletBinding()]
param(
    [string]$PhysXSource = "$env:USERPROFILE\Downloads\PhysX-main\PhysX-main",
    [string]$BlastSource = "$env:USERPROFILE\Downloads\Blast-1.1.5_release\Blast-1.1.5_release",
    [ValidateSet('Debug', 'Release')][string[]]$Configuration = @('Debug', 'Release'),
    [switch]$SkipCopy,
    [switch]$SkipGpuTest
)
$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$sdkRoot = Join-Path $projectRoot 'ThirdParty\PhysicsSdk'
$blastPhysX34Root = Join-Path $projectRoot 'ThirdParty\PhysX-3.4'

# Blast 1.1.5 Authoringだけはリリース時と同じPhysX 3.4型定義を必要とする。
# ManoEngine Runtime用の現行PhysXと混ぜるとPxVec3の型衝突が起きるため、配置を先に検証する。
$blastPhysX34Include = Join-Path $blastPhysX34Root 'PhysX_3.4\Include'
$blastPxSharedInclude = Join-Path $blastPhysX34Root 'PxShared\include'
if (!(Test-Path -LiteralPath $blastPhysX34Root -PathType Container)) {
    $gitCommand = Get-Command git.exe -ErrorAction SilentlyContinue
    if (!$gitCommand) {
        throw 'Git is required to acquire NVIDIA PhysX 3.4 for Blast Authoring.'
    }

    Write-Host 'Downloading NVIDIA PhysX 3.4 for Blast 1.1.5 Authoring...'
    & $gitCommand.Source clone --depth 1 `
        'https://github.com/NVIDIAGameWorks/PhysX-3.4.git' `
        $blastPhysX34Root
    if ($LASTEXITCODE -ne 0) {
        throw 'PhysX 3.4 download failed. Remove an incomplete ThirdParty\PhysX-3.4 folder before retrying.'
    }
}

if (!(Test-Path -LiteralPath $blastPhysX34Include -PathType Container) -or
    !(Test-Path -LiteralPath $blastPxSharedInclude -PathType Container) -or
    !(Test-Path -LiteralPath (Join-Path $blastPxSharedInclude 'foundation\PxVec3.h') -PathType Leaf)) {
    throw 'Blast Authoring requires ThirdParty\PhysX-3.4 (PhysX_3.4\Include and PxShared\include).'
}

# SDK の原本を保持し、CG2 内のコピーだけをビルドする。
function Copy-SdkTree([string]$sourcePath, [string]$destinationPath) {

    if (!(Test-Path -LiteralPath $sourcePath -PathType Container)) {
        throw "SDK source not found: $sourcePath"
    }

    & robocopy $sourcePath $destinationPath /E /NFL /NDL /NJH /NJS /NP

    if ($LASTEXITCODE -ge 8) {
        throw "SDK copy failed: $sourcePath (robocopy $LASTEXITCODE)"
    }

}

if (!$SkipCopy) {
    Copy-SdkTree (Join-Path $PhysXSource 'physx') (Join-Path $projectRoot 'ThirdParty\PhysX\physx')
    Copy-Item -LiteralPath (Join-Path $PhysXSource 'LICENSE.md') -Destination (Join-Path $projectRoot 'ThirdParty\PhysX\LICENSE.md')
    Copy-SdkTree (Join-Path $BlastSource 'sdk') (Join-Path $projectRoot 'ThirdParty\Blast-1.1.5\sdk')
    Copy-Item -LiteralPath (Join-Path $BlastSource 'license.txt') -Destination (Join-Path $projectRoot 'ThirdParty\Blast-1.1.5\license.txt')
    Copy-Item -LiteralPath (Join-Path $BlastSource 'README.md') -Destination (Join-Path $projectRoot 'ThirdParty\Blast-1.1.5\README.md')
}

& (Join-Path $PSScriptRoot 'ApplyCompatibility.ps1') -PhysXRoot (Join-Path $projectRoot 'ThirdParty\PhysX\physx')

# CG2 と同じ VS2022 / v143 / x64 を使用する。
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudioPath = & $vswherePath -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath

if (!$visualStudioPath) {
    throw 'Visual Studio 2022 C++ tools are required.'
}

$cmakePath = Join-Path $visualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctestPath = Join-Path (Split-Path $cmakePath) 'ctest.exe'
$buildDirectory = Join-Path $sdkRoot 'build'
& $cmakePath -S $PSScriptRoot -B $buildDirectory -G 'Visual Studio 17 2022' -A x64

if ($LASTEXITCODE -ne 0) {
    throw 'Physics SDK CMake configure failed.'
}

foreach ($buildConfiguration in $Configuration) {
    $sdkConfiguration = $buildConfiguration.ToLowerInvariant()
    & $cmakePath --build $buildDirectory --config $sdkConfiguration --parallel 3

    if ($LASTEXITCODE -ne 0) {
        throw "Physics SDK build failed: $buildConfiguration"
    }

    $testArguments = @('--test-dir', $buildDirectory, '-C', $sdkConfiguration, '--output-on-failure')

    if ($SkipGpuTest) {
        $testArguments += @('-E', '^PhysicsSdkGpuSmoke$')
        Write-Host 'GPU runtime test explicitly skipped; GPU libraries are still built.'
    }

    & $ctestPath @testArguments

    if ($LASTEXITCODE -ne 0) {
        throw "Physics SDK smoke test failed: $buildConfiguration"
    }

}
Write-Host 'PhysX CPU/GPU SDK and Blast runtime/authoring SDK are ready for ManoEngine.'
