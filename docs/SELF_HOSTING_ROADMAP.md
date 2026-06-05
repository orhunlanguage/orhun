# Orhun Self-Hosting Roadmap

Orhun'un uzun vadeli hedefi, C++ ile baslayan derleyici/runtime cekirdegini
asama asama Orhun kaynaklariyla ifade edilebilir hale getirmektir.

Bu hedef "hicbir teknolojiye fiziksel olarak dokunmamak" anlamina gelmez.
Her dil en altta isletim sistemi, donanim ABI'si ve bir ilk derleme zinciriyle
baslar. Orhun icin asil bagimsizlik hedefi sudur:

- Orhun programlari calismak icin Python, JavaScript ya da baska bir dil runtime'i
  gerektirmeyecek.
- Orhun derleyicisinin buyuyen bolumu Orhun ile yazilacak.
- C++ cekirdek zamanla bootstrap katmanina cekilecek.
- Son kullanici icin tek arac `orhun` olacak.

## Faz 0: C++ Cekirdegi Saglamlastirma

Durum: aktif.

Hedefler:

- VM varsayilan yol olarak guvenilir kalacak.
- Interpreter fallback stable kanalda kapali kalacak.
- Test runner her vaka icin timeout uygulayacak.
- `ust`, varsayilan arguman, miras, hata modeli ve paket guvenligi gibi
  semantik noktalar kilitlenecek.
- Dil davranisi dokumanla sabitlenecek: soz dizimi, bytecode semantigi,
  standart kutuphane sozlesmesi.

Basari olcutu:

- Full test paketi takilmadan biter.
- `orhun doctor --json` stable kanalda makine-okur saglik raporu verir.
- Yeni ozellikler icin once test, sonra uygulama akisi korunur.

## Faz 1: Orhun Ile Standart Kutuphane

Hedefler:

- Saf Orhun ile yazilmis `StdLib/orhun/` modulleri baslar.
- `sonuc`, metin yardimcilari, koleksiyon yardimcilari, paket manifest okuma
  gibi guvenli alanlar Orhun koduna tasinir.
- C++ yerlesikleri sadece sistem siniri, dosya, FFI, ag ve VM primitive'leri
  gibi zorunlu noktalarda kalir.

Basari olcutu:

- Orhun kaynakli stdlib modulleri test paketinde C++ yerlesikleriyle birlikte
  calisir.
- Paket yoneticisi Orhun modullerini cozebilir.

## Faz 2: Orhun Ile Lexer ve Parser

Durum: aktif; lexer prototipi `orhun/lexer.oh` 0.2.0 ile `hata_sayisi` ve
`tokenlar` payload'i ureten `ozetle` giris noktasini sagliyor. Lexer parity
7 basarili fixture, 3 hata fixture ve genis `tests/cases` token sweep
seviyesine tasindi; non-ASCII fixture'lar UTF-8 sutun parity tamamlanana kadar
`tokens-only` politikasi ile korunuyor. Parser prototipi 142 basarili AST
fixture ve 63 hata fixture seviyesine tasindi.
Recursive block summary parity ve recursive expression child parity basladi.
`orhun/parser.oh` 0.3.48 atama `bildirim` ve hedef
ozetlerini, coklu atama hedeflerini ve hedef sayisini, islev basligi parametre/varsayilan
sayilarini ve varsayilan arguman ozetlerini, islev/sinif/dis
islev/dahil_et/deneme-yakala baslik metadatasini, dis islev tip sayisini,
eger/surece kosul
ozetlerini, sinif ebeveyn varligini, blok sayisini, tekrarla sayi ozetlerini,
anonim islev parametre ve varsayilan arguman ozetlerini, inline anonim islev
govde ifadesini, liste uretec degiskenini ve kosul varligini, sozluk
anahtarlarini, liste/sozluk oge sayilarini, dilim erisim sinir varligini,
paralel yap govde komut sayisini, liste/sozluk literal postfix ozetlerini,
alan/ust erisim adlarini, islev cagri adlarini, yeni nesne sinif adlarini ve
arguman sayilarini C++ AST ile
karsilastiriyor.

Hedefler:

- Orhun kaynakli lexer prototipi yazilir.
- Orhun kaynakli parser prototipi AST veya ara temsil uretir.
- C++ lexer/parser ile Orhun lexer/parser ciktisi ayni testlerde
  karsilastirilir.
- C++ lexer token akisi `orhun lex --json` ile makine-okur hale gelir.
- C++ parser AST akisi `orhun parse --json` ile makine-okur hale gelir.
- Lexer parity fixture'lari `tests/lexer_parity/` altinda genisler.
- Parser AST fixture'lari `tests/ast_json/` altinda genisler.
- Orhun parser prototipi once ust seviye komut turlerinde C++ AST ile
  eslenir, sonra blok komut ozetlerine, ana ifade cocuklarina ve daha derin
  blok/ifade agaci detaylarina genisler.
- Hata fixture'lari satir, beklenen-token ipucu, taninmayan komut ve komut
  onerisi seviyesinde C++ parser ile eslenir.

Basari olcutu:

- En az 100 dil fixture'i iki parser yolunda ayni ara temsili uretir.
- Hata mesajlari Turkce ve ogretici kalir.
- Yerel gelistirmede `python tests/roadmap_smoke.py ./build/orhun_test`
  self-hosting, fixture, surum, lambda capture ve closure regresyon kontrollerini
  birlikte gecirir.

## Faz 3: Orhun Ile Bytecode Derleyici

Hedefler:

- Orhun kaynakli derleyici bytecode uretmeye baslar.
- C++ VM ayni bytecode'u calistirir.
- C++ compiler ve Orhun compiler ciktisi fixture bazinda karsilastirilir.

Basari olcutu:

- Temel dil, fonksiyonlar, listeler, sozlukler, siniflar ve hata modeli icin
  bytecode parity saglanir.

## Faz 4: Kendi Kendini Derleyen Orhun

Hedefler:

- Orhun compiler kaynagi Orhun ile derlenebilir.
- Release surecinde C++ bootstrap sadece ilk araci uretir.
- Sonraki asamada Orhun compiler kendi yeni surumunu uretebilir.

Basari olcutu:

- `orhun derle compiler.oh` calisir.
- Uretilen derleyici ayni test paketini gecen bytecode veya native cikti uretir.

## Faz 5: AOT ve Native Cikti

Hedefler:

- VM varsayilan ve guvenilir yol olarak kalir.
- AOT backend opsiyonel hiz yolu olur.
- Once C veya LLVM IR gibi denetlenebilir bir ara cikti, sonra dogrudan native
  backend degerlendirilir.

Basari olcutu:

- CPU agir benchmarklarda VM'in ustune cikan AOT cikti alinabilir.
- AOT ciktisi Orhun'un guvenlik ve paket politikalarini bozmaz.

## Yakin Donem Sirasi

1. Test runner timeout ve takilan OOP/super vakalarini bitir.
2. Repo hijyenini koru: build ve rapor dosyalari kaynak agacinda kalmasin.
3. Saf Orhun stdlib cekirdegini genislet: metin, koleksiyon ve paket manifest
   yardimcilarini Orhun kaynaklarina tasi.
4. Lexer prototipini Orhun ile C++ lexer parity hedefine dogru genislet.
5. Parser prototipi icin AST/ara temsil formatini sabitle.
