param(
    [string]$LauncherPath = (Join-Path $PSScriptRoot "..\x64\Release\CG2Launcher.exe")
)

$ErrorActionPreference = "Stop"
$workspaceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$testRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "TempVersionFoundation"))
if (-not $testRoot.StartsWith($workspaceRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe test path."
}
if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
New-Item -ItemType Directory -Path $testRoot | Out-Null

function Invoke-Launcher {
    param([string[]]$Arguments, [int]$ExpectedExitCode = 0)
    & $LauncherPath @Arguments
    if ($LASTEXITCODE -ne $ExpectedExitCode) {
        throw "Launcher exit code $LASTEXITCODE, expected ${ExpectedExitCode}: $Arguments"
    }
}
function Write-Utf8Bom {
    param([string]$Path, [string]$Text)
    $encoding = New-Object System.Text.UTF8Encoding($true)
    [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($Path)) | Out-Null
    [System.IO.File]::WriteAllText($Path, $Text, $encoding)
}

# check-peerの「Local」側はビルドのたびに自動採番されるEngine本体の実バージョンを見るため、
# ここで固定文字列にすると番号が進むたびにテストが壊れる。実際の値を読んで使う。
$engineVersionJson = Get-Content -LiteralPath (Join-Path $workspaceRoot "Engine\Version\engine-version.json") -Raw | ConvertFrom-Json
$currentEngineVersion = "$($engineVersionJson.major).$($engineVersionJson.minor).$($engineVersionJson.patch)+$($engineVersionJson.build)"

$engineRoot = Join-Path $testRoot "Installed"
$project = Join-Path $testRoot "CompatibleProject"
New-Item -ItemType Directory -Path (Join-Path $project "ProjectSettings") -Force | Out-Null
Write-Utf8Bom (Join-Path $project "ProjectSettings\ProjectVersion.cg2") @"
CG2EngineProjectVersion|1
RequiredEngineVersion|0.9.4+152
EngineVersionPolicy|Minimum
UpdateChannel|Stable
ProjectFormatVersion|1
SceneFormatVersion|1
PrefabFormatVersion|1
"@

$package1 = Join-Path $testRoot "Package1"
$package2 = Join-Path $testRoot "Package2"
$package3 = Join-Path $testRoot "Package3"
foreach ($package in @($package1, $package2, $package3)) { New-Item -ItemType Directory -Path $package | Out-Null }
# "open"はEngine起動直後の即終了を検知するため1.5秒待つ。System32の既存exeは
# (where.exeのように)即終了するか、(findstr.exeのように)標準入力待ちで居座る。
# このテスト環境は標準入力がnullデバイスに固定されているため後者も即終了してしまうので、
# 引数・標準入力に関係なく必ず数秒待ってから終了する身代わりexeを自前でコンパイルする。
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
Copy-Item -LiteralPath $fakeEngineExe -Destination (Join-Path $package1 "CG2.exe")
Copy-Item -LiteralPath $fakeEngineExe -Destination (Join-Path $package2 "CG2.exe")
Copy-Item -LiteralPath $fakeEngineExe -Destination (Join-Path $package3 "CG2.exe")
Write-Utf8Bom (Join-Path $package1 "engine.dat") "version-one"
Write-Utf8Bom (Join-Path $package1 "obsolete.dll") "obsolete"
Write-Utf8Bom (Join-Path $package2 "engine.dat") "version-two"
Write-Utf8Bom (Join-Path $package3 "engine.dat") "version-three"

$manifest1 = Join-Path $testRoot "v1.manifest"
$manifest2 = Join-Path $testRoot "v2.manifest"
$manifest3 = Join-Path $testRoot "v3.manifest"
Invoke-Launcher -Arguments @("create-manifest", "--package", $package1, "--output", $manifest1, "--version", "0.9.4+152", "--channel", "Stable", "--base", $package1)
Invoke-Launcher -Arguments @("create-manifest", "--package", $package2, "--output", $manifest2, "--version", "0.9.5+153", "--channel", "Stable", "--base", $package2)
Add-Content -LiteralPath $manifest2 -Value "RemovedFile|obsolete.dll" -Encoding UTF8
Invoke-Launcher -Arguments @("create-manifest", "--package", $package3, "--output", $manifest3, "--version", "0.9.6+154", "--channel", "Stable", "--base", $package3)

Invoke-Launcher -Arguments @("install", "--manifest", $manifest1, "--root", $engineRoot, "--project", $project)
Invoke-Launcher -Arguments @("verify", "--manifest", $manifest1, "--root", $engineRoot)
Invoke-Launcher -Arguments @("check-update", "--manifest", $manifest2, "--root", $engineRoot, "--project", $project)
Invoke-Launcher -Arguments @("update", "--manifest", $manifest2, "--root", $engineRoot, "--project", $project)
Invoke-Launcher -Arguments @("verify", "--manifest", $manifest2, "--root", $engineRoot)
if (Test-Path -LiteralPath (Join-Path $engineRoot "Engines\0.9.5+153\obsolete.dll")) { throw "RemovedFile was not removed." }

# Manifest作成後に配布Fileを欠落させ、取得失敗時もCurrent Versionが変わらないことを確認する。
Start-Sleep -Milliseconds 200
$taskBrokenPackageFile123 = "$testRoot\Package3\engine.dat"
for ($attempt = 0; $attempt -lt 20 -and [System.IO.File]::Exists($taskBrokenPackageFile123); ++$attempt) {
    [System.IO.File]::Delete($taskBrokenPackageFile123)
    if ([System.IO.File]::Exists($taskBrokenPackageFile123)) { Start-Sleep -Milliseconds 50 }
}
if ([System.IO.File]::Exists($taskBrokenPackageFile123)) { throw "Test package removal failed." }
Invoke-Launcher -Arguments @("update", "--manifest", $manifest3, "--root", $engineRoot, "--project", $project) -ExpectedExitCode 1
$stateAfterFailure = Get-Content -LiteralPath (Join-Path $engineRoot "LauncherState\launcher.state") -Raw
if ($stateAfterFailure -notmatch "CurrentVersion\|0\.9\.5\+153") { throw "Broken update changed current version." }

Invoke-Launcher -Arguments @("rollback", "--root", $engineRoot)
$installedV1Data = Join-Path $engineRoot "Engines\0.9.4+152\engine.dat"
Write-Utf8Bom $installedV1Data "broken-installed-file"
Invoke-Launcher -Arguments @("repair", "--manifest", $manifest1, "--root", $engineRoot, "--project", $project)
Invoke-Launcher -Arguments @("verify", "--manifest", $manifest1, "--root", $engineRoot)
Invoke-Launcher -Arguments @("open", "--root", $engineRoot, "--project", $project)
# 身代わりEngine(findstr.exe)は標準入力待ちで残り続けるので、テスト後に片付ける。
Get-Process -Name "CG2Engine" -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -eq (Join-Path $engineRoot "Engines\0.9.4+152\CG2.exe") } |
    Stop-Process -Force -ErrorAction SilentlyContinue

