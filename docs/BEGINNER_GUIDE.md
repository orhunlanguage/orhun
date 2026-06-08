# Orhun Baslangic Rehberi

Bu rehber Orhun'u ilk kez deneyen biri icin kisa bir yol haritasidir.
Orhun henuz deneysel bir dildir, ama temel kod yazma akisi bugunden
denenebilir.

## 1. Merhaba Yaz

```orhun
yaz "Merhaba, Orhun!"
```

`yaz`, `yazdır` komutunun kisa ve baslangic dostu adidir.

## 2. Degisken Kullan

```orhun
ad olsun "Ada"
yas olsun 18

yaz "Merhaba, " + ad
yaz yas
```

Orhun'da temel atama icin `olsun` kullanilir.

## 3. Girdi Al

```orhun
ad olsun oku("Adin? ")
yaz "Merhaba, " + ad
```

`oku`, kullanicidan bir satir okur.

## 4. Kosul Yaz

```orhun
puan olsun 70

eğer puan büyük 50 ise:
    yaz "Gectin"
değilse:
    yaz "Kaldin"
```

Karsilastirma icin Turkce operatorler de vardir: `büyük`, `küçük`, `eşit`.

## 5. Listeyle Calis

```orhun
sayilar olsun aralik(1, 6)

yaz sayilar
yaz ilk(sayilar)
yaz son(sayilar)
yaz dolu_mu(sayilar)
```

`aralik(1, 6)` sonucu `[1, 2, 3, 4, 5]` olur.

## 6. Islev Yaz

```orhun
işlev selam(ad olsun "dünya"):
    döndür "Merhaba, " + ad

yaz selam()
yaz selam("Orhun")
```

Varsayilan argumanlar desteklenir.

## 7. Koleksiyon Yardimcilari

```orhun
koleksiyon olsun dahil_et "orhun/koleksiyon.oh"

yaz koleksiyon.numaralandir([5, 6], 1)
yaz koleksiyon.eslestir([1, 2], [7, 8, 9])
```

`numaralandir`, Python'daki `enumerate` fikrine benzer. `eslestir`, iki listeyi
kisa olan liste bitene kadar ciftler.

## 8. Dosyayi Calistir

Bir dosya olustur:

```text
ilk.oh
```

Icine Orhun kodu yaz ve calistir:

```bash
orhun ilk.oh
```

Gelisim sirasinda kati VM yolunu da deneyebilirsin:

```bash
orhun vm-kati ilk.oh
```

## 9. Program Argumanlarini Oku

```orhun
yaz sistem.argumanlar
```

Programa arguman vermek icin kaynak dosyasindan sonra yaz:

```bash
orhun ilk.oh Yusuf --hizli
```

Bu durumda `sistem.argumanlar`, `["Yusuf", "--hizli"]` olur.

## Sonraki Adim

- `examples/` klasorundeki ornekleri calistir.
- `docs/SPEC.md` ile dil sozlesmesini oku.
- VS Code eklentisi icin `tools/vscode-orhun/` klasorune bak.
