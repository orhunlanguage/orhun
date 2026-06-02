# Orhun Language Specification

This document is the living language contract for Orhun. It describes the
behavior that the current runtime should preserve while the project moves from a
C++ bootstrap implementation toward self-hosting.

Status: draft, pre-1.0.

## Design Goals

Orhun is designed to be:

- Turkish-first by default.
- Readable for beginners without hiding professional language features.
- Runtime-independent from Python, JavaScript, or other high-level language
  runtimes.
- VM-first, with a path toward self-hosting and optional native output later.
- Strict about safety-sensitive areas such as shell execution, FFI, and package
  verification.

## Source Files

- Orhun source files use the `.oh` extension.
- Source text is UTF-8.
- Turkish characters are valid in keywords and identifiers.
- Newlines are significant.
- Indentation creates blocks.
- `#` starts a line comment that continues until the end of the line.

Example:

```orhun
# Bu bir yorumdur.
ad olsun "Orhun"
yazdır ad
```

## Keywords

Current Turkish keywords:

```text
yazdır olsun eğer ise değilse doğru yanlış tekrarla kez sor
işlev dış_işlev döndür dahil_et sürece eşit eşit_değil büyük küçük
ve veya değil tip yeni benim deneme yakala kır devam ust için içinde
paralel yap
```

Compatibility alias:

```text
dis_islev
```

In Turkish strict mode, compatibility aliases are rejected.

Strict mode is enabled with:

```bash
orhun --turkce-kati dosya.oh
```

or:

```bash
ORHUN_TURKCE_KATI=1 orhun dosya.oh
```

## Identifiers

Identifiers may start with:

- `_`
- ASCII letters
- Turkish letters: `ç Ç ğ Ğ ı I İ ö Ö ş Ş ü Ü`

After the first character, digits are also allowed.

Examples:

```orhun
öğrenciSayısı olsun 42
sınıfAdı olsun "Matematik"
```

## Values

The current runtime value model contains:

- `bos`
- numbers
- booleans
- strings
- lists
- dictionaries
- functions
- native functions
- classes
- instances
- bound methods

Boolean literals:

```orhun
doğru
yanlış
```

## Truthiness

Values considered false:

- `bos`
- `yanlış`
- number `0`
- empty string
- empty list
- empty dictionary

All other values are considered true.

## Variables And Assignment

Variable definition and reassignment use `olsun`.

```orhun
ad olsun "Orhun"
puan olsun 100
puan olsun puan + 1
```

`=` is supported as assignment compatibility syntax in current builds. Inside
functions, `=` updates an existing local binding when one exists; otherwise it
writes the global binding. Use `olsun` to create or update the current
function's local binding.

Multiple assignment and destructuring are supported by the current test suite.

```orhun
a, b olsun [1, 2]
```

## Printing

`yazdır` evaluates an expression and writes its text form. `yaz` is the short
beginner-friendly command alias; it behaves the same as `yazdır` without making
`yaz` a reserved word, so `yaz olsun "değer"` is still a normal assignment.

```orhun
yaz "Merhaba"
yazdır "Merhaba"
yazdır 40 + 2
```

Current display behavior:

- `bos` prints as `bos`.
- Booleans print as `1` and `0` for interpreter/VM parity.
- Lists and dictionaries print in structural form.

## Input

`sor` prompts the user and returns one input line. `oku` is the beginner-friendly
function alias for the same behavior.

```orhun
ad olsun oku("Adın? ")
yaz "Merhaba, " + ad
```

## Ranges

`aralik` creates a list of numbers without requiring a module import. The
Turkish spelling `aralık` is also available. It accepts one, two, or three
arguments:

```orhun
yaz aralik(5)          # [0, 1, 2, 3, 4]
yaz aralik(1, 5)       # [1, 2, 3, 4]
yaz aralık(5, 1, -2)   # [5, 3]
```

An `adim` value of `0` returns an empty list, matching the current
`orhun/temel.oh` helper behavior.

## Control Flow

### If

```orhun
eğer puan büyük 50 ise:
    yazdır "geçti"
değilse:
    yazdır "kaldı"
```

### Repeat

```orhun
tekrarla 3 kez:
    yazdır "Orhun"
```

The colon after `kez` is optional for compatibility with existing fixtures.

### While

```orhun
i olsun 0
sürece i küçük 3:
    yazdır i
    i olsun i + 1
```

### Loop Control

```orhun
kır
devam
```

`kır` exits the nearest loop. `devam` continues the nearest loop.

## Functions

Functions use `işlev`.

```orhun
işlev topla(a, b):
    döndür a + b

yazdır topla(2, 3)
```

Default arguments are supported.

```orhun
işlev selam(ad olsun "dünya"):
    döndür "Merhaba, " + ad
```

