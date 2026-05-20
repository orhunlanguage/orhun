param(
    [string]$Compiler = "g++",
    [string]$Output = "build/orhun_test.exe",
    [int]$TimeoutSeconds = 10
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$outputDir = Split-Path -Parent $Output
if ($outputDir -and !(Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}

if (!(Test-Path $Output)) {
    Write-Host "[vm-parity] Binary not found, building: $Output"
    & $Compiler -std=c++17 -Wall -Wextra -pedantic `
        main.cpp Lexer.cpp Parser.cpp Interpreter.cpp Chunk.cpp Compiler.cpp VM.cpp `
        -o $Output
}

function Run-Orhun($exe, $argsList) {
    $pinfo = New-Object System.Diagnostics.ProcessStartInfo
    $pinfo.FileName = $exe
    $pinfo.Arguments = $argsList
    $pinfo.RedirectStandardOutput = $true
    $pinfo.RedirectStandardError = $true
    $pinfo.UseShellExecute = $false
    $pinfo.StandardOutputEncoding = [System.Text.Encoding]::UTF8
    $pinfo.StandardErrorEncoding = [System.Text.Encoding]::UTF8

    $p = New-Object System.Diagnostics.Process
    $p.StartInfo = $pinfo
    $p.Start() | Out-Null
    $stdoutTask = $p.StandardOutput.ReadToEndAsync()
    $stderrTask = $p.StandardError.ReadToEndAsync()

    $timeoutMs = [Math]::Max(1, $TimeoutSeconds) * 1000
    if (-not $p.WaitForExit($timeoutMs)) {
        try {
            $p.Kill()
            $p.WaitForExit()
        }
        catch {
        }
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        return ($stdout + $stderr + "Hata: test zaman asimi (${TimeoutSeconds}s)")
    }

    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    $combined = $stdout + $stderr
    if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 1) {
        if ($combined.Length -gt 0 -and -not $combined.EndsWith("`n")) {
            $combined += "`n"
        }
        $combined += "Hata: beklenmeyen cikis kodu ($($p.ExitCode))"
    }

    return $combined
}

$cases = Get-ChildItem "tests/cases" -Filter "*.expected.txt" |
    ForEach-Object { $_.FullName -replace "\.expected\.txt$", "" } |
    ForEach-Object { $_ -replace "\\", "/" } |
    Sort-Object |
    Where-Object { Test-Path "$_.oh" }

$strictCase = (Join-Path (Resolve-Path "tests/cases").Path "turkce_kati_alias") -replace "\\", "/"
$cases = $cases | Where-Object { $_ -ne $strictCase }

if ($env:OS -ne "Windows_NT") {
    $cases = $cases | Where-Object {
        $_ -ne "tests/cases/ffi_kernel32" -and
        $_ -ne "tests/cases/ffi_text" -and
        $_ -ne "tests/cases/ffi_symbol" -and
        $_ -ne "tests/cases/ffi_tanimli_kernel32" -and
        $_ -ne "tests/cases/ffi_dis_islev"
    }
}

$ok = 0
$fail = 0
foreach ($case in $cases) {
    $src = "$case.oh"
    $expectedPath = "$case.expected.txt"

    $actual = Run-Orhun ".\$Output" "vm-kati $src"
    $actual = $actual -replace "`r`n", "`n"
    $actual = $actual.TrimEnd("`n")

    $expected = Get-Content $expectedPath -Raw -Encoding utf8
    $expected = $expected -replace "`r`n", "`n"
    $expected = $expected.TrimEnd("`n")

    if ($actual -ne $expected) {
        Write-Host "[VM-FAIL] $src"
        Write-Host "Beklenen:"
        Write-Host $expected
        Write-Host "Alinan:"
        Write-Host $actual
        $fail++
    }
    else {
        Write-Host "[VM-OK] $src"
        $ok++
    }
}

if ((Test-Path "$strictCase.oh") -and (Test-Path "$strictCase.expected.txt")) {
    $src = "$strictCase.oh"
    $expectedPath = "$strictCase.expected.txt"
    $oncekiKati = $env:ORHUN_TURKCE_KATI
    $env:ORHUN_TURKCE_KATI = "1"
    try {
        $actual = Run-Orhun ".\$Output" "vm-kati $src"
    }
    finally {
        $env:ORHUN_TURKCE_KATI = $oncekiKati
    }
    $actual = $actual -replace "`r`n", "`n"
    $actual = $actual.TrimEnd("`n")

    $expected = Get-Content $expectedPath -Raw -Encoding utf8
    $expected = $expected -replace "`r`n", "`n"
    $expected = $expected.TrimEnd("`n")

    if ($actual -ne $expected) {
        Write-Host "[VM-FAIL] $src (ORHUN_TURKCE_KATI=1)"
        Write-Host "Beklenen:"
        Write-Host $expected
        Write-Host "Alinan:"
        Write-Host $actual
        $fail++
    }
    else {
        Write-Host "[VM-OK] $src (ORHUN_TURKCE_KATI=1)"
        $ok++
    }
}

Write-Host "vm_parity_ok=$ok"
Write-Host "vm_parity_fail=$fail"
if ($fail -gt 0) {
    throw "VM parity basarisiz."
}
