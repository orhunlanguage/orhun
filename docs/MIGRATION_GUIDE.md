# Orhun Migration Guide (V2)

## 1. VM Fallback Davranışı
- Varsayılan davranış VM'dir.
- Fallback varsayılanı artık kanal bazlıdır:
  - `stable` -> kapalı
  - `beta/nightly/dev` -> açık
- Kanal seçimi:
  - `ORHUN_CHANNEL=stable|beta|nightly|dev`
  - (alternatif) `ORHUN_RELEASE_CHANNEL=...`
- Manuel override:
  - `ORHUN_VM_FALLBACK=0` -> fallback kapalı
  - `ORHUN_VM_FALLBACK=1` -> fallback açık

## 2. Güvenli Komut Çalıştırma
- `sistem.komut` artık varsayılan kısıtlı modda.
- Tehlikeli karakter içeren komutlar engellenir.
- Gerekirse açık mod:
  - `ORHUN_UNSAFE=1`
  - veya `ORHUN_SYSTEM_UNSAFE=1`
- FFI güvenlik politikası:
  - `ORHUN_FFI_POLICY=off|allowlist|full`
  - stable kanalda varsayılan: `allowlist`
  - `ORHUN_FFI_ALLOWLIST=libc.so.6,kernel32.dll` ile ek izinli kütüphaneler tanımlanabilir.

## 3. Paket Güveni
- Uzak kaynaklar allowlist ile doğrulanır.
- Ek izinli alan adları:
  - `ORHUN_PAKET_ALLOWLIST=example.com,git.example.org`
- Paket kurulumu `orhun.lock` dosyasını `v3` formatında günceller:
  - commit pin (`commit_pin`)
  - içerik hash (`icerik_sha256`)
- Kaynak ref sabitleme desteği:
  - `orhun paket kur <kaynak> [paket_adi] --ref <tag|commit>`
  - lock satırında `source_ref` alanı saklanır.
  - `paket dogrula`, `source_ref -> commit_pin` tutarlılığını doğrular.
- `orhun paket dogrula` artık lock v3 için commit + içerik hash doğrulaması yapar.
- Mevcut lock dosyasını yükseltmek için:
  - `orhun paket lock-guncelle`

## 4. Derleme Metadata
- `orhun derle` artık:
  - `.obc`
  - `.exe`
  - `.obc.meta.json` üretir
- `.obc.meta.json` dosyasında payload boyutu ve CRC32 bulunur.

## 5. Benchmark Semantiği (`orhun hiz`)
- Yeni ölçüm seçenekleri:
  - `--olcum-modu=runtime|full` (varsayılan: `runtime`)
  - `--warmup=N` (varsayılan: `10`)
- `runtime` modu:
  - Parse + VM compile bir kez yapılır.
  - Tekrar döngüsünde sadece çalışma süresi ölçülür.
- `full` modu:
  - Parse + compile + run toplam maliyeti ölçülür.
- `--json` çıktısında yeni alanlar:
  - `olcum_modu`
  - `warmup`
  - `parse_ms`
  - `vm_compile_ms`
  - `gate_result`
- `gate_result` alanı `pass` veya `fail` olur.
- Çıkış kodları:
  - `0`: ölçüm tamamlandı, gate geçti (veya gate kapalı)
  - `1`: argüman/çalıştırma hatası
  - `2`: gate başarısız
  - `3`: benchmark altyapı hatası (örn. baseline JSONL okunamıyor)

## 6. Benchmark Gate Modu
- `tests/benchmark_gate.ps1` ve `tests/benchmark_gate.sh` iki mod destekler:
  - `suite` (varsayılan): tüm test seti için medyan P50/P90 üzerinden kapı.
  - `per_case`: her test dosyası için tek tek kapı.
- Varsayılan JSONL yolu: `build/benchmark_results.jsonl`
- Regresyon budget (opsiyonel) parametreleri:
  - `BaselineJsonL` / `BASELINE_JSONL`: karşılaştırma için önceki JSONL
  - `MinBaselineP50Ratio` / `MIN_BASELINE_P50_RATIO`
  - `MinBaselineP90Ratio` / `MIN_BASELINE_P90_RATIO`
- Budget etkin olduğunda her case için `hizlanma.p50_x` ve `hizlanma.p90_x`
  baseline ile oranlanır; eşik altına düşüş `gate fail` üretir.
- Önerilen CI kullanımı:
  - nightly/beta için `suite` + aşamalı eşikler
  - stabil sürüm adayı için `per_case` + daha sıkı eşikler

## 7. LSP v2 Kapsamı
- `textDocument/hover`
- `textDocument/signatureHelp`
- `textDocument/references`
- `textDocument/rename`
- `textDocument/didChange` artık range tabanlı incremental değişiklikleri uygular
  (`textDocumentSync=2`).
- Yeni başlatma seçeneği:
  - `orhun lsp --stdio --workspace-root <path>`
- Workspace altında `.oh` dosyaları indekslenir (açık belge dışı semboller de
  `workspace/symbol` ve `definition` akışına dahil edilir).

## 8. Doctor JSON
- `orhun doctor --json` makine-okur çıktı üretir.
- Sabit alanlar:
  - `version`
  - `commit`
  - `channel`
  - `fallback_default`
  - `ci_profiles`
  - `security_mode`

## 9. DX: `fmt` ve `lint`
- `orhun fmt` yeni seçenekler:
  - `--check`: dosyayı yazmadan biçim farkı kontrolü yapar
  - `--json`: CI/IDE için makine-okur çıktı üretir
- `orhun lint` yeni seçenek:
  - `--json`: özet + mesajları JSON verir (`durum`, `hata_sayisi`,
    `uyari_sayisi`, `mesajlar`)
- CI için önerilen akış:
  - `orhun fmt <dosya> --check --json`
  - `orhun lint <dosya> --strict --json`

## 10. Self-hosting: `lex --json`
- `orhun lex <dosya.oh> --json`, C++ lexer token akışını JSON olarak verir.
- Bu çıktı, Orhun ile yazılan `orhun/lexer.oh` prototipiyle parity testlerinde
  kullanılır.

## 11. Self-hosting: `parse --json`
- `orhun parse <dosya.oh> --json`, C++ parser AST'sini JSON olarak verir.
- Başarılı çıktıda `durum: "ok"`, `hata_sayisi: 0` ve `ast` alanı bulunur.
- Parser hatalarında komut `1` ile çıkar, `durum: "fail"` döndürür ve `ast`
  alanını `null` yapar.

## 12. Self-hosting: `orhun/parser.oh`
- `orhun/parser.oh`, Orhun ile yazılan ilk parser prototipidir.
- Şimdilik üst seviye komut türlerini, satırlarını, ana ifade özetlerini
  (`tur`, `op`, `ayrinti`, `altlar`), doğrudan alt blok komut sayılarını ve
  bloklardaki doğrudan çocuk komut özetlerini üretir; `parse --json` AST
  çıktısıyla fixture bazında karşılaştırılır.
- İlk temel hata eşleşmeleri olarak `eğer`, `sürece`, `tekrarla`, `işlev`,
  `tip` ve `deneme` başlıklarındaki eksikler yakalanır.
