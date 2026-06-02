# Orhun VS Code Eklentisi (Taslak)

Bu klasor Orhun dili icin temel VS Code entegrasyon taslagini icerir.

## Ozellikler

- `.oh` dosya uzantisini tanir
- Temel sozdizimi renklendirme (anahtar kelime, yorum, metin, sayi)
- Girinti tabanli dil icin temel editor ayarlari
- Baslangic dostu snippet'lar (`yaz`, `oku`, `eger`, `islev`, `aralik`)

## Snippet'lar

Editor icinde bu kisaltmalar kullanilabilir:

- `yaz`: ekrana metin yazdirma
- `oku`: kullanicidan satir okuma
- `eger`: kosul blogu
- `islev`: islev tanimi
- `aralik`: basit aralik dongusu

## LSP Baglantisi

Orhun calistiricisi icinde resmi komut:

```bash
orhun lsp --stdio
```

Workspace koku belirtmek icin:

```bash
orhun lsp --stdio --workspace-root <proje_klasoru>
```

Bu komut uzerinden VS Code tarafinda Language Server baglantisi kurulabilir.

## Paketleme (VSIX)

```bash
npm install
npx vsce package
```

Olusan `.vsix` dosyasi VS Code'a kurulabilir.
