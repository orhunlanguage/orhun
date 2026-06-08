# Orhun Release Channels

## Kanallar
1. `nightly`
- Günlük otomatik derleme/test.
- Deneysel ve hızlı değişim.

2. `beta`
- Nightly'de doğrulanan değişimlerin toplandığı aday sürüm.
- Regression ve performans kıyasları zorunlu.

3. `stable`
- Üretim kullanımı için önerilen kanal.
- Yalnız doğrulanmış ve migration notu tamamlanmış değişiklikler.

## Yayın Kriterleri
- Test matrisi (Windows + Linux + macOS) yeşil.
- `vm-kati` testleri yeşil.
- Benchmark raporu eklenmiş.
- Güvenlik notları ve known-issues güncel.

## Sürümlü Yayın Akışı

- `VERSION` yayınlanacak semantik sürümü taşır.
- Yayın etiketi bu değerle tam eşleşmelidir: örneğin `VERSION` `0.8.0` ise
  etiket yalnız `v0.8.0` olabilir.
- Etiket push'u `.github/workflows/release.yml` hattını başlatır.
- Hat üç platformda testleri çalıştırır, bootstrap toolchain'i yeniden üretir,
  taşınabilir derleyici bundle'ını doğrular ve sürümlü arşiv üretir.
- Her arşiv için ayrı `.sha256` dosyası ve tüm arşivleri kapsayan
  `SHA256SUMS` yayınlanır.
- Release asset'leri kısa ömürlü OIDC/Sigstore sertifikası kullanan
  GitHub artifact attestation ile imzalı build provenance kaydına bağlanır.
- `0.x` ve ön sürüm etiketleri GitHub üzerinde prerelease olarak işaretlenir.

SHA-256 dosyaları aktarım bütünlüğünü doğrular, ancak yayın sahibinin kimliğini
kanıtlayan provenance doğrulamasının yerini tutmaz. Kullanıcılar indirdikleri
asset'i `gh attestation verify <dosya> --repo orhunlanguage/orhun` ile
doğrulayabilir. Platform code-signing ve kurulum paketi imzalama ayrı bir
gelecek adımdır.
