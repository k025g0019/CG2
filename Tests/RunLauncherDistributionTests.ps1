param(
    [string]$LauncherPath = (Join-Path $PSScriptRoot "..\x64\Release\ManoLauncher.exe")
)

$ErrorActionPreference = "Stop"
$workspaceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$testRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "TempLauncherDistribution"))
if (-not $testRoot.StartsWith($workspaceRoot, [System.StringComparison]::OrdinalIgnoreCase)) { throw "Unsafe test path." }
if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
New-Item -ItemType Directory -Path $testRoot | Out-Null

function Invoke-Launcher {
    param([string[]]$Arguments, [int]$ExpectedExitCode = 0)
    & $LauncherPath @Arguments
    if ($LASTEXITCODE -ne $ExpectedExitCode) { throw "Launcher exit code $LASTEXITCODE, expected ${ExpectedExitCode}: $Arguments" }
}

function Write-Utf8Bom {
    param([string]$Path, [string]$Text)
    $encoding = New-Object System.Text.UTF8Encoding($true)
    [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($Path)) | Out-Null
    [System.IO.File]::WriteAllText($Path, $Text, $encoding)
}

$release1 = Join-Path $testRoot "Release1"
$release2 = Join-Path $testRoot "Release2"
$hub = Join-Path $testRoot "Hub"
$installed = Join-Path $testRoot "NewPc"
$project = Join-Path $testRoot "MyGame"
foreach ($folder in @($release1, $release2, (Join-Path $project "ProjectSettings"))) { New-Item -ItemType Directory -Path $folder -Force | Out-Null }

# Test専用の軽量ExecutableをEditor成果物として使い、起動経路もCreateProcessまで確認する。
# "open"はEngine起動直後の即終了を検知するため1.5秒待つ。where.exeは即終了してしまい
# この検知に引っかかるので、引数・標準入力に関係なく数秒待って終了する身代わりexeを使う。
$fakeEngineSource = @'
using System.Threading;
class FakeEngine {
    static int Main(string[] args) {
        Thread.Sleep(3000);
        return 0;
    }
}
'@
$fakeEngineExe = Join-Path $testRoot "FakeEngine.exe"
Add-Type -TypeDefinition $fakeEngineSource -OutputType ConsoleApplication -OutputAssembly $fakeEngineExe
Copy-Item -LiteralPath $fakeEngineExe -Destination (Join-Path $release1 "CG2.exe")
Copy-Item -LiteralPath $fakeEngineExe -Destination (Join-Path $release2 "CG2.exe")
$requiredDlls = @("dxcompiler.dll", "dxil.dll", "libfbxsdk.dll", "onnxruntime.dll", "PhysX_64.dll", "PhysXCommon_64.dll", "PhysXFoundation_64.dll", "PhysXCooking_64.dll", "NvBlast.dll")
foreach ($dll in $requiredDlls) {
    Copy-Item -LiteralPath "$env:SystemRoot\System32\where.exe" -Destination (Join-Path $release1 $dll)
    Copy-Item -LiteralPath "$env:SystemRoot\System32\where.exe" -Destination (Join-Path $release2 $dll)
}
Write-Utf8Bom (Join-Path $release1 "engine.dat") "release-one"
Write-Utf8Bom (Join-Path $release2 "engine.dat") "release-two"
Write-Utf8Bom (Join-Path $release1 "Assets\must-not-publish.asset") "project-owned"
Write-Utf8Bom (Join-Path $release1 "ProjectSettings\must-not-publish.txt") "project-owned"
# Assets/ShadersだけはRendererが実行時に読む必須Engine Shaderなので除外対象からは除く。
Write-Utf8Bom (Join-Path $release1 "Assets\Shaders\Object3d.VS.hlsl") "required-engine-shader"
Write-Utf8Bom (Join-Path $project "ProjectSettings\ProjectVersion.cg2") @"
ManoEngineProjectVersion|1
RequiredEngineVersion|0.9.4+152
EngineVersionPolicy|Minimum
UpdateChannel|Stable
ProjectFormatVersion|1
SceneFormatVersion|1
PrefabFormatVersion|1
"@

