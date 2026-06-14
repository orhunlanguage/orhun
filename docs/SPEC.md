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

## Operators

Arithmetic operators are `+`, `-`, `*`, `/`, and `%`. Modulo uses the same
zero-divisor error contract in the interpreter and VM. `ve` and `veya` are
short-circuit boolean operators in both runtimes. They return normalized boolean
results and do not evaluate the right operand when the left operand determines
the result.

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

`listeye_ekle(liste, deger)` mutates the supplied list in both the interpreter
and VM. The equivalent method form is `liste.ekle(deger)`.

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

Function, method, and constructor calls may span multiple lines. Newlines and
indentation inside the argument list are layout only, and a trailing comma
before `)` is allowed. Parenthesized expressions may also span multiple lines.

```orhun
sonuc olsun topla(
    2,
    3,
)
```

Default arguments are supported.

```orhun
işlev selam(ad olsun "dünya"):
    döndür "Merhaba, " + ad
```

Required parameters may not appear after default-valued parameters.

Named functions, methods, anonymous functions, and external-function
declarations may split their parameter lists across multiple lines. A trailing
comma before `)` is allowed, and a default value may begin on the next line.

```orhun
işlev selamla(
    ad,
    selam olsun
        "Merhaba",
):
    yazdır selam + ", " + ad
```

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
yaz ilk(sayılar)
yaz son(sayılar)
yaz dolu_mu(sayılar)
```

Dictionaries:

```orhun
kullanıcı olsun {"ad": "Ali", "yaş": 28}
yazdır kullanıcı.ad
yazdır kullanıcı["ad"]
```

List and dictionary literals may span multiple lines. Newlines and indentation
inside the literal are layout only, and a trailing comma before `]` or `}` is
allowed.

```orhun
ayarlar olsun {
    "ad": "orhun",
    "portlar": [
        8080,
        8081,
    ],
}
```

Index and slice access may also span multiple lines.

```orhun
orta olsun sayılar[
    1:
    3
]
```

Inside an open `()`, `[]`, or `{}` delimiter, an expression may continue on
the next line after a binary or unary operator. Outside delimiters, a newline
still ends the expression.

```orhun
toplam olsun (
    10 +
    20 *
    2
)
```

Postfix chains may also continue on the next line inside an open delimiter.
This includes field access, safe field access, calls, indexes, and slices.

```orhun
deger olsun (
    kutu
    .oku(
    )
)
```

List comprehensions are supported.

```orhun
sonuç olsun [x * 2 için x içinde [1, 2, 3]]
```

Collection helpers:

- `ilk(liste, [yedek])` returns the first item. Empty lists require `yedek`.
- `son(liste, [yedek])` returns the last item. Empty lists require `yedek`.
- `bos_mu(deger)` / `boş_mu(deger)` checks whether a string, list, or
  dictionary is empty.
- `dolu_mu(deger)` checks whether a string, list, or dictionary has at least
  one item.

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

`orhun/koleksiyon.oh` includes beginner-friendly list helpers such as
`haritala`, `filtrele`, `katla`, `benzersiz`, `numaralandir`, and
`eslestir`/`eşleştir`. `numaralandir(liste, [baslangic])` returns
`[sira, deger]` pairs, and `eslestir(sol, sag)` returns pairs up to the
shorter list. `toplam(liste, [baslangic])` accumulates values with `+`.
`en_kucuk`/`en_küçük` and `en_buyuk`/`en_büyük` return their explicit fallback
value for an empty list.

`orhun/metin.oh` includes beginner-friendly string helpers such as `say`,
`on_eki_kaldir`/`ön_eki_kaldır`, and `son_eki_kaldir`/`son_eki_kaldır`.
`say` counts non-overlapping occurrences and returns zero for an empty search
string. Prefix and suffix removal return the original string when it does not
match.

`orhun/paket.oh` includes package manifest helpers such as `coz`,
`coz_ve_dogrula`, `dogrula`, `bagimliliklar`, `bagimli_mi`, and
`ad_gecerli_mi`. Manifest package names and dependency names should contain
only letters, digits, `_`, `.`, and `-`. Manifest versions are validated as
Semantic Versioning 2.0 versions by the Orhun-written `surum_gecerli_mi`
helper, including prerelease and build identifiers. `surum_ayristir` returns a
result record whose value separates a valid version into numeric `ana`, `yan`,
and `duzeltme` fields plus `on_surum` and `yapi` strings. Invalid versions
return an error result.

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
- `orhun/derleyici.oh`
- `orhun/derleyici_cli.oh`

Safety-sensitive modules must keep policy checks enabled by default.

`sistem.argumanlar` is a list containing only the arguments passed after the
program source or runtime command. The runtime executable and source path are
not included. Global Orhun options such as `--turkce-kati` are interpreted only
when they appear before the command/source, so the same text can still be a
program argument after it. Direct source execution, `vm`, `vm-kati`, `yorumla`,
`obc`, packaged executables, `orhun-vm -- <arguments>`, and
`bootstrap-calistir` expose the same argument list.

## Orhun-Source Lexer Prototype

`orhun/lexer.oh` exposes the first self-hosting lexer prototype. Its public
entry point is:

```orhun
lexer olsun dahil_et "orhun/lexer.oh"
sonuc olsun lexer.ozetle("yazdır \"Merhaba\"\n")
tokenlar olsun sonuc.tokenlar
```

Each token is a dictionary with `tur`, `deger`, `satir`, and `sutun` fields.
`ozetle(kaynak)` returns a dictionary with `hata_sayisi`, `token_sayisi`, and `tokenlar`,
matching the C++ `lex --json` health shape used by parity tests.
The current prototype recognizes keywords, identifiers, numbers, decimals,
strings, one-character operators, indentation, LF newlines, end-of-file, and
error tokens. It is a parity target for the C++ lexer, not yet the production
lexer.

Lexer parity fixtures live in `tests/lexer_parity/` and are compared against the
C++ lexer through `tests/lexer_parity_smoke.py`. The same smoke can sweep the
runtime case suite with `--fixtures tests/cases` to guard token type, value,
line, and column parity across broader language examples.
Fixtures that intentionally exercise lexer errors use `# parity: allow-errors`;
for those cases the smoke compares error counts and token shape while leaving
the exact diagnostic text free for clearer Turkish wording.
Fixtures with non-ASCII source text compare token values and character-based
line/column positions. The Orhun-source lexer keeps byte offsets internally but
uses UTF-8 code-point counts when advancing diagnostic columns.
The runtime primitive `metin.utf8_uzunluk(metin)` exposes the same code-point
counting rule to Orhun-source tooling.

