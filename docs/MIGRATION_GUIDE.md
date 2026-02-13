# Orhun Migration Guide (V2)

## 1. VM Fallback Davranışı
- Varsayılan davranış VM'dir.
- Fallback davranışını test etmek/kapatmak için:
  - `ORHUN_VM_FALLBACK=0` -> fallback kapalı

## 2. Güvenli Komut Çalıştırma
- `sistem.komut` artık varsayılan kısıtlı modda.
- Tehlikeli karakter içeren komutlar engellenir.
- Gerekirse açık mod:
  - `ORHUN_UNSAFE=1`
  - veya `ORHUN_SYSTEM_UNSAFE=1`

## 3. Paket Güveni
- Uzak kaynaklar allowlist ile doğrulanır.
- Ek izinli alan adları:
  - `ORHUN_PAKET_ALLOWLIST=example.com,git.example.org`
- Paket kurulumu `orhun.lock` günceller.

## 4. Derleme Metadata
- `orhun derle` artık:
  - `.obc`
  - `.exe`
  - `.obc.meta.json` üretir
- `.obc.meta.json` dosyasında payload boyutu ve CRC32 bulunur.
