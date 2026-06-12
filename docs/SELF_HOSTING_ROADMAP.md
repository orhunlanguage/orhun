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

Durum: aktif; lexer prototipi `orhun/lexer.oh` 0.3.0 ile `hata_sayisi`,
`token_sayisi` ve `tokenlar` payload'i ureten `ozetle` giris noktasini sagliyor. Lexer parity
7 basarili fixture, 3 hata fixture ve genis `tests/cases` token sweep
seviyesine tasindi; non-ASCII fixture'larda UTF-8 kod noktasi tabanli satir/sutun
parity saglandi. Parser prototipi 143 basarili AST
fixture ve 63 hata fixture seviyesine tasindi.
Recursive block summary parity ve recursive expression child parity basladi.
`orhun/parser.oh` 0.6.0 `Program` ve `Block` yapisal IR turlerini, parse sonuc
hata/token/komut sayisini ve komut turlerini, ifade satirlarini
ve alt ifade sayilarini, atama `bildirim` ve hedef
ozetlerini, coklu atama hedeflerini ve hedef sayisini, islev basligi parametre/varsayilan
sayilarini ve varsayilan arguman ozetlerini, islev/sinif/dis
islev/dahil_et/deneme-yakala baslik metadatasini, dis islev tip sayisini,
eger/surece kosul
ozetlerini, sinif ebeveyn varligini, blok sayisini, blok satirlarini ve blok
komut sayilarini, tekrarla sayi ozetlerini,
anonim islev parametre ve varsayilan arguman ozetlerini, inline anonim islev
govde ifadesini, liste uretec degiskenini ve kosul varligini, sozluk
anahtarlarini, liste/sozluk oge sayilarini, dilim erisim sinir varligini,
paralel yap govde komut sayisini ve yapisal komutlarini, liste/sozluk literal postfix ozetlerini,
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

Durum: aktif; C++ derleyici ciktisini artifact uretmeden cozumleyen
`orhun baytkod <dosya.oh> --json` parity yuzeyi ve sozlesme smoke testi
hazirlandi. `orhun/derleyici.oh` 0.25.0; sabitler, bicimlendirilmis metinler,
global kimlik okuma/atama,
temel ikili/tekli islemler, liste/sozluk literal'leri, basit global islev
cagrilari, indeks/alan/guvenli alan okumalari, `eger/degilse`, basit `surece`
ve `tekrarla` donguleri ve `yazdir` icin opcode, operand, IP, kaynak satiri ve
sabit havuzu parity'si uretiyor.
Zorunlu/varsayilan parametreli islev tanimlari, yerel tanim/okuma/yazma, acik/ortuk donus ve
`OP_ISLEV_OLUSTUR` local-ad metadata'si de ilk parity kapsaminda.
Dogudan literal sayi/metin/mantik islemleri ve tekli islemler C++ ile ayni
sabit katlama kurallarini uygular.
Sabit dogrulukla belirlenen `eger` dallari, `surece yanlis` ve sifir/negatif
`tekrarla` govdeleri bytecode uretmeden elenir.
Alan/indeks atamalari, dilim okumalari ve global/local coklu atamalar da
fixture parity kapsamindadir.
Noktali modul/islev cagrilari ile ifade ve komut bicimindeki `dahil_et`
derlemeleri de parity kapsamindadir.
`deneme/yakala` kontrol akisi ve global/local hata degiskeni baglama davranisi
da parity kapsamindadir.
`yeni` nesne olusturma ve ifade/komut bicimindeki `sor` cagrilari da parity
kapsamindadir.
Ic ice dongu baglamlari, `kir` atlamalari ve `devam` hedef/yama davranisi da
parity kapsamindadir.
Isimsiz islevler, ic ice isimli islevler, varsayilan argumanlar ve kararlı
`__anonim_islev_N` metadata adlariyla parity kapsamindadir. Dis yerel okuma ve
degistirme, C++ derleyicisiyle ayni isim tabanli opcode + VM yakalama
sozlesmesini uretir.
Filtreli/filtresiz liste uretecleri, kapasite ayirma optimizasyonu, gecici
degisken adlari ve islev-ici yerel metadata davranisiyla parity kapsamindadir.
Dis islev tanimlari ad, kutuphane, donus tipi ve parametre tipi listesiyle
mevcut FFI politika yuzeyine derlenir.
Alan tanimli siniflar, metodlar, temel kalitim kurulumu, `benim` alan
okuma/yazma ve `ust` metod cagrilari parity kapsamindadir. Metod metadata'si
baglam argumanlarini ve varsayilan parametre ofsetlerini C++ ile ayni uretir.
`paralel yap` adimlari yapisal parser IR'indan gorev plan sozluklerine
indirgenir ve parity kapsamindadir.
Compiler prototype smoke su anda 96 programda C++ bytecode ozetini birebir
eslestirir. Bu kapsam buyuk closure, OOP, varsayilan metod argumani ve
liste-ureteci/lambda/paralel-yap fixture'larini da dogrudan karsilastirir;
desteklenmeyen yapilar icin acik hata bekler.
Tum `tests/cases` derleyici sweep'i, C++ derleyicisinin kabul ettigi 131
programin tamaminda Orhun derleyicisinin bytecode ozetini birebir eslestirdigini
dogrular; C++ tarafindaki 3 bilincli hata fixture'i ayri izlenir.
`orhun baytkod-yurut <dosya.json>` koprusu, Orhun derleyicisinin cozumlenmis
bytecode ciktisini siki dogrulamadan sonra C++ VM'de calistirir. Bootstrap smoke
testi bicimlendirilmis metin, ic ice islev, OOP/ust ve hata yakalama programlarini
bu kopruyle yurutup dogrudan `vm-kati` ciktisiyla karsilastirir.
Deneysel `orhun orhun-vm <dosya.oh>` komutu ayni hatti ara JSON dosyasi olmadan
tek komutta calistirir.
Deneysel `orhun orhun-derle <dosya.oh> [cikti]` komutu Orhun derleyici yolundan
`.obc`, paketli calistirilabilir dosya ve metadata uretir; bootstrap testi C++
derleyici artifact'lariyla byte duzeyinde esitligi dogrular.

