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
- Test matrisi (Windows + Linux) yeşil.
- `vm-kati` testleri yeşil.
- Benchmark raporu eklenmiş.
- Güvenlik notları ve known-issues güncel.
