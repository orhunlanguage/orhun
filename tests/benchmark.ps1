param(
    [string]$Compiler = "g++",
    [string]$Output = "build/orhun_bench.exe",
    [int]$Tekrar = 30,
    [int]$Warmup = 10,
    [ValidateSet("runtime", "full")]
    [string]$OlcumModu = "runtime",
    [string]$JsonCikti = "build/benchmark_results.jsonl",
    [string]$Baseline = "",
    [double]$GateP50 = 0.0,
    [double]$GateP90 = 0.0,
    [ValidateSet("suite", "per_case")]
    [string]$GateMode = "suite",
    [string]$GateBaseline = "",
    [double]$GateBaselineP50 = 0.0,
    [double]$GateBaselineP90 = 0.0
)

$ErrorActionPreference = "Stop"
trap {
    Write-Host "Benchmark altyapi hatasi: $($_.Exception.Message)"
    exit 3
}

$outputDir = Split-Path -Parent $Output
if ($outputDir -and !(Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}
$jsonDir = Split-Path -Parent $JsonCikti
if ($jsonDir -and !(Test-Path $jsonDir)) {
    New-Item -ItemType Directory -Path $jsonDir -Force | Out-Null
}

if (!(Test-Path $Output)) {
    Write-Host "[build] $Output bulunamadi, derleniyor..."
    & $Compiler -std=c++17 -O2 -DNDEBUG -Wall -Wextra -pedantic `
        main.cpp Lexer.cpp Parser.cpp Interpreter.cpp Chunk.cpp Compiler.cpp VM.cpp `
        -o $Output
}

$cases = @(
    "tests/benchmarks/python_compare/fib_recursive.oh",
    "tests/benchmarks/python_compare/fib_iterative.oh",
    "tests/benchmarks/python_compare/nbody.oh",
    "tests/benchmarks/python_compare/spectral_norm.oh",
    "tests/benchmarks/python_compare/json_loads.oh",
    "tests/benchmarks/python_compare/string_concat.oh",
    "tests/benchmarks/python_compare/binary_tree.oh",
    "tests/benchmarks/python_compare/method_call.oh",
    "tests/benchmarks/python_compare/matrix_mult.oh",
    "tests/benchmarks/python_compare/primes.oh"
)

if (Test-Path $JsonCikti) {
    Remove-Item $JsonCikti -Force
}

$okSayisi = 0
$hataSayisi = 0

Write-Host "[bench] Orhun hiz karsilastirma (JSONL: $JsonCikti)"
foreach ($src in $cases) {
    Write-Host ""
    Write-Host "=== $src ==="
    $args = @("hiz", $src, "--tekrar=$Tekrar", "--warmup=$Warmup", "--olcum-modu=$OlcumModu", "--json")
    if ($Baseline -ne "") {
        $args += @("--baseline", $Baseline)
    }
    $json = & ".\$Output" @args 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        $hataDetayi = ($json -replace "`r`n", "`n").Trim()
        if ([string]::IsNullOrWhiteSpace($hataDetayi)) {
            $hataDetayi = "exit_code=$LASTEXITCODE"
        }
        Write-Host "[FAIL] $src benchmark basarisiz (exit=$LASTEXITCODE)"
        Write-Host $hataDetayi
        $hataSayisi++
    } else {
        $json = ($json -replace "`r`n", "`n").Trim()
        Add-Content -Path $JsonCikti -Value $json -Encoding utf8
        Write-Host $json
        $okSayisi++
    }
}

if ($hataSayisi -gt 0) {
    Write-Host "Benchmark smoke basarisiz: $hataSayisi testte hata alindi."
    exit 3
}
if (!(Test-Path $JsonCikti)) {
    Write-Host "Benchmark smoke basarisiz: JSONL dosyasi uretilmedi: $JsonCikti"
    exit 3
}
$satirSayisi = (Get-Content $JsonCikti -Encoding utf8 | Where-Object { $_.Trim().Length -gt 0 }).Count
if ($satirSayisi -le 0) {
    Write-Host "Benchmark smoke basarisiz: JSONL dosyasi bos: $JsonCikti"
    exit 3
}
Write-Host "[bench] toplam_ok=$okSayisi satir=$satirSayisi"

if ($GateP50 -gt 0 -or $GateP90 -gt 0 -or
    $GateBaselineP50 -gt 0 -or $GateBaselineP90 -gt 0) {
    Write-Host ""
    Write-Host "[gate] KPI kontrolu"
    ./tests/benchmark_gate.ps1 `
        -JsonL $JsonCikti `
        -MinP50 ([Math]::Max($GateP50, 0.0)) `
        -MinP90 ([Math]::Max($GateP90, 0.0)) `
        -Mode $GateMode `
        -BaselineJsonL $GateBaseline `
        -MinBaselineP50Ratio ([Math]::Max($GateBaselineP50, 0.0)) `
        -MinBaselineP90Ratio ([Math]::Max($GateBaselineP90, 0.0))
    exit $LASTEXITCODE
}

exit 0
