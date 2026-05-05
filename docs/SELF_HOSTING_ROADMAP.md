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

Hedefler:

- Orhun kaynakli lexer prototipi yazilir.
- Orhun kaynakli parser prototipi AST veya ara temsil uretir.
- C++ lexer/parser ile Orhun lexer/parser ciktisi ayni testlerde
  karsilastirilir.

Basari olcutu:

- En az 50 dil fixture'i iki parser yolunda ayni ara temsili uretir.
- Hata mesajlari Turkce ve ogretici kalir.

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
3. Saf Orhun stdlib cekirdegini genislet: `sonuc`, metin ve koleksiyon
   yardimcilarini Orhun kaynaklarina tasi.
4. Lexer prototipini Orhun ile yazmaya basla.
5. Parser prototipi icin AST/ara temsil formatini sabitle.