Required parameters may not appear after default-valued parameters.

Anonymous functions are supported by the current parser/runtime.

```orhun
iki_kat olsun işlev(x): x * 2
yazdır iki_kat(4)
```

## Collections

Lists:

```orhun
sayılar olsun [1, 2, 3]
yazdır sayılar[0]
```

Dictionaries:

```orhun
kullanıcı olsun {"ad": "Ali", "yaş": 28}
yazdır kullanıcı.ad
yazdır kullanıcı["ad"]
```

List comprehensions are supported.

```orhun
sonuç olsun [x * 2 için x içinde [1, 2, 3]]
```

## Classes

Classes use `tip`.

```orhun
tip Selamlayici:
    işlev selam(ad olsun "dünya"):
        döndür "Merhaba, " + ad

s olsun yeni Selamlayici()
yazdır s.selam()
```

Inheritance:

```orhun
tip A:
    işlev değer():
        döndür 1

tip B(A):
    işlev değer():
        döndür ust.değer() + 1
```

Inside methods:

- `benim` refers to the receiver.
- `ust` refers to the parent method context.

For inherited method calls, `ust` must resolve relative to the class that owns
the currently executing method, not merely the runtime class of the instance.

## Error Handling

Runtime errors can be handled with `deneme` / `yakala`.

```orhun
deneme:
    yazdır 42?.ad
yakala hata:
    yazdır hata
```

The standard result-value helper is exposed as `sonuc`.

```orhun
başarılı olsun sonuc.ok(42)
hatalı olsun sonuc.hata("olmadı")
```

## Safe Access

Safe access uses `?.`.

```orhun
kullanıcı olsun {"ad": "Ali"}
yazdır kullanıcı?.ad
yazdır kullanıcı?.profil?.yaş
```

If the left side is `bos` or missing in a safe chain, the result is `bos`.
Invalid non-object access still produces a runtime error.

## Modules And Includes

`dahil_et` loads another Orhun file.

```orhun
matematik olsun dahil_et "matematik.oh"
```

Lookup order:

1. The requested path exactly as written.
2. Each root in `ORHUN_STDLIB_PATH`.
3. The local development standard library roots `StdLib/` and `stdlib/`.

Official Orhun-source standard modules live under `StdLib/orhun/` and are
included by their library-relative path:

```orhun
temel olsun dahil_et "orhun/temel.oh"
sonuc_yardimci olsun dahil_et "orhun/sonuc.oh"
koleksiyon olsun dahil_et "orhun/koleksiyon.oh"
metin_yardimci olsun dahil_et "orhun/metin.oh"
paket_yardimci olsun dahil_et "orhun/paket.oh"
lexer olsun dahil_et "orhun/lexer.oh"
```

The public package and module system is still evolving. Pre-1.0 code should keep
module behavior covered by tests.

## Concurrency

Task primitives are available through `gorev`.

`paralel yap` is the language-level syntax for starting supported task plans.

```orhun
g1 olsun paralel yap:
    bekle(0)
    bekle(0)

g2 olsun paralel yap: bekle(0)
sonuçlar olsun gorev.hepsi_bekle([g1, g2])
```

The current `paralel yap` implementation is intentionally narrow and should be
expanded only with tests.

## Standard Modules

Current built-in module surfaces include:

- `dosya`
- `internet`
- `json`
- `metin`
- `regex`
- `tarih`
- `veritabani`
- `sunucu`
- `sistem`
- `sonuc`
- `gorev`
- `ffi`
- `orhun/temel.oh`
- `orhun/sonuc.oh`
- `orhun/koleksiyon.oh`
- `orhun/metin.oh`
- `orhun/paket.oh`
- `orhun/lexer.oh`
- `orhun/parser.oh`

Safety-sensitive modules must keep policy checks enabled by default.

## Orhun-Source Lexer Prototype

`orhun/lexer.oh` exposes the first self-hosting lexer prototype. Its public
entry point is:

```orhun
lexer olsun dahil_et "orhun/lexer.oh"
tokenlar olsun lexer.tokenlestir("yazdır \"Merhaba\"\n")
```

Each token is a dictionary with `tur`, `deger`, `satir`, and `sutun` fields.
The current prototype recognizes keywords, identifiers, numbers, decimals,
strings, one-character operators, indentation, LF newlines, end-of-file, and
error tokens. It is a parity target for the C++ lexer, not yet the production
lexer.

Lexer parity fixtures live in `tests/lexer_parity/` and are compared against the
C++ lexer through `tests/lexer_parity_smoke.py`. The same smoke can sweep the
runtime case suite with `--fixtures tests/cases --tokens-only` to guard token
type/value parity across broader language examples.