## Orhun-Source Parser Prototype

`orhun/parser.oh` exposes the first self-hosting parser prototype. Its public
entry point is:

```orhun
parser olsun dahil_et "orhun/parser.oh"
sonuc olsun parser.ozetle("yazdır \"Merhaba\"\n")
```

The current prototype exposes a `Program` root and `Block` structural nodes,
then summarizes command node kinds, line numbers, primary
expression summaries (`tur`, `satir`, `op`, `ayrinti`, `alt_sayisi`, `altlar`), recursive expression
children, assignment metadata, total child-block counts, child block line
numbers and command counts, recursive child block command summaries, and result command kinds,
command/error and token counts, then compares them
against the C++ parser AST through
`tests/parser_prototype_smoke.py`. Current coverage includes 153 successful AST
fixtures and 63 parser error fixtures. Command metadata covers declaration
assignment forms, assignment targets, multiple-assignment targets/counts,
function/class/external-function headers, class parent presence,
parameter/default counts, external-function type counts, includes,
and try/catch error variables. Control-flow metadata covers `eğer`/`sürece`
condition summaries and `tekrarla` count summaries. Expression metadata covers
anonymous function parameter/default counts, parameters/defaults,
inline anonymous function body summaries,
list-comprehension variables and condition presence, list/dictionary item
counts, dictionary literal keys, slice-bound presence, and `paralel yap` body
command counts. Error parity covers missing `ise`,
missing `kez`, missing control-flow conditions/counts,
missing assignment/return expression operands, required header names/colons,
malformed external-function, `deneme/yakala`, anonymous-function, `yeni`,
postfix, safe-access, collection/list-comprehension, and `paralel yap`
expressions, unknown command typos such as `yzdır 1`, and non-trailing required
parameters after default values. Those error fixtures also compare the reported line, expected-token
hint, unknown command name, and typo suggestion against the C++ parser. It is
not yet the production parser.

