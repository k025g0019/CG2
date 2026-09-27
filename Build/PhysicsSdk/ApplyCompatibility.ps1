[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$PhysXRoot)
$ErrorActionPreference = 'Stop'
$sourcePath = Join-Path $PhysXRoot 'source\cudamanager\src\CudaContextManager.cpp'
$sourceText = [System.IO.File]::ReadAllText($sourcePath)
$patchMarker = '// CG2: CUDA 13 context creation compatibility.'

# SDK の元ファイルを変えず、プロジェクト内のコピーに同じ修正を再現する。
if ($sourceText.Contains($patchMarker)) {
    return
}

$originalCall = 'status = cuCtxCreate(&mCtx, (unsigned int)flags, mDevHandle);'

if (!$sourceText.Contains($originalCall)) {
    throw 'PhysX CUDA compatibility patch: expected cuCtxCreate call was not found. Review the new SDK source.'
}

$replacementCall = @'
// CG2: CUDA 13 context creation compatibility.
#if CUDA_VERSION >= 13000
            CUctxCreateParams contextParams{};
            status = cuCtxCreate(&mCtx, &contextParams, static_cast<unsigned int>(flags), mDevHandle);
#else
            status = cuCtxCreate(&mCtx, static_cast<unsigned int>(flags), mDevHandle);
#endif
'@
$sourceText = $sourceText.Replace($originalCall, $replacementCall)
$sourceText = $sourceText -replace '\r?\n', "`r`n"
[System.IO.File]::WriteAllText($sourcePath, $sourceText, [System.Text.UTF8Encoding]::new($true))
Write-Host 'Applied PhysX CUDA 13 context creation compatibility patch.'
