#requires -Version 5.1
<#
    Source 配下の C++ ソースについて、あとから直すのが高くつく3点だけを検査する。

    1. 文字化けの混入
       Source 配下には U+FFFD と典型的な文字化け断片を許可しない。UTF-8 以外の
       文字コードで保存したり、壊れた文字列を貼り付けたりした場合に検出する。

    2. UTF-8 BOM の欠落
       本体は /utf-8 でコンパイルするので BOM が無くても通るが、BOM 無し UTF-8 を
       Visual Studio が ANSI と誤認して保存し直すと 1 の文字化けが再発する。
       リポジトリの既存ファイルは全て BOM 付きなので、その状態を保つ。

    3. assert(SUCCEEDED(...)) の再導入
       assert は Release(NDEBUG)で消えるため、これだけで HRESULT を見ていると
       Release 構成が無検査になる。代わりに EDITOR_HR_OK / EDITOR_HR_VERIFY を使う
       (Source/Engine/Core/EditorHrCheck.h)。

    終了コード 0 = 問題なし。1 = 違反あり。
#>
param(
    # Source 配下では文字化けを許可しない。移行作業中だけ明示的に上限を指定できる。
    [int]$AllowedReplacementCharacterCount = 0,

    [switch]$Quiet
)

$ErrorActionPreference = "Stop"
$workspaceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$sourceRoot = Join-Path $workspaceRoot "Source"

if (-not (Test-Path -LiteralPath $sourceRoot)) {
    throw "Source フォルダが見つかりません: $sourceRoot"
}

$sourceFiles = Get-ChildItem -LiteralPath $sourceRoot -Recurse -File -Include *.cpp, *.h, *.hpp

$replacementTotal = 0
$replacementFiles = @()
$mojibakeFragmentHits = @()
$missingBomFiles = @()
$assertHresultHits = @()
$mojibakeFragmentPattern = "縺|繧|繝|譁|蜿|邵|郢|陷|隴|ぁE|めE|亁E|綁E|琁E|宁E|匁E|吁E|晁E|、E\s*$|。E\s*$"

foreach ($sourceFile in $sourceFiles) {
    $bytes = [System.IO.File]::ReadAllBytes($sourceFile.FullName)

    # --- 2. BOM 検査 ---
    $hasBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    if (-not $hasBom) {
        $missingBomFiles += $sourceFile.FullName.Substring($workspaceRoot.Length + 1)
    }

    $text = [System.Text.Encoding]::UTF8.GetString($bytes)

    # --- 1. 文字化け検査 ---
    $replacementCount = ([regex]::Matches($text, "�")).Count
    if ($replacementCount -gt 0) {
        $replacementTotal += $replacementCount
        $replacementFiles += [pscustomobject]@{
            Path  = $sourceFile.FullName.Substring($workspaceRoot.Length + 1)
            Count = $replacementCount
        }
    }

    if ($text -match $mojibakeFragmentPattern) {
        $mojibakeFragmentHits += $sourceFile.FullName.Substring($workspaceRoot.Length + 1)
    }

    # --- 3. assert(SUCCEEDED(...)) 検査 ---
    # EditorHrCheck.h 自身は説明コメントでこの文字列に触れるため除外する。
    if ($sourceFile.Name -ne "EditorHrCheck.h") {
        $lines = $text -split "`r?`n"
        for ($lineIndex = 0; $lineIndex -lt $lines.Count; $lineIndex++) {
            if ($lines[$lineIndex] -match "assert\s*\(\s*SUCCEEDED\s*\(") {
                $relativePath = $sourceFile.FullName.Substring($workspaceRoot.Length + 1)
                $assertHresultHits += "${relativePath}:$($lineIndex + 1)"
            }
        }
    }
}

$hasViolation = $false

function Write-Section {
    param([string]$Text)
    if (-not $Quiet) { Write-Host $Text }
}

Write-Section ""
Write-Section "検査対象: $($sourceFiles.Count) ファイル"

# --- 結果 1 ---
Write-Section ""
Write-Section "[1] 文字化け (U+FFFD): $replacementTotal 個 / 上限 $AllowedReplacementCharacterCount 個"
foreach ($replacementFile in ($replacementFiles | Sort-Object -Property Count -Descending)) {
    Write-Section ("    {0,6} 個  {1}" -f $replacementFile.Count, $replacementFile.Path)
}
foreach ($mojibakeFragmentHit in $mojibakeFragmentHits) {
    Write-Section "    文字化け断片  $mojibakeFragmentHit"
}
if ($replacementTotal -gt $AllowedReplacementCharacterCount) {
    Write-Host "    NG: 文字化けが増えています。Shift-JIS で保存されたファイルがないか確認してください。" -ForegroundColor Red
    $hasViolation = $true
}
elseif ($replacementTotal -lt $AllowedReplacementCharacterCount) {
    Write-Section "    改善しています。-AllowedReplacementCharacterCount の既定値を $replacementTotal へ下げてください。"
}
if ($mojibakeFragmentHits.Count -gt 0) {
    Write-Host "    NG: 典型的な文字化け断片が残っています。" -ForegroundColor Red
    $hasViolation = $true
}

# --- 結果 2 ---
Write-Section ""
Write-Section "[2] UTF-8 BOM なし: $($missingBomFiles.Count) ファイル"
foreach ($missingBomFile in $missingBomFiles) {
    Write-Section "    $missingBomFile"
}
if ($missingBomFiles.Count -gt 0) {
    Write-Host "    NG: BOM を付けて保存してください。BOM 無し UTF-8 は ANSI と誤認されて文字化けの原因になります。" -ForegroundColor Red
    $hasViolation = $true
}

# --- 結果 3 ---
Write-Section ""
Write-Section "[3] assert(SUCCEEDED(...)): $($assertHresultHits.Count) 箇所"
foreach ($assertHresultHit in $assertHresultHits) {
    Write-Section "    $assertHresultHit"
}
if ($assertHresultHits.Count -gt 0) {
    Write-Host "    NG: Release で検査が消えます。EDITOR_HR_OK / EDITOR_HR_VERIFY を使ってください。" -ForegroundColor Red
    $hasViolation = $true
}

Write-Section ""
if ($hasViolation) {
    Write-Host "違反あり" -ForegroundColor Red
    exit 1
}

Write-Host "問題なし" -ForegroundColor Green
exit 0
