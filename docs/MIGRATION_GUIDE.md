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
- Yeni `.obc.meta.json` dosyaları `orhun-obc-v2` formatında payload boyutu,
  CRC32 ve SHA-256 içerir.
- `orhun obc-dogrula <dosya.obc> [metadata.json]`, OBC yapısını ve metadata
  bütünlüğünü doğrular. İngilizce uyumluluk takma adı `obc-verify`'dır.
- Eski `orhun-obc-v1` metadata dosyaları CRC32 ile doğrulanmaya devam eder.
- Yeni paketli çalıştırılabilir dosyalar, gömülü payload boyutu, CRC32 ve
  SHA-256 taşıyan `ORHNPKG2` trailer'ı kullanır.
- `orhun paketli-dogrula <paketli-dosya>`, payload bütünlüğünü ve OBC yapısını
  çalıştırmadan doğrular. İngilizce uyumluluk takma adı `packaged-verify`'dır.
- Eski `ORHNPKG1` paketleri doğrulanmaya ve çalıştırılmaya devam eder.

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
- `Atama` ve `CokluAtama` düğümlerinde `bildirim` alanı bulunur; `olsun`
  biçimi `true`, `=` uyumluluk ataması `false` döndürür.
- Parser hatalarında komut `1` ile çıkar, `durum: "fail"` döndürür ve `ast`
  alanını `null` yapar.

## 12. Self-hosting: `orhun/parser.oh`
- `orhun/parser.oh`, Orhun ile yazılan ilk parser prototipidir.
- Şimdilik üst seviye komut türlerini, satırlarını, ana ifade özetlerini
  (`tur`, `op`, `ayrinti`, `altlar`), recursive ifade çocuklarını,
  `Atama`/`CokluAtama` için `bildirim` bilgisini, alt blok komut sayılarını
  ve recursive çocuk blok komut özetlerini üretir;
  `parse --json` AST çıktısıyla fixture bazında karşılaştırılır.
- Hata eşleşmeleri olarak `eğer`, `sürece`, `tekrarla`, `işlev`, `tip` ve
  `deneme` başlıklarındaki eksikler, tanınmayan komutlar, varsayılan
  parametreden sonra gelen zorunlu parametreler ve çok satırlı sözlük anahtarı
  hataları yakalanır.

## 13. Closure Capture
- Döndürülen iç içe işlevler ve anonim işlevler artık dış yerel değişkenleri
  canlı tutar.
- Aynı dış çağrı içinde üretilen closure'lar aynı yakalanmış değişken hücresini
  paylaşır; ayrı dış çağrılar ayrı hücre oluşturur.
- Döngü içinde `x olsun i` gibi yeni yerel bağlar her iterasyonda bağımsız
  capture hücresi üretir.
- Önceki davranışta bazı döndürülen lambda'lar dış yerel değişken yerine aynı
  adlı global değere düşebiliyordu. Yeni davranışta en yakın dış yerel bağ
  kullanılır.

## 14. Self-hosting: `baytkod --json`
- `orhun baytkod <dosya.oh> --json`, C++ derleyici çıktısını artifact
  oluşturmadan çözümlenmiş JSON olarak verir.
- Başarılı çıktıda komutlar, sabitler, kaynak satırları ve sayımlar bulunur.
- Derleme hatalarında komut `1` ile çıkar, `durum: "fail"` döndürür ve
  `bytecode` alanını `null` yapar.
- İngilizce `bytecode` komutu uyumluluk takma adıdır.

## 15. Self-hosting: `orhun/derleyici.oh`
- `orhun/derleyici.oh`, parser prototipinin yapısal IR çıktısını çözümlenmiş
  bytecode özetine dönüştüren ilk Orhun-yazılı derleyici prototipidir.
- Desteklenen subset; temel ifadeler, koleksiyonlar, global/local değişkenler,
  dallanma/döngüler, işlevler, varsayılan parametreler ve ilk sabit katlama
  kurallarını içerir.
- `tests/compiler_prototype_smoke.py`, prototip çıktısını C++ derleyicisinin
  `baytkod --json` çıktısıyla birebir karşılaştırır.
- Desteklenmeyen yapılar sessizce yanlış bytecode üretmek yerine açık hata
  döndürür.