Hedefler:

- Orhun kaynakli derleyici bytecode uretmeye baslar.
- C++ VM ayni bytecode'u calistirir.
- C++ compiler ve Orhun compiler ciktisi fixture bazinda karsilastirilir.
- C++ compiler bytecode akisi `orhun baytkod --json` ile makine-okur kalir.

Basari olcutu:

- Temel dil, fonksiyonlar, listeler, sozlukler, siniflar ve hata modeli icin
  bytecode parity saglanir.

## Faz 4: Kendi Kendini Derleyen Orhun

Durum: basladi; `orhun orhun-derle StdLib/orhun/derleyici.oh <cikti>` calisir
ve C++ derleyicinin ayni kaynak icin urettigi `.obc` ile byte duzeyinde ayni
bootstrap artifact'ini uretir. Bu artifact henuz tek basina kaynak kabul eden
bagimsiz bir derleyici CLI'i degildir.
`ORHUN_MODULE_MODE=obc-only` ile onceden derlenmis Orhun
derleyici/parser/lexer modul zinciri `.oh` kaynaklari olmadan ve C++ kaynak
derleme fallback'i yapmadan calisir; eksik modul artifact'i acik hata verir.
`orhun-vm` ve `orhun-derle`, bu zinciri tek komutta secmek icin `--obc-only`
ve `--obc-first` CLI politikalarini destekler.
`orhun bootstrap-hazirla <dizin>`, lexer/parser/derleyici modulleri ile
derleyici CLI girisini kaynak dosyasi icermeyen bir toolchain klasorune ve CRC
tasiyan makine-okur manifeste donusturur.
`orhun bootstrap-dogrula <toolchain>`, manifest sozlesmesini, tam modul
listesini, payload boyut/CRC degerlerini ve OBC yapisini hedef calistirmadan
dogrular; derleme ve calistirma komutlari da once ayni denetimi yapar.
`orhun bootstrap-derle <toolchain> <kaynak.oh> [cikti]`, hazirlanan toolchain'i
ortam degiskeni gerektirmeden kati `obc-only` modunda kullanir.
`orhun bootstrap-calistir <toolchain> <kaynak.oh>`, ayni zincirle hedefi
derleyip VM'de calistirir.
`orhun bootstrap-derleyici-paketle <toolchain> <cikti-dizini>`, kardes
toolchain'ini farkli calisma klasorlerinden otomatik bulan ve dogrulayan,
kaynak-kodsuz tasinabilir `orhun-derleyici` calistirilabilir dosyasini uretir.
Bu ilk bagimsiz derleyici CLI'i bytecode JSON uretir; `--derle` modu ayni
Orhun-yazili compiler zinciriyle byte-duzeyinde esit `.obc`, paketli
calistirilabilir dosya ve metadata artifact'larini dogrudan uretir. Artifact
isteginin kaynak/cikti argumanlarini ve `.obc`, paketli calistirilabilir,
metadata yollarindan olusan tam cikti planini artik Orhun-yazili
`derleyici_cli.oh` cozer. Plan `orhun-artifact-plan-v1` sozlesmesiyle
surumlenir. C++ cekirdegi plani bilinmeyen sozlesme, bos alan, beklenmeyen
uzanti, kaynak adinda yol ayirici ve cakisan cikti yollarina karsi
dogruladiktan sonra yalniz OBC/paket serilestirme ve dosya yazma koprusu olarak
kalir. Artifact'lar once hedeflerinin yanindaki benzersiz gecici dosyalara
yazilir; tumu hazirlanmadan yayinlanmaz ve hazirlama hatasi mevcut ciktilari
korur. Paketli C++ host `--derle` veya `--compile` komut adlarini bilmez; her
cagrinin yapilandirilmis cikis kodu ve artifact plani Orhun CLI bytecode'u
tarafindan uretilir. Compiler bundle kimligi dosya adina degil, dogrulanan
bundle manifestine, embedded CLI payload boyut/CRC degerine ve kardes toolchain
bagina dayanir.
`orhun bootstrap-yeniden-uret <tohum-toolchain> <cikti-dizini>`, tohum ile
asama 2'yi, asama 2 ile asama 3'u uretir ve son iki asamadaki dort artifact'in
byte duzeyinde ayni olmasini zorunlu tutar. Dolu cikti dizinini ezmez ve
basarili kapinin sonucunu `bootstrap-rebuild.manifest.json` ile kaydeder.
`sistem.argumanlar`, dogrudan, paketli ve bootstrap calistirma yollarinda ayni
program argumani sozlesmesini saglar; bagimsiz derleyici CLI'i bu primitive ile
kaynak/cikti yollarini okuyup tam artifact planini Orhun kodunda uretir.

Hedefler:

- Orhun compiler kaynagi Orhun ile derlenebilir.
- Release surecinde C++ bootstrap sadece ilk araci uretir.
- Sonraki asamada Orhun compiler kendi yeni surumunu uretebilir.
- CI tarafinda dogrulanan tasinabilir compiler bundle'lari surumlu, SHA-256
  dogrulamali ve GitHub/Sigstore build-provenance attestation'li release
  asset'lerine donusturulur; platform code-signing ayri bir katman olur.

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
