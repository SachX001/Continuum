param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot ".."))
$exe = Join-Path $repo "build\continuum-uia-poc.exe"
$root = Join-Path $repo "output\context\phase7-stabilization"
$invalidConversation = Join-Path $root "invalid-conversation.json"
$customPrompt = Join-Path $root "custom-compilation-prompt.md"
$sentinelPackage = Join-Path $root "context_package.json"
$sentinel = '{"sentinel":"must remain unchanged"}'

if (!$SkipBuild) {
    $env:Path = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + $env:Path
    cmake --build (Join-Path $repo "build") --parallel 2
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }
}

New-Item -ItemType Directory -Force $root | Out-Null
Set-Content -LiteralPath $invalidConversation -Value '{ not valid json' -NoNewline
Set-Content -LiteralPath $sentinelPackage -Value $sentinel -NoNewline

$diagnostics = Join-Path $root "compile-error.txt"
$previousErrorAction = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $exe --compile-local qwen2.5-coder:3b $customPrompt $invalidConversation 2>$diagnostics
$compileExitCode = $LASTEXITCODE
$ErrorActionPreference = $previousErrorAction
if ($compileExitCode -eq 0) {
    throw "Malformed conversation unexpectedly compiled successfully."
}

$afterFailure = Get-Content -Raw -LiteralPath $sentinelPackage
if ($afterFailure -ne $sentinel) {
    throw "Failed compilation overwrote the existing package."
}
Write-Host "PASS: malformed input fails without overwriting the existing package"

$ErrorActionPreference = "Continue"
& $exe --validate-context $sentinelPackage 2>$null
$validationExitCode = $LASTEXITCODE
$ErrorActionPreference = "Stop"
if ($validationExitCode -eq 0) {
    throw "Invalid sentinel package unexpectedly passed validation."
}
Write-Host "PASS: invalid package remains rejected"
Write-Host "Phase 7 stabilization regression passed."