Invoke-Launcher -Arguments @("check-project", "--project", $project)
Invoke-Launcher -Arguments @("check-peer", "--project", $project, "--peer-engine", $currentEngineVersion, "--peer-format", "1", "--peer-api", "13", "--peer-channel", "Stable")
Invoke-Launcher -Arguments @("check-peer", "--project", $project, "--peer-engine", "0.9.3+100", "--peer-format", "1", "--peer-api", "13", "--peer-channel", "Stable") -ExpectedExitCode 1

$legacyProject = Join-Path $testRoot "LegacyProject"
New-Item -ItemType Directory -Path (Join-Path $legacyProject "Assets\Scenes") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $legacyProject "ProjectSettings") -Force | Out-Null
Write-Utf8Bom (Join-Path $legacyProject "Assets\Scenes\Legacy.scene") "SceneUuid|00000000-0000-0000-0000-000000000001`nPhysicsSettings|0|-9.8|0|0.016|1|0|0|0`n"
Write-Utf8Bom (Join-Path $legacyProject "ProjectSettings\ProjectVersion.cg2") @"
CG2EngineProjectVersion|1
RequiredEngineVersion|0.9.4+152
EngineVersionPolicy|Minimum
UpdateChannel|Stable
ProjectFormatVersion|0
SceneFormatVersion|0
PrefabFormatVersion|0
"@
Invoke-Launcher -Arguments @("migrate", "--project", $legacyProject)
if ((Get-Content -LiteralPath (Join-Path $legacyProject "Assets\Scenes\Legacy.scene") -Raw) -notmatch "FormatVersion\|Scene\|1") { throw "Scene was not migrated." }
if (-not (Test-Path -LiteralPath (Join-Path $legacyProject "Library\MigrationBackups"))) { throw "Migration backup was not created." }

$futureProject = Join-Path $testRoot "FutureProject"
New-Item -ItemType Directory -Path (Join-Path $futureProject "ProjectSettings") -Force | Out-Null
Write-Utf8Bom (Join-Path $futureProject "ProjectSettings\ProjectVersion.cg2") @"
CG2EngineProjectVersion|1
RequiredEngineVersion|0.9.4+152
EngineVersionPolicy|Minimum
UpdateChannel|Stable
ProjectFormatVersion|999
SceneFormatVersion|999
PrefabFormatVersion|999
"@
Invoke-Launcher -Arguments @("check-project", "--project", $futureProject) -ExpectedExitCode 1

Write-Host "Version foundation representative tests: PASS"
Remove-Item -LiteralPath $testRoot -Recurse -Force
