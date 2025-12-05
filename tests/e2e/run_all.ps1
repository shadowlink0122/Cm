# tests/e2e/run_all.ps1
# E2E test runner for Cm language (Windows)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$CmBin = if ($env:CM_BIN) { $env:CM_BIN } else { "$ScriptDir\..\..\build\bin\Debug\cm.exe" }
$Failed = 0
$Passed = 0
$Skipped = 0

Write-Host "=========================================="
Write-Host "Cm E2E Test Runner (Windows)"
Write-Host "=========================================="
Write-Host "Binary: $CmBin"
Write-Host ""

# Check if binary exists
if (-not (Test-Path $CmBin)) {
    Write-Host "Warning: Cm binary not found at $CmBin" -ForegroundColor Yellow
    Write-Host "Skipping E2E tests"
    exit 0
}

# Run interpreter tests
$InterpreterDir = "$ScriptDir\interpreter"
if (Test-Path $InterpreterDir) {
    Write-Host "--- Interpreter Tests ---"
    Get-ChildItem "$InterpreterDir\*.cm" | ForEach-Object {
        $TestFile = $_.FullName
        $Name = $_.BaseName
        $Expected = "$InterpreterDir\$Name.expected"
        
        if (-not (Test-Path $Expected)) {
            Write-Host "SKIP: $Name (no .expected file)" -ForegroundColor Yellow
            $script:Skipped++
            return
        }
        
        try {
            $Actual = & $CmBin run $TestFile 2>&1 | Out-String
            $ExpectedContent = Get-Content $Expected -Raw
            
            if ($Actual.Trim() -eq $ExpectedContent.Trim()) {
                Write-Host "PASS: $Name" -ForegroundColor Green
                $script:Passed++
            } else {
                Write-Host "FAIL: $Name" -ForegroundColor Red
                Write-Host "  Expected: $ExpectedContent"
                Write-Host "  Actual:   $Actual"
                $script:Failed++
            }
        } catch {
            Write-Host "FAIL: $Name (execution error)" -ForegroundColor Red
            $script:Failed++
        }
    }
}

Write-Host ""
Write-Host "=========================================="
Write-Host "Results: $Passed passed, $Failed failed, $Skipped skipped"
Write-Host "=========================================="

exit $Failed
