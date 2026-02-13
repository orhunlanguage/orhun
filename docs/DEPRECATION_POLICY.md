# Orhun Deprecation Policy

## Amaç
Kırıcı değişiklikleri öngörülebilir hale getirmek ve migration süresini garanti etmek.

## Politika
1. `nightly` kanalında yeni davranışlar önce uyarı ile gelir.
2. Bir özelliğin kaldırılması için en az şu akış izlenir:
   - `N` sürümü: deprecation uyarısı (çalışır, uyarı basar)
   - `N+1` sürümü: varsayılan kapalı, opt-in bayrakla açık
   - `N+2` sürümü: tamamen kaldırılır
3. Her deprecation için:
   - Hata kodu veya uyarı kodu
   - Alternatif kullanım
   - Son kaldırılacağı sürüm notlarda belirtilir

## Hedef
- Stable kullanıcılarının en az 2 sürüm önceden haberdar edilmesi.