$port = 18084
$hubUrl = "http://127.0.0.1:$port"
Invoke-Launcher @("publish-engine", "--release", $release1, "--output", $hub, "--version", "0.9.4+152", "--channel", "Stable", "--hub", $hubUrl)
if (Test-Path -LiteralPath (Join-Path $hub "engines\0.9.4+152\Assets\must-not-publish.asset")) { throw "Project Asset leaked into Engine package." }
if (Test-Path -LiteralPath (Join-Path $hub "engines\0.9.4+152\ProjectSettings\must-not-publish.txt")) { throw "Project Settings leaked into Engine package." }
if (-not (Test-Path -LiteralPath (Join-Path $hub "engines\0.9.4+152\Assets\Shaders\Object3d.VS.hlsl"))) { throw "Required Engine shader was excluded from the package." }
$invite = Join-Path $testRoot "MyGame.mano-invite"
Invoke-Launcher @("create-invite", "--output", $invite, "--project-id", "my-game", "--project-name", "MyGame", "--hub", "127.0.0.1:$port", "--channel", "Stable", "--required-engine", "0.9.4+152")

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) { $python = Get-Command py -ErrorAction Stop }
$server = Start-Process -FilePath $python.Source -ArgumentList @("-m", "http.server", $port, "--bind", "127.0.0.1", "--directory", $hub) -WindowStyle Hidden -PassThru
try {
    $serverReady = $false
    for ($attempt = 0; $attempt -lt 30 -and -not $serverReady; ++$attempt) {
        try {
            $response = Invoke-WebRequest -UseBasicParsing "$hubUrl/mano-hub.json" -TimeoutSec 1
            $serverReady = $response.StatusCode -eq 200
        }
        catch { Start-Sleep -Milliseconds 200 }
    }
    if (-not $serverReady) { throw "Local test Hub did not start." }
    $manifestResponse = Invoke-WebRequest -UseBasicParsing "$hubUrl/update/stable/engine.manifest" -TimeoutSec 2
    if ($manifestResponse.StatusCode -ne 200) { throw "Local test Hub manifest is not reachable." }
    # GUIの[参加 / セットアップ]が呼ぶ同じApplication ServiceをCLIで自動検証する。
    Invoke-Launcher @("setup-invite", "--invite", $invite, "--root", $installed, "--project", $project)
    if (-not (Test-Path -LiteralPath (Join-Path $installed "Engines\0.9.4+152\CG2.exe"))) { throw "Invite install failed." }
    if (-not (Test-Path -LiteralPath (Join-Path $installed "LauncherState\projects.registry"))) { throw "Project registration failed." }
    Invoke-Launcher @("verify", "--manifest", (Join-Path $installed "LauncherState\Manifests\0.9.4+152.manifest"), "--root", $installed)
    Invoke-Launcher @("open", "--root", $installed, "--project", $project)
    # 身代わりEngineは数秒後に自分で終了するが、念のため片付けておく。
    Get-Process -Name "ManoEngine" -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq (Join-Path $installed "Engines\0.9.4+152\CG2.exe") } |
        Stop-Process -Force -ErrorAction SilentlyContinue

    Invoke-Launcher @("publish-engine", "--release", $release2, "--output", $hub, "--version", "0.9.5+161", "--channel", "Stable", "--hub", $hubUrl)
    $manifest2 = Join-Path $hub "update\stable\engine.manifest"
    Invoke-Launcher @("update", "--manifest", $manifest2, "--root", $installed, "--project", $project)
    Invoke-Launcher @("rollback", "--root", $installed)

    $broken = Join-Path $installed "Engines\0.9.4+152\engine.dat"
    Write-Utf8Bom $broken "broken"
    Invoke-Launcher @("verify", "--manifest", (Join-Path $installed "LauncherState\Manifests\0.9.4+152.manifest"), "--root", $installed) -ExpectedExitCode 1
    Invoke-Launcher @("repair", "--manifest", (Join-Path $installed "LauncherState\Manifests\0.9.4+152.manifest"), "--root", $installed, "--project", $project)
    Invoke-Launcher @("verify", "--manifest", (Join-Path $installed "LauncherState\Manifests\0.9.4+152.manifest"), "--root", $installed)
    Write-Host "Launcher distribution representative tests: PASS"
}
finally {
    if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}
