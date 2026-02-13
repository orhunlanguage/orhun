param(
    [string]$Compiler = "g++",
    [string]$Output = "orhun_test.exe"
)

$ErrorActionPreference = "Stop"

Write-Host "[1/3] Derleniyor..."
& $Compiler -std=c++17 -Wall -Wextra -pedantic `
    main.cpp Lexer.cpp Parser.cpp Interpreter.cpp Chunk.cpp Compiler.cpp VM.cpp `
    -o $Output

$cases = @(
    "tests/cases/basic_math",
    "tests/cases/oop_super",
    "tests/cases/list_comprehension",
    "tests/cases/try_break_continue",
    "tests/cases/assignment_equals",
    "tests/cases/json_parse",
    "tests/cases/f_string",
    "tests/cases/slicing",
    "tests/cases/stdlib_modules",
    "tests/cases/stdlib_database",
    "tests/cases/stdlib_regex_date",
    "tests/cases/dict_nested",
    "tests/cases/while_float",
    "tests/cases/module_stdlib",
    "tests/cases/try_catch_runtime",
    "tests/cases/f_string_escape",
    "tests/cases/vm_loop_control"
)

if ($env:OS -eq "Windows_NT") {
    $cases += "tests/cases/ffi_kernel32"
    $cases += "tests/cases/ffi_text"
    $cases += "tests/cases/ffi_symbol"
}

$failed = $false

Write-Host "[2/3] Testler calisiyor..."
foreach ($case in $cases) {
    $src = "$case.oh"
    $expectedPath = "$case.expected.txt"

    $actual = & ".\$Output" $src 2>&1 | Out-String
    $actual = $actual -replace "`r`n", "`n"
    $actual = $actual.TrimEnd("`n")

    $expected = Get-Content $expectedPath -Raw -Encoding utf8
    $expected = $expected -replace "`r`n", "`n"
    $expected = $expected.TrimEnd("`n")

    if ($actual -ne $expected) {
        Write-Host ""
        Write-Host "[HATA] $src"
        Write-Host "Beklenen:"
        Write-Host $expected
        Write-Host "Alinan:"
        Write-Host $actual
        $failed = $true
    } else {
        Write-Host "[OK] $src"
    }
}

Write-Host "[3/4] VM-kati secili testler..."
$vmCases = @(
    "tests/cases/basic_math",
    "tests/cases/assignment_equals",
    "tests/cases/oop_super",
    "tests/cases/f_string",
    "tests/cases/f_string_escape",
    "tests/cases/json_parse",
    "tests/cases/dict_nested",
    "tests/cases/while_float",
    "tests/cases/list_comprehension",
    "tests/cases/try_break_continue",
    "tests/cases/try_catch_runtime",
    "tests/cases/stdlib_modules",
    "tests/cases/stdlib_database",
    "tests/cases/stdlib_regex_date",
    "tests/cases/module_stdlib",
    "tests/cases/vm_loop_control",
    "tests/cases/slicing",
    "tests/cases/vm_try_catch"
)

if ($env:OS -eq "Windows_NT") {
    $vmCases += "tests/cases/ffi_kernel32"
    $vmCases += "tests/cases/ffi_text"
    $vmCases += "tests/cases/ffi_symbol"
}

foreach ($case in $vmCases) {
    $src = "$case.oh"
    $expectedPath = "$case.expected.txt"

    $actual = & ".\$Output" vm-kati $src 2>&1 | Out-String
    $actual = $actual -replace "`r`n", "`n"
    $actual = $actual.TrimEnd("`n")

    $expected = Get-Content $expectedPath -Raw -Encoding utf8
    $expected = $expected -replace "`r`n", "`n"
    $expected = $expected.TrimEnd("`n")

    if ($actual -ne $expected) {
        Write-Host ""
        Write-Host "[VM-HATA] $src"
        Write-Host "Beklenen:"
        Write-Host $expected
        Write-Host "Alinan:"
        Write-Host $actual
        $failed = $true
    } else {
        Write-Host "[VM-OK] $src"
    }
}

Write-Host "[4/4] Sonuc..."
if ($failed) {
    throw "Bazi testler basarisiz."
}

Write-Host "Tum testler basarili."