## Orhun-Source Parser Prototype

`orhun/parser.oh` exposes the first self-hosting parser prototype. Its public
entry point is:

```orhun
parser olsun dahil_et "orhun/parser.oh"
sonuc olsun parser.ozetle("yazdır \"Merhaba\"\n")
```

The current prototype summarizes command node kinds, line numbers, primary
expression summaries (`tur`, `op`, `ayrinti`, `altlar`), recursive expression
children, assignment metadata, child block command counts, and recursive child
block command summaries, then compares them against the C++ parser AST through
`tests/parser_prototype_smoke.py`. Current coverage includes 139 successful AST
fixtures and 62 parser error fixtures. Command metadata covers declaration
assignment forms, assignment targets, multiple-assignment targets,
function/class/external-function headers, includes, and try/catch error
variables. Expression metadata covers anonymous function parameters/defaults,
inline anonymous function body summaries, list-comprehension variables,
dictionary literal keys, slice-bound presence, and `paralel yap` body command
counts. Error parity covers missing `ise`,
missing `kez`, missing control-flow conditions/counts,
missing assignment/return expression operands, required header names/colons,
malformed external-function, `deneme/yakala`, anonymous-function, `yeni`,
postfix, safe-access, collection/list-comprehension, and `paralel yap`
expressions, unknown command typos such as `yzdır 1`,
non-trailing required parameters after default values, and multiline dictionary
key errors. Those error fixtures also compare the reported line, expected-token
hint, unknown command name, and typo suggestion against the C++ parser. It is
not yet the production parser.

## CLI Contract

Important commands:

```bash
orhun dosya.oh
orhun yorumla dosya.oh
orhun vm-kati dosya.oh
orhun doctor
orhun doctor --json
orhun fmt dosya.oh
orhun lint dosya.oh
orhun lex dosya.oh --json
orhun parse dosya.oh --json
orhun hiz dosya.oh --json
orhun lsp --stdio
```

Stable channel defaults:

- VM fallback is off.
- Shell command execution is restricted.
- FFI defaults to allowlist policy.
- Package sources are allowlist-checked.
- `lex --json` exposes the C++ lexer token stream for self-hosting parity
  checks. Its JSON payload contains `dosya`, `hata_sayisi`, and `tokenlar`.
- `parse --json` exposes the C++ parser AST for self-hosting parity checks.
  Its JSON payload contains `dosya`, `durum`, `hata_sayisi`, and `ast`.
  The `ast` root is a `Program` node with nested command and expression nodes.
  `Atama` and `CokluAtama` nodes include `bildirim`: `true` for the `olsun`
  form and `false` for `=` compatibility assignment.
  Parser AST JSON fixtures live in `tests/ast_json/` and are checked through
  `tests/ast_json_smoke.py`.

## Compatibility Rules

Until `1.0`, Orhun may change quickly, but changes should follow these rules:

- Turkish-first keywords and diagnostics stay central.
- Existing passing fixtures should remain valid unless there is a documented
  migration.
- Stable channel changes should be additive whenever possible.
- Safety defaults should not be weakened.
- Any behavior needed for self-hosting must be documented here before it is
  depended on by Orhun-written tooling.

## Closure Capture Status

Nested functions and anonymous functions are parsed and callable. The
interpreter (`orhun yorumla`), default runner, and `vm-kati` keep returned
closures' captured local variables alive. The promoted closure fixture is
`tests/cases/closure_missing_feature.oh`, guarded by
`tests/known_gap_smoke.py` and `tests/interpreter_closure_smoke.py`.

The intended capture model is documented in `docs/CLOSURE_CAPTURE_PLAN.md`.

Target closure semantics:

- Nested named functions and anonymous functions use lexical scope.
- A name declared as a parameter or local in the current function shadows outer
  bindings with the same name.
- A referenced name that is not local to the nested function is captured from
  the nearest enclosing function scope when such a binding exists.
- Global bindings are not copied into closure environments; they remain global
  lookups.
- Captured locals live as long as any closure that references them.
- Multiple closures created during the same outer function call share the same
  mutable capture cell for each captured binding.
- Separate calls to the same outer function create separate capture cells.
- A new local binding created inside a loop iteration should have an independent
  capture cell for closures produced in that iteration.
- The interpreter and VM must agree before closure capture is considered part of
  the stable language contract.

## Self-Hosting Implications

This specification is the baseline for future Orhun-written components:

1. Standard library modules written in Orhun.
2. Lexer prototype written in Orhun.
3. Parser prototype written in Orhun.
4. Bytecode compiler written in Orhun.
5. Orhun compiler capable of compiling itself.

When implementation behavior and this document diverge, either the implementation
or the specification must be updated in the same development cycle.
