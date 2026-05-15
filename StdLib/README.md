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
yazdır temel.ilk([1, 2, 3])

sonuc_yardimci olsun dahil_et "orhun/sonuc.oh"
yazdır sonuc_yardimci.deger_yada(sonuc_yardimci.ok(42), 0)
```

Module lookup checks the requested path first, then searches the standard
library roots. `ORHUN_STDLIB_PATH` can be used to add custom roots; otherwise
the local `StdLib/` directory is used during development.
