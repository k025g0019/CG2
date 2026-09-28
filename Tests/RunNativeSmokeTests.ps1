#requires -Version 5.1
<#
    Tests/*.cpp の C++ Smoke Test をコンパイルして実行する。

    これらのテストは以前 CG2.vcxproj に登録されておらず、どの構成でもビルドされて
    いなかった。それぞれ独立した main() を持つため CG2 本体(WinMain)へは同居できない。
    そこで既存の Tests/Run*.ps1 と同じ形で、個別 exe としてビルド・実行する。

    終了コード 0 = 全件成功。1 以上 = 失敗した本数。
#>
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [string]$OutputDirectory = (Join-Path $PSScriptRoot "..\x64\SmokeTests")
)

$ErrorActionPreference = "Stop"
$workspaceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))

# ---------------------------------------------------------------------------
# cl.exe を使える状態にする。通常の PowerShell から実行できるよう、必要なら
# VsDevCmd.bat を読み込んで INCLUDE / LIB / PATH をこのセッションへ取り込む。
# 開発者コマンドプロンプトから実行した場合は何もしない。
# ---------------------------------------------------------------------------
function Import-VisualStudioEnvironment {
    if ($null -ne (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        return
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw "vswhere.exe が見つかりません。Visual Studio のC++ツールセットを入れてください。"
    }

    $installPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if ([string]::IsNullOrWhiteSpace($installPath)) {
        throw "Visual Studio の C++ ツールセットが見つかりません。"
    }

    $vsDevCmd = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path -LiteralPath $vsDevCmd)) {
        throw "VsDevCmd.bat が見つかりません: $vsDevCmd"
    }

    # VsDevCmd を実行した後の環境変数を取り出して、このセッションへ反映する。
    $environmentLines = & cmd.exe /c "call `"$vsDevCmd`" -arch=amd64 -host_arch=amd64 >nul 2>&1 && set"
    foreach ($environmentLine in $environmentLines) {
        $separatorIndex = $environmentLine.IndexOf('=')
        if ($separatorIndex -lt 1) { continue }
        $name = $environmentLine.Substring(0, $separatorIndex)
        $value = $environmentLine.Substring($separatorIndex + 1)
        Set-Item -LiteralPath "Env:$name" -Value $value
    }

    if ($null -eq (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw "VsDevCmd を読み込んでも cl.exe が使えません。"
    }
}

Import-VisualStudioEnvironment
$clExe = (Get-Command cl.exe).Source

# ---------------------------------------------------------------------------
# テストごとの依存関係。追加の include / lib が要るものだけ書く。
# ---------------------------------------------------------------------------
$blastInclude = Join-Path $workspaceRoot "ThirdParty\Blast-1.1.5\sdk\lowlevel\include"
$blastLibDirectory = if ($Configuration -eq "Debug") {
    Join-Path $workspaceRoot "ThirdParty\PhysicsSdk\lib\debug"
} else {
    Join-Path $workspaceRoot "ThirdParty\PhysicsSdk\lib\release"
}
$blastBinDirectory = if ($Configuration -eq "Debug") {
    Join-Path $workspaceRoot "ThirdParty\PhysicsSdk\bin\debug"
} else {
    Join-Path $workspaceRoot "ThirdParty\PhysicsSdk\bin\release"
}

$testCases = @(
    [pscustomobject]@{
        Name        = "TerrainHeightFieldSmoke"
        Source      = Join-Path $PSScriptRoot "TerrainHeightFieldSmoke.cpp"
        Includes    = @()
        Libraries   = @()
        RuntimeDlls = @()
        Description = "TerrainColliderの高さが頂点シェーダと同じ式になっているか"
    },
    [pscustomobject]@{
        Name        = "CameraAudioRendererVfxApiSmoke"
        Source      = Join-Path $PSScriptRoot "CameraAudioRendererVfxApiSmoke.cpp"
        # Source/Engine/Core/EditorNativeScript.h をリポジトリ相対で include するため、
        # ワークスペース直下を include パスへ入れる。
        Includes    = @($workspaceRoot)
        Libraries   = @()
        RuntimeDlls = @()
        Description = "Script API Wrapperが未接続・対象なしでもCrashせずfalseを返すか"
    },
    [pscustomobject]@{
        Name        = "BlastLowLevelSmoke"
        Source      = Join-Path $PSScriptRoot "BlastLowLevelSmoke.cpp"
        Includes    = @($blastInclude)
        Libraries   = @((Join-Path $blastLibDirectory "NvBlast.lib"))
        RuntimeDlls = @((Join-Path $blastBinDirectory "NvBlast.dll"))
        Description = "Blast低レベルAPIで破壊分割が2 Actorになるか"
    }
)

if (Test-Path -LiteralPath $OutputDirectory) {
    Remove-Item -LiteralPath $OutputDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null

$failureCount = 0

foreach ($testCase in $testCases) {
    Write-Host ""
    Write-Host "=== $($testCase.Name)" -ForegroundColor Cyan
    Write-Host "    $($testCase.Description)"

    if (-not (Test-Path -LiteralPath $testCase.Source)) {
        Write-Host "    SKIP: ソースがありません ($($testCase.Source))" -ForegroundColor Yellow
        continue
    }

    $exePath = Join-Path $OutputDirectory "$($testCase.Name).exe"

    # /utf-8 は本体と同じ。日本語コメントを含むソースでも文字化けしない。
    # /W4 は本体(Level3)より厳しくしている。テストは新規コードなので警告0を保てる。
    $compilerArguments = @(
        "/nologo", "/std:c++20", "/utf-8", "/W4", "/EHsc", "/permissive-"
    )
    # NDEBUG / _DEBUG は MSBuild が自動で付けるが cl.exe 直呼びでは付かない。
    # Blast の NvPreprocessor.h はどちらか一方が必須なので明示する。
    $compilerArguments += if ($Configuration -eq "Debug") {
        @("/Od", "/MDd", "/Zi", "/D_DEBUG")
    } else {
        @("/O2", "/MD", "/DNDEBUG")
    }
    foreach ($include in $testCase.Includes) {
        if (-not (Test-Path -LiteralPath $include)) {
            throw "include が見つかりません: $include"
        }
        $compilerArguments += "/I$include"
    }
    $compilerArguments += @("/Fe:$exePath", "/Fo:$OutputDirectory\", "/Fd:$OutputDirectory\")
    $compilerArguments += $testCase.Source
    if ($testCase.Libraries.Count -gt 0) {
        foreach ($library in $testCase.Libraries) {
            if (-not (Test-Path -LiteralPath $library)) {
                throw "lib が見つかりません: $library"
            }
        }
        $compilerArguments += "/link"
        $compilerArguments += $testCase.Libraries
    }

    & $clExe @compilerArguments | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "    BUILD FAILED (cl.exe exit $LASTEXITCODE)" -ForegroundColor Red
        $failureCount++
        continue
    }

    # 依存 DLL は exe と同じ場所へ置く。PATH を汚さずに実行できる。
    foreach ($runtimeDll in $testCase.RuntimeDlls) {
        if (Test-Path -LiteralPath $runtimeDll) {
            Copy-Item -LiteralPath $runtimeDll -Destination $OutputDirectory -Force
        }
    }

    & $exePath
    $testExitCode = $LASTEXITCODE
    if ($testExitCode -eq 0) {
        Write-Host "    PASS" -ForegroundColor Green
    } else {
        Write-Host "    FAIL (exit $testExitCode)" -ForegroundColor Red
        $failureCount++
    }
}

Write-Host ""
if ($failureCount -eq 0) {
    Write-Host "全 $($testCases.Count) 件成功" -ForegroundColor Green
} else {
    Write-Host "$failureCount / $($testCases.Count) 件失敗" -ForegroundColor Red
}
exit $failureCount
