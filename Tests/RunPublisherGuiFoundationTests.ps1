param(
    [string]$LauncherPath = (Join-Path $PSScriptRoot "..\x64\Release\ManoLauncher.exe"),
    [string]$TeamServerPath = (Join-Path $PSScriptRoot "..\x64\Release\ManoTeamServer.exe")
)

$ErrorActionPreference = "Stop"
$workspaceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$testRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "TempPublisherGui"))
if (-not $testRoot.StartsWith($workspaceRoot, [System.StringComparison]::OrdinalIgnoreCase)) { throw "Unsafe test path." }
if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }

function Invoke-Launcher {
    param([string[]]$Arguments, [int]$ExpectedExitCode = 0)
    $output = & $LauncherPath @Arguments 2>&1 | Out-String
    if ($LASTEXITCODE -ne $ExpectedExitCode) { throw "Launcher exit code $LASTEXITCODE, expected ${ExpectedExitCode}: $Arguments`n$output" }
    return $output
}

try {
    # publisher-preview/publishはGetManoEngineVersion()、つまりビルドのたびに自動採番される
    # 実際のEngine Versionを使う。固定文字列にすると番号が進むたびにテストが壊れる。
    $engineVersionJson = Get-Content -LiteralPath (Join-Path $workspaceRoot "Engine\Version\engine-version.json") -Raw | ConvertFrom-Json
    $currentEngineVersion = "$($engineVersionJson.major).$($engineVersionJson.minor).$($engineVersionJson.patch)+$($engineVersionJson.build)"

    $release = Join-Path $testRoot "Release"
    $hub = Join-Path $testRoot "Hub"
    $state = Join-Path $testRoot "Installed"
    New-Item -ItemType Directory -Path $release, $hub, $state -Force | Out-Null

    Copy-Item -LiteralPath "$env:SystemRoot\System32\where.exe" -Destination (Join-Path $release "CG2.exe")
    foreach ($dll in @("dxcompiler.dll", "dxil.dll", "libfbxsdk.dll", "onnxruntime.dll", "PhysX_64.dll", "PhysXCommon_64.dll", "PhysXFoundation_64.dll", "PhysXCooking_64.dll", "NvBlast.dll")) {
        Copy-Item -LiteralPath "$env:SystemRoot\System32\where.exe" -Destination (Join-Path $release $dll)
    }
    [System.IO.File]::WriteAllText((Join-Path $release "engine.dat"), "publisher-test", (New-Object System.Text.UTF8Encoding($true)))
    [System.IO.Directory]::CreateDirectory((Join-Path $release "Assets")) | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $release "Assets\must-not-publish.asset"), "project", (New-Object System.Text.UTF8Encoding($true)))
    # Assets/ShadersだけはRendererが実行時に読む必須Engine Shaderなので除外対象からは除く。
    [System.IO.Directory]::CreateDirectory((Join-Path $release "Assets\Shaders")) | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $release "Assets\Shaders\Object3d.VS.hlsl"), "required-engine-shader", (New-Object System.Text.UTF8Encoding($true)))

    $distributionPort = 18095
    $collaborationPort = 18096
    $hubUrl = "http://127.0.0.1:$distributionPort"
    Invoke-Launcher @("publisher-config", "--root", $state, "--release", $release, "--output", $hub, "--hub", $hubUrl,
        "--channel", "Stable", "--mode", "Local", "--collaboration-server", $TeamServerPath, "--project-id", "publisher-test",
        "--distribution-port", $distributionPort, "--collaboration-port", $collaborationPort) | Out-Null

    # 別Processで設定を再読込することで、Launcher再起動後の設定保持も同時に確認する。
    $preview = Invoke-Launcher @("publisher-preview", "--root", $state)
    if ($preview -notmatch [regex]::Escape("バージョン: $currentEngineVersion") -or $preview -notmatch "追加 / 変更 / 削除") { throw "公開内容の確認結果が不足しています。`n$preview" }
    Invoke-Launcher @("publisher-publish", "--root", $state) | Out-Null
    if (-not (Test-Path -LiteralPath (Join-Path $hub "update\stable\engine.manifest"))) { throw "Stable publish failed." }
    if (Test-Path -LiteralPath (Join-Path $hub "engines\$currentEngineVersion\Assets\must-not-publish.asset")) { throw "Project Assets leaked." }
    if (-not (Test-Path -LiteralPath (Join-Path $hub "engines\$currentEngineVersion\Assets\Shaders\Object3d.VS.hlsl"))) { throw "Required Engine shader was excluded from the package." }
    if (-not (Test-Path -LiteralPath (Join-Path $state "LauncherState\publish-history.log"))) { throw "Publish history was not saved." }

    Invoke-Launcher @("publisher-server", "--root", $state, "--kind", "distribution", "--action", "start") | Out-Null
    $distributionReady = $false
    for ($i = 0; $i -lt 30 -and -not $distributionReady; ++$i) {
        try { $distributionReady = (Invoke-WebRequest -UseBasicParsing "$hubUrl/mano-hub.json" -TimeoutSec 1).StatusCode -eq 200 } catch { Start-Sleep -Milliseconds 100 }
    }
    if (-not $distributionReady) { throw "Distribution Server did not become reachable." }
    if ((Invoke-Launcher @("publisher-server", "--root", $state, "--kind", "distribution", "--action", "status")) -notmatch "稼働中") { throw "配布サーバーが稼働中になっていません。" }
    Invoke-Launcher @("publisher-server", "--root", $state, "--kind", "distribution", "--action", "stop") | Out-Null

    Invoke-Launcher @("publisher-server", "--root", $state, "--kind", "collaboration", "--action", "start") | Out-Null
    $teamReady = $false
    for ($i = 0; $i -lt 30 -and -not $teamReady; ++$i) {
        $status = Invoke-Launcher @("publisher-server", "--root", $state, "--kind", "collaboration", "--action", "status")
        $teamReady = $status -match "稼働中"
        if (-not $teamReady) { Start-Sleep -Milliseconds 100 }
    }
    if (-not $teamReady) { throw "ManoTeamServer did not become Running." }
    Invoke-Launcher @("publisher-server", "--root", $state, "--kind", "collaboration", "--action", "stop") | Out-Null

    # Hub metadataをLockしてCommit最終段を失敗させ、Stable Manifestが旧Versionへ戻ることを確認する。
    $stablePath = Join-Path $hub "update\stable\engine.manifest"
    $stableBefore = [System.IO.File]::ReadAllBytes($stablePath)
    $hubInfoPath = Join-Path $hub "mano-hub.json"
    $lock = [System.IO.File]::Open($hubInfoPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::None)
    try {
        Invoke-Launcher @("publish-engine", "--release", $release, "--output", $hub, "--version", "0.9.5+161", "--channel", "Stable", "--hub", $hubUrl) -ExpectedExitCode 1 | Out-Null
    }
    finally { $lock.Dispose() }
    $stableAfter = [System.IO.File]::ReadAllBytes($stablePath)
    if ([Convert]::ToBase64String($stableBefore) -ne [Convert]::ToBase64String($stableAfter)) { throw "Broken publish changed Stable manifest." }

    Write-Host "Publisher GUI foundation representative tests: PASS"
}
finally {
    try { Invoke-Launcher @("publisher-server", "--root", $state, "--kind", "distribution", "--action", "stop") | Out-Null } catch {}
    try { Invoke-Launcher @("publisher-server", "--root", $state, "--kind", "collaboration", "--action", "stop") | Out-Null } catch {}
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}
