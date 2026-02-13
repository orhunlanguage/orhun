# Orhun Yol Haritasi (v1.1 -> v2.0)

Bu belge, "en kolay + en guclu" hedefi icin kalan buyuk isleri siralar.

## 1) Kalite ve Guvenilirlik
- [x] Otomatik test altyapisi (deterministik senaryolar)
- [x] GitHub Actions CI
- [ ] Parser/Lexer/Interpreter icin daha genis regression test paketi (100+)
- [ ] Fuzz test (Lexer/Parser)
- [ ] Performans benchmark seti

## 2) Gelistirici Deneyimi
- [ ] Orhun formatter (`orhun fmt`)
- [x] Orhun linter (`orhun lint`)
- [x] VS Code extension (syntax highlight + snippets)
- [ ] LSP (completion, go-to-definition, diagnostics)

## 3) Ekosistem
- [x] Paket yoneticisi (`orhun paket ekle ...`)
- [ ] Merkezi paket deposu taslagi
- [x] Standart kutuphane genisletme (http/json/sunucu/veritabani/regex/date)
- [ ] Dokumantasyon sitesi + 30 dk hizli baslangic

## 4) Calisma Zamani Mimarisi
- [ ] Bytecode tasarimi
- [ ] VM prototipi
- [ ] Interpreter -> VM uyumluluk katmani

## 5) Uyum ve Entegrasyon
- [ ] C API / FFI
- [ ] Python kutuphaneleriyle kopru
- [ ] Tek dosya paketleme

## Not
Tamamı tek iterasyonda guvenli sekilde bitirilemez. Oncelik:
1. Guvenilirlik,
2. Gelistirici deneyimi,
3. Ekosistem,
4. Performans/VM.
