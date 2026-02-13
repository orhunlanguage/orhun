# Orhun LSP (MVP)

Bu klasör Orhun için minimal Language Server içerir:

- Dosya: `orhun_lsp.py`
- Özellikler:
  - `initialize` / `shutdown` / `exit`
  - `textDocument/completion` (anahtar kelime tamamlama)
  - `didOpen` / `didChange` için temel diagnostics
    - tab karakteri uyarısı
    - satır sonu boşluk uyarısı
    - 4'ün katı olmayan girinti uyarısı

## Çalıştırma

```bash
python tools/lsp/orhun_lsp.py
```

VS Code tarafında bu process bir language client ile bağlanmalıdır.
Bu sürüm MVP'dir; parse tabanlı diagnostics ve go-to-definition sonraki adımdır.