## Orhun-Source Compiler Prototype

`orhun/derleyici.oh` is the first Orhun-written bytecode compiler prototype.
It consumes the structural IR from `orhun/parser.oh` and emits the same decoded
bytecode shape exposed by C++ `baytkod --json`.

The initial supported subset contains number, string, and boolean constants,
global identifier reads and assignments, basic binary and unary operations,
list and dictionary literals, simple global function calls, index access,
field and safe-field reads, `eğer/değilse`, basic `sürece` loops, and `yazdır`.
Counted `tekrarla ... kez` loops are also covered by compiler parity.
Functions with required/default parameters and local assignments, explicit/implicit
returns, and function-creation/local-name metadata are covered by the initial
function subset. Nested named functions and returned functions are also covered.
Direct literal number, string, boolean, and unary expressions follow the C++
compiler's initial constant-folding rules.
Constant-truthiness branches and loops follow the C++ compiler's initial
dead-code elimination behavior.
Field/index assignments, slice reads, and global/local multiple assignments are
covered by compiler parity.
Dotted module/function calls and expression/command forms of `dahil_et` are
covered by compiler parity.
`deneme/yakala` control flow and global/local caught-error bindings are covered
by compiler parity.
`yeni` object construction and expression/command forms of `sor` are covered by
compiler parity.
Nested loop contexts and `kır`/`devam` jump patching are covered by compiler
parity.
Anonymous functions and nested named functions are covered by compiler parity,
including default arguments, deterministic `__anonim_islev_N` metadata names,
and captured outer-local reads/writes through the same name-based bytecode and
VM capture-cell contract used by the C++ compiler.
Filtered and unfiltered list comprehensions are covered by compiler parity,
including deterministic temporary names, the unfiltered reserve optimization,
and function-local temporary metadata.
External function declarations are covered by compiler parity and lower to the
existing `ffi_dis_islev_tanimla` policy surface with their library, return type,
and parameter type list.
Classes containing field declarations, methods, and inheritance setup are
covered by compiler parity. Method metadata includes `benim`/`üst` context
arguments and default-parameter local offsets; field reads/writes and super
method calls are also covered.
Interpolated strings, escaped braces, and constant truthiness folding are
covered by compiler parity.
`paralel yap` expressions are covered by compiler parity. The Orhun parser IR
exposes their structural `paralel_komutlar`, which the compiler lowers to the
same task-plan dictionaries and `gorev.baslat_plan` call as the C++ compiler.
`tests/compiler_prototype_smoke.py` compares opcode names, instruction pointers,
source lines, operands, constant-pool entries, and aggregate counts against the
C++ compiler. It includes focused generated programs plus larger checked-in
closure, OOP, default-method, and list-comprehension fixtures. Unsupported
constructs return an explicit error instead of silently emitting incorrect
bytecode. It is not yet the production compiler.

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
orhun baytkod dosya.oh --json
orhun hiz dosya.oh --json
orhun lsp --stdio
```

The LSP completion provider returns language keywords plus common built-in
functions and modules such as `yaz`, `oku`, `aralik`, `ilk`, `son`, `json`,
`dosya`, and Orhun-source helpers such as `numaralandir` and `eslestir`.
Signature help also includes common built-in and Orhun-source helper
signatures, including `aralik([baslangic], bitis, [adim])` and
`numaralandir(liste, [baslangic])`.
Workspace file URIs use UTF-8 percent encoding for reserved and non-ASCII path
bytes. On Windows, indexed workspace files are reported with long paths rather
than legacy 8.3 short paths, and UNC paths retain their file-URI authority.

Stable channel defaults:

- VM fallback is off.
- Shell command execution is restricted.
- FFI defaults to allowlist policy.
- Package sources are allowlist-checked.
- `doctor --json` reports `version`, `build`, `commit`, `channel`,
  `fallback_default`, `fallback_source`, `ci_profiles`, `security_mode`,
  `checks`, and `status`.
- `lex --json` exposes the C++ lexer token stream for self-hosting parity
  checks. Its JSON payload contains `dosya`, `hata_sayisi`, and `tokenlar`.
- `parse --json` exposes the C++ parser AST for self-hosting parity checks.
  Its JSON payload contains `dosya`, `durum`, `hata_sayisi`, and `ast`.
  The `ast` root is a `Program` node with nested command and expression nodes.
  `Atama` and `CokluAtama` nodes include `bildirim`: `true` for the `olsun`
  form and `false` for `=` compatibility assignment.
  Orhun's parser prototype parity also checks field names through `alan`,
  super access method names through `metod`, function-call names through `ad`,
  new-object class names through `sinif`, and argument counts through
  `arguman_sayisi`.
  Parser AST JSON fixtures live in `tests/ast_json/` and are checked through
  `tests/ast_json_smoke.py`.
- `baytkod --json` exposes the C++ compiler output as a decoded, machine-readable
  bytecode contract for self-hosting parity checks. Its successful payload
  contains bytecode size, instruction and constant counts, decoded instructions,
  source lines, operands, and primitive constants. `OP_ISLEV_OLUSTUR`
  instructions also expose function name/constant, arity, entry IP, local count,
  context argument count, and local-name mappings. Compiler errors return exit
  code `1`, set `durum` to `fail`, and leave `bytecode` as `null`. The English
  `bytecode` command is a compatibility alias. This is an inspection and parity
  surface; it does not write build artifacts.
- `baytkod-yurut <dosya.json>` validates and executes the decoded bytecode
  contract through the C++ VM. It accepts the full successful compiler payload
  or its inner `bytecode` object. Unknown opcodes, missing fields, invalid
  operand widths, mismatched instruction positions/counts, and malformed
  function metadata are rejected before execution. `bytecode-run` is the
  English compatibility alias.
- `obc-dogrula <file.obc> [metadata.json]` validates the serialized OBC
  structure and its metadata contract without executing it. New
  `orhun-obc-v2` metadata records payload size, CRC32, and SHA-256.
  `orhun-obc-v1` metadata remains verifiable through size and CRC32.
  `obc-verify` is the English compatibility alias.
- Newly generated packaged executables use an `ORHNPKG2` trailer containing
  payload size, CRC32, and SHA-256. `paketli-dogrula <packaged-file>` validates
  the trailer and embedded OBC without executing it; `packaged-verify` is its
  English compatibility alias. Legacy `ORHNPKG1` packages remain readable.
- `orhun-vm <dosya.oh>` is the experimental single-command bootstrap path. It
  compiles the target through `orhun/derleyici.oh`, validates the decoded
  bytecode contract, and executes it through the C++ VM. `bootstrap-vm` is the
  English compatibility alias.
- `orhun-derle <dosya.oh> [cikti]` compiles through the Orhun-written compiler
  and writes the same `.obc`, packaged executable, and metadata artifacts as
  `derle`. `bootstrap-compile` is the English compatibility alias.
- The guarded bootstrap path can compile `StdLib/orhun/derleyici.oh` itself
  into an `.obc` artifact byte-identical to the C++ compiler output. This is a
  self-source bootstrap milestone, not yet a standalone compiler CLI.
- VM module loading defaults to `ORHUN_MODULE_MODE=source`. Explicit
  `obc-first` mode loads a sibling `.obc` module when available and otherwise
  falls back to source compilation. Explicit `obc-only` mode requires the
  corresponding `.obc`, does not require a sibling `.oh`, and rejects missing
  precompiled modules without C++ compiler fallback. The guarded bootstrap
  test runs the Orhun compiler/parser/lexer module chain source-free in
  `obc-only` mode.
- `orhun-vm` and `orhun-derle` accept `--source`, `--obc-first`,
  `--obc-only`, or `--modul-modu=<mode>` to select the policy for that process.
- `bootstrap-hazirla <directory>` writes source-free `lexer.obc`,
  `parser.obc`, `derleyici.obc`, and `derleyici_cli.obc` artifacts plus a
  size/CRC32/SHA-256-bearing `orhun-bootstrap-v2`
  `bootstrap.manifest.json`. `orhun-bootstrap-v1` manifests remain verifiable.
  `bootstrap-prepare` is its compatibility alias.
- `bootstrap-dogrula <toolchain-directory>` validates the manifest contract,
  exact module set, payload sizes, CRC32 values, and OBC structure without
  compiling or executing a target. `bootstrap-verify` is its compatibility
  alias. Bootstrap build and run commands perform the same validation first.
- `bootstrap-derle <toolchain-directory> <source.oh> [output]` consumes a
  prepared toolchain in strict `obc-only` mode without environment-variable
  setup. `bootstrap-build` is its compatibility alias.
- `bootstrap-calistir <toolchain-directory> <source.oh>` compiles and executes
  through the same prepared strict toolchain. `bootstrap-run` is its
  compatibility alias.
- `bootstrap-derleyici-paketle <toolchain-directory> <output-directory>`
  creates a source-free portable compiler bundle containing an
  `orhun-derleyici` executable and its sibling `StdLib` toolchain.
  `bootstrap-compiler-bundle` is its compatibility alias. The executable
  validates and activates the sibling strict toolchain automatically, then
  emits Orhun compiler bytecode JSON for the source path passed as its first
  program argument. `orhun-derleyici --derle <source.oh> [output]` uses the
  same Orhun-written compiler and the runtime serialization bridge to emit
  `.obc`, packaged executable, and metadata artifacts directly. Source/output
  argument parsing and the complete artifact output plan are produced by
  `orhun/derleyici_cli.oh`. That plan contains the `.obc`, packaged executable,
  metadata paths, and metadata source name, and identifies its contract as
  `orhun-artifact-plan-v1`. The C++ bootstrap runtime rejects unknown plan
  contracts, empty fields, unexpected output suffixes, source names containing
  path separators, and colliding output paths before its OBC/package
  serialization bridge writes anything. Existing non-file targets are also
  rejected. Valid outputs are first written to unique sibling staging files;
  existing outputs are preserved and staged files are cleaned if any artifact
  cannot be produced. The packaged host does not dispatch individual
  compiler command names; it consumes the structured exit code and optional
  artifact plan returned by Orhun CLI bytecode. At startup it strictly
  validates the `orhun-bootstrap-compiler-bundle-v2` manifest, embedded CLI
  payload size/CRC32/SHA-256, and sibling toolchain path. V1 bundle manifests
  remain verifiable. Compiler-bundle identity does not depend on the executable
  filename, so a valid bundle executable may be renamed.
- `bootstrap-derleyici-dogrula <bundle-directory>` validates the complete
  portable compiler bundle without executing compiler CLI bytecode.
  `bootstrap-compiler-verify` is its compatibility alias.
- `bootstrap-yeniden-uret <seed-toolchain> <output-directory>` performs a
  reproducible three-stage bootstrap gate: the seed builds stage 2, stage 2
  builds stage 3, and every stage-2/stage-3 OBC artifact must be byte-identical.
  It refuses non-empty output directories and writes
  an `orhun-bootstrap-rebuild-v2` `bootstrap-rebuild.manifest.json` containing
  size, CRC32, and SHA-256 identity for every compiler module after success.
  `bootstrap-rebuild` is its compatibility alias.
- `bootstrap-yeniden-dogrula <toolchain-directory>` validates the rebuild-v2
  evidence and the companion strict toolchain without rebuilding it.
  `bootstrap-rebuild-verify` is its compatibility alias.

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