## 16. Self-hosting: decoded bytecode execution bridge
- `orhun baytkod-yurut <dosya.json>`, Orhun ile yazılan derleyicinin çözümlenmiş
  bytecode JSON çıktısını doğrular ve C++ VM üzerinde çalıştırır.
- Komut, tam derleyici payload'ını veya doğrudan iç `bytecode` sözlüğünü kabul
  eder.
- Bilinmeyen opcode, eksik alan, geçersiz U16 operand, uyuşmayan IP/sayım ve
  bozuk işlev metadata'sı çalıştırmadan önce reddedilir.
- İngilizce `bytecode-run` komutu uyumluluk takma adıdır.
- `orhun orhun-vm <dosya.oh>`, aynı bootstrap hattını ara JSON dosyası olmadan
  tek komutta çalıştırır. İngilizce uyumluluk takma adı `bootstrap-vm`'dir.
- `orhun orhun-derle <dosya.oh> [çıktı]`, Orhun-yazılı derleyici üzerinden
  `.obc`, paketli çalıştırılabilir dosya ve metadata üretir. İngilizce uyumluluk
  takma adı `bootstrap-compile`'dır.
- `ORHUN_MODULE_MODE` varsayılan olarak `source` kalır. `obc-first`, bulunan
  kardeş `.obc` modülünü tercih eder ve gerekirse kaynağa düşer. `obc-only`,
  eksik önceden derlenmiş modülü açık hatayla reddeder ve C++ derleyici
  fallback'i yapmaz.
- `orhun-vm` ve `orhun-derle` aynı politikaları doğrudan `--source`,
  `--obc-first`, `--obc-only` veya `--modul-modu=<değer>` ile seçebilir.
- `orhun bootstrap-hazirla <dizin>`, kaynak dosyası içermeyen lexer/parser/
  derleyici ve derleyici-CLI `.obc` zincirini ve `bootstrap.manifest.json`
  dosyasını üretir.
  İngilizce uyumluluk takma adı `bootstrap-prepare`'dır.
- `orhun bootstrap-dogrula <toolchain-dizini>`, manifest sözleşmesini, modül
  kümesini, boyut/CRC değerlerini ve OBC yapısını çalıştırmadan denetler.
  İngilizce uyumluluk takma adı `bootstrap-verify`'dır.
- `orhun bootstrap-derle <toolchain-dizini> <kaynak.oh> [çıktı]`, hazırlanmış
  toolchain'i `obc-only` politikasında kullanır; ortam değişkeni gerektirmez.
  İngilizce uyumluluk takma adı `bootstrap-build`'dir.
- `orhun bootstrap-calistir <toolchain-dizini> <kaynak.oh>`, aynı doğrulanmış
  toolchain ile hedefi strict modda derleyip VM'de çalıştırır. İngilizce
  uyumluluk takma adı `bootstrap-run`'dır.
- `orhun bootstrap-derleyici-paketle <toolchain-dizini> <çıktı-dizini>`,
  kaynak-kodsuz taşınabilir `orhun-derleyici` çalıştırılabilir dosyasını ve
  kardeş strict toolchain'ini üretir. İngilizce uyumluluk takma adı
  `bootstrap-compiler-bundle`'dır. Üretilen derleyici tek kaynak argümanıyla
  bytecode JSON, `--derle <kaynak.oh> [çıktı]` ile `.obc`, paketli
  çalıştırılabilir dosya ve metadata üretir.
- `orhun bootstrap-derleyici-dogrula <bundle-dizini>`, dağıtımdan önce compiler
  manifestini, embedded CLI payload boyut/CRC değerini ve kardeş strict
  toolchain'i birlikte doğrular. İngilizce uyumluluk takma adı
  `bootstrap-compiler-verify`'dır.
- `orhun bootstrap-yeniden-uret <tohum-toolchain> <çıktı-dizini>`, tohumdan
  aşama 2 ve aşama 2'den aşama 3 üretir; son iki aşamanın dört `.obc`
  artifact'i byte düzeyinde eşleşmezse başarısız olur. Dolu çıktı dizinleri
  korunur. İngilizce uyumluluk takma adı `bootstrap-rebuild`'dir.
- Kullanıcı programlarına kaynak/komut sonrasında verilen değerler artık
  `sistem.argumanlar` listesinde bulunur. `orhun-vm` yolunda çalışma zamanı
  argümanlarını modül seçeneklerinden ayırmak için `--` kullanılır.
