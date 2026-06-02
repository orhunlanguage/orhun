# Orhun Standard Library

`StdLib/` contains standard library pieces that ship with the Orhun runtime.

- `Sunucu.h` carries the native HTTP server boundary used by built-in modules.
- `orhun/` contains modules written in Orhun itself.

The Orhun-source modules are the first step toward self-hosting. They should use
ordinary `.oh` syntax, avoid privileged native behavior, and stay covered by the
fixture test suite.

Example:

```orhun
temel olsun dahil_et "orhun/temel.oh"
yazdır temel.ilk([1, 2, 3], 0)

sonuc_yardimci olsun dahil_et "orhun/sonuc.oh"
yazdır sonuc_yardimci.deger_yada(sonuc_yardimci.ok(42), 0)
yazdır sonuc_yardimci.dene(işlev(): 7).deger

koleksiyon olsun dahil_et "orhun/koleksiyon.oh"
yazdır koleksiyon.benzersiz([1, 2, 1, 3])

metin_yardimci olsun dahil_et "orhun/metin.oh"
yazdır metin_yardimci.kirp("  Orhun  ")

paket_yardimci olsun dahil_et "orhun/paket.oh"
yazdır paket_yardimci.coz_ve_dogrula("{\"ad\":\"ornek\",\"surum\":\"0.1.0\"}").ok

lexer olsun dahil_et "orhun/lexer.oh"
yazdır lexer.tokenlestir("yazdır \"Merhaba\"\n")[0].tur
```

Module lookup checks the requested path first, then searches the standard
library roots. `ORHUN_STDLIB_PATH` can be used to add custom roots; otherwise
the local `StdLib/` directory is used during development.
