param(
    [string]$Model = "qwen2.5-coder:3b",
    [switch]$SkipOllama
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot ".."))
$exe = Join-Path $repo "build\continuum-uia-poc.exe"
$fixture = Join-Path $PSScriptRoot "fixtures\phase6_multi_revision.json"
$benchmarkDir = Join-Path $repo "output\context\phase6-benchmark"
$prompt = Join-Path $benchmarkDir "compilation_prompt.md"
$package = Join-Path $benchmarkDir "context_package.json"
$memoryPrompt = Join-Path $benchmarkDir "memory_update_prompt.md"

if (!(Test-Path $exe)) { throw "Build not found: $exe" }
if (!(Test-Path $fixture)) { throw "Fixture not found: $fixture" }
New-Item -ItemType Directory -Force $benchmarkDir | Out-Null

function Assert-Contains([string]$Path, [string]$Text, [string]$Description) {
    $value = Get-Content -Raw -LiteralPath $Path
    if (!$value.Contains($Text)) { throw "Benchmark failed: $Description" }
    Write-Host "PASS: $Description"
}

& $exe --compile-context $fixture $prompt
if ($LASTEXITCODE -ne 0) { throw "Compilation prompt generation failed." }
Assert-Contains $prompt "trim(input)" "superseded implementation is present"
Assert-Contains $prompt "trim_preserving_code" "current implementation is present"
Assert-Contains $prompt "source-message-2-code-1" "original artifact provenance is present"
Assert-Contains $prompt "source-message-4-code-1" "corrected artifact provenance is present"

if (!$SkipOllama) {
    & $exe --compile-local $Model $prompt $fixture
    if ($LASTEXITCODE -ne 0) { throw "Ollama context compilation failed." }
    $package = Join-Path $benchmarkDir "context_package.json"
    & $exe --validate-context $package
    if ($LASTEXITCODE -ne 0) { throw "Generated package validation failed." }
    & $exe --build-memory-prompt $package $memoryPrompt
    if ($LASTEXITCODE -ne 0) { throw "Transfer prompt generation failed." }
    Assert-Contains $memoryPrompt "trim_preserving_code" "transfer prompt contains current source"
    Assert-Contains $memoryPrompt "supersedes_id" "transfer prompt preserves revision relationships"
    Assert-Contains $memoryPrompt "duplicate-marker bug" "transfer prompt preserves correction rationale"
}

Write-Host "Phase 6 deterministic benchmark passed."
if ($SkipOllama) {
    Write-Host "Ollama stage skipped; run without -SkipOllama for full local compilation."
}
