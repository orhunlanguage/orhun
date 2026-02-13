param(
    [string]$Compiler = "g++",
    [string]$Output = "orhun_bench.exe"
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $Output)) {
    Write-Host "[build] $Output bulunamadi, derleniyor..."
    & $Compiler -std=c++17 -Wall -Wextra -pedantic `
        main.cpp Lexer.cpp Parser.cpp Interpreter.cpp Chunk.cpp Compiler.cpp VM.cpp `
        -o $Output
}

$cases = @(
    "tests/cases/basic_math.oh",
    "tests/cases/assignment_equals.oh",
    "tests/cases/while_float.oh",
    "tests/cases/list_comprehension.oh"
)

Write-Host "[bench] Orhun hiz karsilastirma"
foreach ($src in $cases) {
    Write-Host ""
    Write-Host "=== $src ==="
    & ".\$Output" hiz $src 40
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[skip] $src benchmark atlandi (VM destek disi olabilir)."
    }
}
