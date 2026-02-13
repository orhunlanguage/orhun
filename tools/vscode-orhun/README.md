# Orhun VS Code Eklentisi (Taslak)

Bu klasor Orhun dili icin temel VS Code entegrasyon taslagini icerir.

## Ozellikler

- `.oh` dosya uzantisini tanir
- Temel sozdizimi renklendirme (anahtar kelime, yorum, metin, sayi)
- Girinti tabanli dil icin temel editor ayarlari

## LSP Baglantisi

Orhun calistiricisi icinde `lsp` komutu bulunur:

```bash
orhun lsp
```

Bu komut uzerinden VS Code tarafinda Language Server baglantisi kurulabilir.

## Paketleme (VSIX)

```bash
npm install
npx vsce package
```

Olusan `.vsix` dosyasi VS Code'a kurulabilir.
