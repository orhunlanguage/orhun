#include "Chunk.h"
#include "Compiler.h"
#include "Interpreter.h"
#include "Lexer.h"
#include "Parser.h"
#include "VM.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

constexpr char kPaketSihir[8] = {'O', 'R', 'H', 'N', 'P', 'K', 'G', '1'};

std::string dosyaOku(const std::string& dosyaYolu) {
  std::ifstream dosya(dosyaYolu, std::ios::binary);
  if (!dosya.is_open()) {
    throw std::runtime_error("Hata: '" + dosyaYolu + "' dosyasi acilamadi.");
  }

  std::ostringstream tampon;
  tampon << dosya.rdbuf();
  return tampon.str();
}

std::vector<std::uint8_t> dosyaOkuIkili(const std::string& dosyaYolu) {
  std::ifstream dosya(dosyaYolu, std::ios::binary);
  if (!dosya.is_open()) {
    throw std::runtime_error("Hata: '" + dosyaYolu + "' dosyasi acilamadi.");
  }
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(dosya),
                                   std::istreambuf_iterator<char>());
}

void dosyaYaz(const std::string& dosyaYolu, const std::string& icerik) {
  std::ofstream dosya(dosyaYolu, std::ios::binary | std::ios::trunc);
  if (!dosya.is_open()) {
    throw std::runtime_error("Hata: '" + dosyaYolu + "' dosyasina yazilamadi.");
  }
  dosya << icerik;
}

void dosyaYazIkili(const std::string& dosyaYolu,
                   const std::vector<std::uint8_t>& icerik) {
  std::ofstream dosya(dosyaYolu, std::ios::binary | std::ios::trunc);
  if (!dosya.is_open()) {
    throw std::runtime_error("Hata: '" + dosyaYolu + "' dosyasina yazilamadi.");
  }
  if (!icerik.empty()) {
    dosya.write(reinterpret_cast<const char*>(icerik.data()),
                static_cast<std::streamsize>(icerik.size()));
  }
}

std::string sagaBoslukKirp(std::string metin) {
  while (!metin.empty() && (metin.back() == ' ' || metin.back() == '\t' ||
                            metin.back() == '\r' || metin.back() == '\n')) {
    metin.pop_back();
  }
  return metin;
}

bool satirBlokBaslatir(const std::string& satir) {
  const std::string kirpilmis = sagaBoslukKirp(satir);
  return !kirpilmis.empty() && kirpilmis.back() == ':';
}

std::unique_ptr<ProgramNode> parseEt(const std::string& kaynakKod) {
  Lexer lexer(kaynakKod);
  std::vector<Token> tokenlar = lexer.tokenize();

  Parser parser(std::move(tokenlar));
  return parser.parse();
}

BytecodeChunk bytecodeDerle(const std::string& kaynakKod) {
  std::unique_ptr<ProgramNode> program = parseEt(kaynakKod);
  Compiler derleyici;
  return derleyici.derle(program.get());
}

bool kodCalistir(const std::string& kaynakKod, Interpreter& yorumlayici,
                 std::string* hataMesaji = nullptr) {
  try {
    std::unique_ptr<ProgramNode> program = parseEt(kaynakKod);
    yorumlayici.calistir(program.get());
    return true;
  } catch (const std::exception& ex) {
    if (hataMesaji != nullptr) {
      *hataMesaji = ex.what();
      return false;
    }
    throw;
  }
}

bool kodCalistirVM(const std::string& kaynakKod, std::string* hataMesaji = nullptr) {
  try {
    BytecodeChunk chunk = bytecodeDerle(kaynakKod);
    VM vm;
    vm.calistir(chunk);
    return true;
  } catch (const std::exception& ex) {
    if (hataMesaji != nullptr) {
      *hataMesaji = ex.what();
      return false;
    }
    throw;
  }
}

std::uint32_t crc32Hesapla(const std::vector<std::uint8_t>& veri) {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (const std::uint8_t b : veri) {
    crc ^= static_cast<std::uint32_t>(b);
    for (int i = 0; i < 8; ++i) {
      const bool lsb = (crc & 1u) != 0u;
      crc >>= 1u;
      if (lsb) {
        crc ^= 0xEDB88320u;
      }
    }
  }
  return ~crc;
}

void streamU32Yaz(std::ofstream& dosya, std::uint32_t deger) {
  const std::uint8_t ham[4] = {
      static_cast<std::uint8_t>(deger & 0xFF),
      static_cast<std::uint8_t>((deger >> 8) & 0xFF),
      static_cast<std::uint8_t>((deger >> 16) & 0xFF),
      static_cast<std::uint8_t>((deger >> 24) & 0xFF),
  };
  dosya.write(reinterpret_cast<const char*>(ham), 4);
}

std::uint32_t hamdanU32(const std::uint8_t* ham) {
  return static_cast<std::uint32_t>(ham[0]) |
         (static_cast<std::uint32_t>(ham[1]) << 8) |
         (static_cast<std::uint32_t>(ham[2]) << 16) |
         (static_cast<std::uint32_t>(ham[3]) << 24);
}

void paketliExeUret(const std::string& calisanExeYolu, const std::string& ciktiExeYolu,
                    const std::vector<std::uint8_t>& payload) {
  namespace fs = std::filesystem;
  fs::copy_file(fs::absolute(calisanExeYolu), fs::path(ciktiExeYolu),
                fs::copy_options::overwrite_existing);

  std::ofstream cikti(ciktiExeYolu, std::ios::binary | std::ios::app);
  if (!cikti.is_open()) {
    throw std::runtime_error("Hata: Paket exe acilamadi: " + ciktiExeYolu);
  }

  if (!payload.empty()) {
    cikti.write(reinterpret_cast<const char*>(payload.data()),
                static_cast<std::streamsize>(payload.size()));
  }

  const std::uint32_t boyut = static_cast<std::uint32_t>(payload.size());
  const std::uint32_t crc = crc32Hesapla(payload);
  cikti.write(kPaketSihir, static_cast<std::streamsize>(sizeof(kPaketSihir)));
  streamU32Yaz(cikti, boyut);
  streamU32Yaz(cikti, crc);
}

bool paketPayloadOku(const std::string& calisanExeYolu,
                     std::vector<std::uint8_t>& payload) {
  std::ifstream dosya(calisanExeYolu, std::ios::binary);
  if (!dosya.is_open()) {
    return false;
  }

  dosya.seekg(0, std::ios::end);
  const std::streamoff toplamBoyut = dosya.tellg();
  constexpr std::streamoff trailerBoyutu = 8 + 4 + 4;
  if (toplamBoyut < trailerBoyutu) {
    return false;
  }

  dosya.seekg(toplamBoyut - trailerBoyutu, std::ios::beg);
  std::uint8_t trailer[16] = {};
  dosya.read(reinterpret_cast<char*>(trailer), trailerBoyutu);
  if (dosya.gcount() != trailerBoyutu) {
    return false;
  }

  if (!std::equal(std::begin(kPaketSihir), std::end(kPaketSihir), trailer)) {
    return false;
  }

  const std::uint32_t payloadBoyutu = hamdanU32(trailer + 8);
  const std::uint32_t crcBeklenen = hamdanU32(trailer + 12);
  if (payloadBoyutu > static_cast<std::uint32_t>(toplamBoyut - trailerBoyutu)) {
    throw std::runtime_error("Paket bozuk: payload boyutu gecersiz.");
  }

  const std::streamoff payloadBaslangic =
      toplamBoyut - trailerBoyutu - static_cast<std::streamoff>(payloadBoyutu);
  dosya.seekg(payloadBaslangic, std::ios::beg);
  payload.resize(payloadBoyutu);
  if (payloadBoyutu > 0) {
    dosya.read(reinterpret_cast<char*>(payload.data()),
               static_cast<std::streamsize>(payloadBoyutu));
    if (static_cast<std::uint32_t>(dosya.gcount()) != payloadBoyutu) {
      throw std::runtime_error("Paket bozuk: payload okunamadi.");
    }
  }

  const std::uint32_t crcGercek = crc32Hesapla(payload);
  if (crcGercek != crcBeklenen) {
    throw std::runtime_error("Paket bozuk: CRC32 dogrulamasi basarisiz.");
  }

  return true;
}

bool gomuluPaketiCalistir(const std::string& calisanExeYolu) {
  std::vector<std::uint8_t> payload;
  if (!paketPayloadOku(calisanExeYolu, payload)) {
    return false;
  }

  BytecodeChunk chunk = chunkCoz(payload);
  VM vm;
  vm.calistir(chunk);
  return true;
}

int replCalistir() {
  Interpreter yorumlayici;
  std::string tampon;
  bool blokToplaniyor = false;

  std::cout << "Orhun REPL basladi. Cikmak icin 'cikis' yazin.\n";
  std::cout
      << "Blok komutlarinda (':' ile biten satirlar) calistirmak icin bos satir girin.\n";

  while (true) {
    std::cout << (tampon.empty() ? "orhun> " : "....> ");
    std::string satir;
    if (!std::getline(std::cin, satir)) {
      std::cout << '\n';
      break;
    }

    const std::string kirpilmis = sagaBoslukKirp(satir);
    if (tampon.empty() &&
        (kirpilmis == "cikis" || kirpilmis == "çıkış" ||
         kirpilmis == "exit" || kirpilmis == "quit")) {
      break;
    }

    if (satir.empty()) {
      if (tampon.empty()) {
        continue;
      }

      std::string hata;
      if (!kodCalistir(tampon, yorumlayici, &hata)) {
        std::cerr << hata << '\n';
      }
      tampon.clear();
      blokToplaniyor = false;
      continue;
    }

    tampon += satir;
    tampon.push_back('\n');

    if (satirBlokBaslatir(satir)) {
      blokToplaniyor = true;
      continue;
    }

    if (blokToplaniyor) {
      continue;
    }

    std::string hata;
    if (!kodCalistir(tampon, yorumlayici, &hata)) {
      std::cerr << hata << '\n';
    }
    tampon.clear();
  }

  if (!tampon.empty()) {
    std::string hata;
    if (!kodCalistir(tampon, yorumlayici, &hata)) {
      std::cerr << hata << '\n';
    }
  }

  return 0;
}

std::string tokeniYaziyaCevir(const Token& token) {
  if (token.tur != TokenType::METIN) {
    return token.deger;
  }

  std::string sonuc = "\"";
  for (const char c : token.deger) {
    switch (c) {
      case '\\':
        sonuc += "\\\\";
        break;
      case '"':
        sonuc += "\\\"";
        break;
      case '\n':
        sonuc += "\\n";
        break;
      case '\t':
        sonuc += "\\t";
        break;
      default:
        sonuc.push_back(c);
        break;
    }
  }
  sonuc += "\"";
  return sonuc;
}

bool noktalamaKapanisMi(const Token& token) {
  return token.tur == TokenType::ISLEM &&
         (token.deger == ")" || token.deger == "]" || token.deger == "}" ||
          token.deger == "," || token.deger == ":" || token.deger == ".");
}

bool acilisNoktalamasiMi(const Token& token) {
  return token.tur == TokenType::ISLEM &&
         (token.deger == "(" || token.deger == "[" || token.deger == "{" ||
          token.deger == ".");
}

bool boslukGerekliMi(const Token& onceki, const Token& simdiki) {
  if (simdiki.tur == TokenType::ISLEM && simdiki.deger == ".") {
    return false;
  }
  if (onceki.tur == TokenType::ISLEM && onceki.deger == ".") {
    return false;
  }
  if (noktalamaKapanisMi(simdiki)) {
    return false;
  }
  if (acilisNoktalamasiMi(onceki)) {
    return onceki.deger == ":";
  }
  if (simdiki.tur == TokenType::ISLEM && simdiki.deger == "(") {
    if (onceki.tur == TokenType::KIMLIK || onceki.tur == TokenType::ANAHTAR_KELIME) {
      return false;
    }
    if (onceki.tur == TokenType::ISLEM &&
        (onceki.deger == ")" || onceki.deger == "]")) {
      return false;
    }
  }
  if (simdiki.tur == TokenType::ISLEM && simdiki.deger == "[") {
    if (onceki.tur == TokenType::KIMLIK || onceki.tur == TokenType::ANAHTAR_KELIME) {
      return false;
    }
    if (onceki.tur == TokenType::ISLEM &&
        (onceki.deger == ")" || onceki.deger == "]")) {
      return false;
    }
  }
  if (onceki.tur == TokenType::ISLEM && onceki.deger == ",") {
    return true;
  }
  return true;
}

void sondakiBosluklariTemizle(std::string& satir) {
  while (!satir.empty() && (satir.back() == ' ' || satir.back() == '\t')) {
    satir.pop_back();
  }
}

std::string bicimlendir(const std::vector<Token>& tokenlar) {
  std::string sonuc;
  int girintiSeviyesi = 0;
  bool satirBasi = true;
  bool oncekiTokenVar = false;
  Token oncekiToken{};

  for (const Token& token : tokenlar) {
    if (token.tur == TokenType::DOSYA_SONU) {
      break;
    }
    if (token.tur == TokenType::HATA) {
      throw std::runtime_error("Satir " + std::to_string(token.satir) +
                               ": Bicimlendirme sirasinda lexer hatasi: " +
                               token.deger);
    }
    if (token.tur == TokenType::GIRINTI) {
      ++girintiSeviyesi;
      continue;
    }
    if (token.tur == TokenType::CIKINTI) {
      girintiSeviyesi = std::max(0, girintiSeviyesi - 1);
      continue;
    }
    if (token.tur == TokenType::YENI_SATIR) {
      sondakiBosluklariTemizle(sonuc);
      if (sonuc.empty() || sonuc.back() != '\n') {
        sonuc.push_back('\n');
      }
      satirBasi = true;
      oncekiTokenVar = false;
      continue;
    }

    if (satirBasi) {
      sonuc.append(static_cast<std::size_t>(girintiSeviyesi) * 4, ' ');
      satirBasi = false;
    }

    if (oncekiTokenVar && boslukGerekliMi(oncekiToken, token)) {
      sonuc.push_back(' ');
    }

    sonuc += tokeniYaziyaCevir(token);
    oncekiToken = token;
    oncekiTokenVar = true;
  }

  sondakiBosluklariTemizle(sonuc);
  if (!sonuc.empty() && sonuc.back() != '\n') {
    sonuc.push_back('\n');
  }
  return sonuc;
}

int komutFmt(const std::string& dosyaYolu) {
  const std::string kaynakKod = dosyaOku(dosyaYolu);
  Lexer lexer(kaynakKod);
  const std::vector<Token> tokenlar = lexer.tokenize();
  const std::string yeniIcerik = bicimlendir(tokenlar);
  dosyaYaz(dosyaYolu, yeniIcerik);

  std::cout << "Bicimlendirildi: " << dosyaYolu << "\n";
  return 0;
}

struct LintMesaji {
  std::size_t satir = 0;
  std::string seviye;
  std::string mesaj;
};

std::vector<std::string> satirlaraBol(const std::string& icerik) {
  std::vector<std::string> satirlar;
  std::string aktif;
  for (char c : icerik) {
    if (c == '\n') {
      satirlar.push_back(aktif);
      aktif.clear();
      continue;
    }
    if (c != '\r') {
      aktif.push_back(c);
    }
  }
  if (!aktif.empty() || icerik.empty() || icerik.back() != '\n') {
    satirlar.push_back(aktif);
  }
  return satirlar;
}

void lintMesajiEkle(std::vector<LintMesaji>& mesajlar, std::size_t satir,
                    const std::string& seviye, const std::string& mesaj) {
  mesajlar.push_back({satir, seviye, mesaj});
}

std::vector<LintMesaji> lintCalistir(const std::string& kaynakKod) {
  std::vector<LintMesaji> mesajlar;
  const std::vector<std::string> satirlar = satirlaraBol(kaynakKod);
  std::size_t bosSeri = 0;

  for (std::size_t i = 0; i < satirlar.size(); ++i) {
    const std::string& satir = satirlar[i];
    const std::size_t satirNo = i + 1;

    if (satir.find('\t') != std::string::npos) {
      lintMesajiEkle(mesajlar, satirNo, "uyari",
                     "Tab karakteri tespit edildi; 4 bosluk kullanin.");
    }
    if (!satir.empty() && (satir.back() == ' ' || satir.back() == '\t')) {
      lintMesajiEkle(mesajlar, satirNo, "uyari",
                     "Satir sonunda gereksiz bosluk var.");
    }
    if (satir.size() > 140) {
      lintMesajiEkle(
          mesajlar, satirNo, "uyari",
          "Satir uzunlugu 140 karakteri asiyor (okunabilirlik dusuyor).");
    }

    std::size_t bosluk = 0;
    while (bosluk < satir.size() && satir[bosluk] == ' ') {
      ++bosluk;
    }
    const bool bosSatir = bosluk == satir.size();
    if (!bosSatir && (bosluk % 4) != 0) {
      lintMesajiEkle(
          mesajlar, satirNo, "uyari",
          "Girinti 4'un kati degil; blok hizalamasi bozulabilir.");
    }

    if (bosSatir) {
      ++bosSeri;
      if (bosSeri > 2) {
        lintMesajiEkle(mesajlar, satirNo, "uyari",
                       "Ardisik cok fazla bos satir var.");
      }
    } else {
      bosSeri = 0;
    }
  }

  try {
    static_cast<void>(parseEt(kaynakKod));
  } catch (const std::exception& ex) {
    lintMesajiEkle(mesajlar, 0, "hata",
                   std::string("Parser hatasi: ") + ex.what());
  }

  return mesajlar;
}

int komutLint(const std::string& dosyaYolu, bool strict) {
  if (dosyaYolu.size() < 3 || dosyaYolu.substr(dosyaYolu.size() - 3) != ".oh") {
    throw std::runtime_error("Hata: lint komutu icin .oh dosyasi bekleniyor.");
  }

  const std::string kaynakKod = dosyaOku(dosyaYolu);
  const std::vector<LintMesaji> mesajlar = lintCalistir(kaynakKod);
  std::size_t hataSayisi = 0;
  std::size_t uyariSayisi = 0;

  for (const auto& mesaj : mesajlar) {
    if (mesaj.seviye == "hata") {
      ++hataSayisi;
      std::cout << "[HATA] " << mesaj.mesaj << "\n";
      continue;
    }
    ++uyariSayisi;
    std::cout << "[UYARI] Satir " << mesaj.satir << ": " << mesaj.mesaj << "\n";
  }

  std::cout << "Lint ozeti: " << hataSayisi << " hata, " << uyariSayisi
            << " uyari.\n";
  if (hataSayisi > 0 || (strict && uyariSayisi > 0)) {
    return 1;
  }
  return 0;
}

struct DepoKaydi {
  std::string ad;
  std::string kaynak;
  std::string aciklama;
};

std::filesystem::path varsayilanDepoIndexYolu() {
  namespace fs = std::filesystem;
  if (const char* env = std::getenv("ORHUN_DEPO_INDEX")) {
    if (*env != '\0') {
      return fs::path(env);
    }
  }

  const fs::path yol = fs::current_path() / "orhun_depo" / "index.txt";
  return yol;
}

std::string solaSagaKirp(const std::string& metin) {
  std::size_t bas = 0;
  while (bas < metin.size() &&
         (metin[bas] == ' ' || metin[bas] == '\t' || metin[bas] == '\r' ||
          metin[bas] == '\n')) {
    ++bas;
  }

  std::size_t son = metin.size();
  while (son > bas &&
         (metin[son - 1] == ' ' || metin[son - 1] == '\t' ||
          metin[son - 1] == '\r' || metin[son - 1] == '\n')) {
    --son;
  }

  return metin.substr(bas, son - bas);
}

std::string asciiKucuk(const std::string& metin) {
  std::string sonuc = metin;
  std::transform(sonuc.begin(), sonuc.end(), sonuc.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return sonuc;
}

std::vector<DepoKaydi> depoIndexOku(const std::filesystem::path& indexYolu) {
  std::vector<DepoKaydi> kayitlar;
  if (!std::filesystem::exists(indexYolu)) {
    return kayitlar;
  }

  std::istringstream akis(dosyaOku(indexYolu.string()));
  std::string satir;
  while (std::getline(akis, satir)) {
    const std::string temiz = solaSagaKirp(satir);
    if (temiz.empty() || temiz[0] == '#') {
      continue;
    }

    const std::size_t p1 = temiz.find('|');
    if (p1 == std::string::npos) {
      continue;
    }
    const std::size_t p2 = temiz.find('|', p1 + 1);
    if (p2 == std::string::npos) {
      continue;
    }

    DepoKaydi kayit;
    kayit.ad = solaSagaKirp(temiz.substr(0, p1));
    kayit.kaynak = solaSagaKirp(temiz.substr(p1 + 1, p2 - (p1 + 1)));
    kayit.aciklama = solaSagaKirp(temiz.substr(p2 + 1));
    if (!kayit.ad.empty() && !kayit.kaynak.empty()) {
      kayitlar.push_back(std::move(kayit));
    }
  }

  return kayitlar;
}

int komutPaketDepoBaslat(const std::string& klasor) {
  namespace fs = std::filesystem;
  const fs::path kok = klasor.empty() ? (fs::current_path() / "orhun_depo")
                                      : fs::path(klasor);
  fs::create_directories(kok / "paketler");

  const fs::path index = kok / "index.txt";
  if (!fs::exists(index)) {
    const std::string ornek =
        "# ad | kaynak | aciklama\n"
        "# ornek_paket | https://github.com/ornek/orhun-ornek.git | ornek aciklama\n";
    dosyaYaz(index.string(), ornek);
  }

  const fs::path readme = kok / "README.md";
  if (!fs::exists(readme)) {
    dosyaYaz(readme.string(),
             "# Orhun Paket Deposu\n\n"
             "- Paket ekle: `orhun paket depo-ekle <ad> <kaynak> [aciklama]`\n"
             "- Paket ara: `orhun paket ara <kelime>`\n"
             "- Paket kur: `orhun paket kur depo:<ad>`\n");
  }

  std::cout << "Depo baslatildi: " << kok.string() << "\n";
  std::cout << "Index: " << index.string() << "\n";
  return 0;
}

int komutPaketDepoEkle(const std::string& ad, const std::string& kaynak,
                       const std::string& aciklama) {
  if (ad.empty() || kaynak.empty()) {
    throw std::runtime_error(
        "Hata: paket depo-ekle <ad> <kaynak> [aciklama] kullanin.");
  }

  const std::filesystem::path indexYolu = varsayilanDepoIndexYolu();
  std::filesystem::create_directories(indexYolu.parent_path());
  auto kayitlar = depoIndexOku(indexYolu);
  for (const auto& kayit : kayitlar) {
    if (kayit.ad == ad) {
      throw std::runtime_error("Hata: '" + ad +
                               "' kaydi zaten depoda mevcut.");
    }
  }

  std::ofstream dosya(indexYolu, std::ios::app | std::ios::binary);
  if (!dosya.is_open()) {
    throw std::runtime_error("Hata: depo index dosyasina yazilamadi: " +
                             indexYolu.string());
  }
  dosya << ad << " | " << kaynak << " | " << aciklama << "\n";

  std::cout << "Depoya eklendi: " << ad << " -> " << kaynak << "\n";
  return 0;
}

int komutPaketAra(const std::string& sorgu) {
  if (sorgu.empty()) {
    throw std::runtime_error("Hata: paket ara <kelime> kullanin.");
  }

  const std::filesystem::path indexYolu = varsayilanDepoIndexYolu();
  const auto kayitlar = depoIndexOku(indexYolu);
  if (kayitlar.empty()) {
    std::cout << "Depo kaydi bulunamadi: " << indexYolu.string() << "\n";
    return 0;
  }

  const std::string needle = asciiKucuk(sorgu);
  std::size_t sayi = 0;
  for (const auto& kayit : kayitlar) {
    const std::string havuz =
        asciiKucuk(kayit.ad + " " + kayit.kaynak + " " + kayit.aciklama);
    if (havuz.find(needle) == std::string::npos) {
      continue;
    }
    ++sayi;
    std::cout << "- " << kayit.ad << " -> " << kayit.kaynak;
    if (!kayit.aciklama.empty()) {
      std::cout << " | " << kayit.aciklama;
    }
    std::cout << "\n";
  }

  if (sayi == 0) {
    std::cout << "Sonuc bulunamadi.\n";
  } else {
    std::cout << "Toplam " << sayi << " paket bulundu.\n";
  }
  return 0;
}

std::optional<std::string> depodanKaynakCoz(const std::string& referans) {
  if (referans.rfind("depo:", 0) != 0) {
    return std::nullopt;
  }

  const std::string paketAdi = referans.substr(5);
  if (paketAdi.empty()) {
    return std::nullopt;
  }

  const auto kayitlar = depoIndexOku(varsayilanDepoIndexYolu());
  for (const auto& kayit : kayitlar) {
    if (kayit.ad == paketAdi) {
      return kayit.kaynak;
    }
  }
  return std::nullopt;
}

int komutPaketYeni(const std::string& projeAdi) {
  namespace fs = std::filesystem;
  if (projeAdi.empty()) {
    throw std::runtime_error("Hata: Proje adi bos olamaz.");
  }

  const fs::path kok = fs::current_path() / projeAdi;
  if (fs::exists(kok)) {
    throw std::runtime_error("Hata: '" + kok.string() + "' zaten mevcut.");
  }

  fs::create_directories(kok / "lib");

  const std::string anaDosya =
      "# " + projeAdi + "\n"
      "yazdır \"Merhaba Orhun!\"\n";
  dosyaYaz((kok / "main.oh").string(), anaDosya);

  const std::string yapilandirma =
      "ad: \"" + projeAdi + "\"\n"
      "surum: \"0.1.0\"\n"
      "ana_dosya: \"main.oh\"\n";
  dosyaYaz((kok / "orhun.yap").string(), yapilandirma);

  std::cout << "Paket iskeleti olusturuldu: " << kok.string() << "\n";
  return 0;
}

std::string paketAdiCikar(const std::string& kaynak) {
  if (kaynak.empty()) {
    return "paket";
  }

  std::string temiz = kaynak;
  while (!temiz.empty() && (temiz.back() == '/' || temiz.back() == '\\')) {
    temiz.pop_back();
  }
  const std::size_t slash = temiz.find_last_of("/\\");
  std::string ad = slash == std::string::npos ? temiz : temiz.substr(slash + 1);
  if (ad.size() > 4 && ad.substr(ad.size() - 4) == ".git") {
    ad = ad.substr(0, ad.size() - 4);
  }
  if (ad.empty()) {
    return "paket";
  }
  return ad;
}

bool uzakKaynakMi(const std::string& kaynak) {
  return kaynak.rfind("http://", 0) == 0 ||
         kaynak.rfind("https://", 0) == 0 ||
         kaynak.rfind("git@", 0) == 0 ||
         kaynak.rfind("ssh://", 0) == 0;
}

bool paketAdiGecerliMi(const std::string& ad) {
  if (ad.empty()) {
    return false;
  }
  for (char c : ad) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (!(std::isalnum(uc) || c == '_' || c == '-' || c == '.')) {
      return false;
    }
  }
  return true;
}

bool uzakKaynakGuvenliMi(const std::string& kaynak) {
  if (!uzakKaynakMi(kaynak)) {
    return false;
  }
  for (char c : kaynak) {
    if (c == '"' || c == '\'' || c == '`' || c == '\n' || c == '\r' || c == '\t' ||
        c == '&' || c == '|' || c == ';' || c == '<' || c == '>' || c == '$') {
      return false;
    }
  }
  return true;
}

std::optional<std::string> uzakKaynakHostBul(const std::string& kaynak) {
  if (kaynak.rfind("http://", 0) == 0 || kaynak.rfind("https://", 0) == 0 ||
      kaynak.rfind("ssh://", 0) == 0) {
    const std::size_t scheme = kaynak.find("://");
    if (scheme == std::string::npos) {
      return std::nullopt;
    }
    std::size_t bas = scheme + 3;
    const std::size_t atPos = kaynak.find('@', bas);
    if (atPos != std::string::npos &&
        atPos < kaynak.find_first_of(":/", bas)) {
      bas = atPos + 1;
    }
    const std::size_t son = kaynak.find_first_of(":/", bas);
    const std::string host =
        son == std::string::npos ? kaynak.substr(bas) : kaynak.substr(bas, son - bas);
    if (!host.empty()) {
      return asciiKucuk(host);
    }
    return std::nullopt;
  }
  if (kaynak.rfind("git@", 0) == 0) {
    const std::size_t bas = 4;
    const std::size_t son = kaynak.find(':', bas);
    if (son == std::string::npos || son <= bas) {
      return std::nullopt;
    }
    return asciiKucuk(kaynak.substr(bas, son - bas));
  }
  return std::nullopt;
}

bool paketKaynakAllowlistteMi(const std::string& kaynak) {
  const auto host = uzakKaynakHostBul(kaynak);
  if (!host.has_value()) {
    return false;
  }
  std::unordered_set<std::string> izinliler = {"github.com", "gitlab.com", "bitbucket.org"};
  if (const char* env = std::getenv("ORHUN_PAKET_ALLOWLIST")) {
    std::istringstream ak(env);
    for (std::string parca; std::getline(ak, parca, ',');) {
      const std::string trim = asciiKucuk(solaSagaKirp(parca));
      if (!trim.empty()) {
        izinliler.insert(trim);
      }
    }
  }
  if (izinliler.find(*host) != izinliler.end()) {
    return true;
  }
  for (const std::string& alan : izinliler) {
    if (host->size() > alan.size() &&
        host->compare(host->size() - alan.size(), alan.size(), alan) == 0 &&
        (*host)[host->size() - alan.size() - 1] == '.') {
      return true;
    }
  }
  return false;
}

int processCalistir(const std::vector<std::string>& argumanlar) {
  if (argumanlar.empty()) {
    throw std::runtime_error("Hata: processCalistir icin komut verilmedi.");
  }
#ifdef _WIN32
  std::vector<char*> ham;
  ham.reserve(argumanlar.size() + 1);
  for (const std::string& s : argumanlar) {
    ham.push_back(const_cast<char*>(s.c_str()));
  }
  ham.push_back(nullptr);
  const int kod = _spawnvp(_P_WAIT, argumanlar[0].c_str(), ham.data());
  if (kod == -1) {
    throw std::runtime_error("Hata: '" + argumanlar[0] +
                             "' calistirilamadi: " + std::strerror(errno));
  }
  return kod;
#else
  std::vector<char*> ham;
  ham.reserve(argumanlar.size() + 1);
  for (const std::string& s : argumanlar) {
    ham.push_back(const_cast<char*>(s.c_str()));
  }
  ham.push_back(nullptr);

  const pid_t pid = fork();
  if (pid < 0) {
    throw std::runtime_error("Hata: process fork basarisiz.");
  }
  if (pid == 0) {
    execvp(ham[0], ham.data());
    _exit(127);
  }
  int durum = 0;
  if (waitpid(pid, &durum, 0) < 0) {
    throw std::runtime_error("Hata: process wait basarisiz.");
  }
  if (WIFEXITED(durum)) {
    return WEXITSTATUS(durum);
  }
  return 1;
#endif
}

bool programYoldaVarMi(const std::string& ad) {
  const char* envPath = std::getenv("PATH");
  if (envPath == nullptr || *envPath == '\0') {
    return false;
  }
  const std::string pathDegeri(envPath);
#ifdef _WIN32
  const char ayrac = ';';
  const std::vector<std::string> uzantilar = {".exe", ".cmd", ".bat", ""};
#else
  const char ayrac = ':';
  const std::vector<std::string> uzantilar = {""};
#endif
  std::size_t bas = 0;
  while (bas <= pathDegeri.size()) {
    const std::size_t son = pathDegeri.find(ayrac, bas);
    const std::string parca =
        son == std::string::npos ? pathDegeri.substr(bas)
                                 : pathDegeri.substr(bas, son - bas);
    if (!parca.empty()) {
      const std::filesystem::path kok(parca);
      for (const std::string& uzanti : uzantilar) {
        const std::filesystem::path aday = kok / (ad + uzanti);
        std::error_code ec;
        if (std::filesystem::exists(aday, ec) && !ec &&
            std::filesystem::is_regular_file(aday, ec)) {
          return true;
        }
      }
    }
    if (son == std::string::npos) {
      break;
    }
    bas = son + 1;
  }
  return false;
}

std::string crc32Hex(const std::string& metin) {
  std::vector<std::uint8_t> veri(metin.begin(), metin.end());
  std::ostringstream ss;
  ss << std::hex << std::setfill('0') << std::setw(8) << crc32Hesapla(veri);
  return ss.str();
}

void orhunLockKaydet(const std::string& paketAdi, const std::string& kaynak) {
  namespace fs = std::filesystem;
  const fs::path lockDosyasi = fs::current_path() / "orhun.lock";
  std::vector<std::string> satirlar;
  if (fs::exists(lockDosyasi)) {
    std::istringstream ak(dosyaOku(lockDosyasi.string()));
    for (std::string satir; std::getline(ak, satir);) {
      satirlar.push_back(satir);
    }
  } else {
    satirlar.push_back("# ad|kaynak|crc32");
  }

  const std::string kayit =
      paketAdi + "|" + kaynak + "|" + crc32Hex(kaynak + "|" + paketAdi);
  bool guncellendi = false;
  for (std::string& satir : satirlar) {
    if (satir.rfind(paketAdi + "|", 0) == 0) {
      satir = kayit;
      guncellendi = true;
      break;
    }
  }
  if (!guncellendi) {
    satirlar.push_back(kayit);
  }

  std::ostringstream yeni;
  for (std::size_t i = 0; i < satirlar.size(); ++i) {
    yeni << satirlar[i];
    if (i + 1 < satirlar.size()) {
      yeni << '\n';
    }
  }
  dosyaYaz(lockDosyasi.string(), yeni.str());
}

void orhunYapBagimlilikEkle(const std::string& paketAdi) {
  namespace fs = std::filesystem;
  const fs::path yapDosyasi = fs::current_path() / "orhun.yap";
  if (!fs::exists(yapDosyasi)) {
    return;
  }

  std::string icerik = dosyaOku(yapDosyasi.string());
  if (icerik.find("- " + paketAdi) != std::string::npos) {
    return;
  }

  if (icerik.find("bagimliliklar:") == std::string::npos) {
    if (!icerik.empty() && icerik.back() != '\n') {
      icerik.push_back('\n');
    }
    icerik += "bagimliliklar:\n";
  }

  if (!icerik.empty() && icerik.back() != '\n') {
    icerik.push_back('\n');
  }
  icerik += "- " + paketAdi + "\n";
  dosyaYaz(yapDosyasi.string(), icerik);
}

int komutPaketKur(const std::string& kaynak, const std::string& hedefAdi) {
  namespace fs = std::filesystem;
  if (kaynak.empty()) {
    throw std::runtime_error("Hata: paket kur icin kaynak belirtilmeli.");
  }

  std::string cozulmusKaynak = kaynak;
  if (kaynak.rfind("depo:", 0) == 0) {
    auto cozum = depodanKaynakCoz(kaynak);
    if (!cozum.has_value()) {
      throw std::runtime_error(
          "Hata: '" + kaynak +
          "' deposunda paket bulunamadi. 'orhun paket ara <kelime>' ile arayin.");
    }
    cozulmusKaynak = cozum.value();
  }

  fs::path libKlasoru = fs::current_path() / "lib";
  fs::create_directories(libKlasoru);

  const std::string paketAdi =
      hedefAdi.empty() ? paketAdiCikar(cozulmusKaynak) : hedefAdi;
  if (!paketAdiGecerliMi(paketAdi)) {
    throw std::runtime_error(
        "Hata: paket adi yalnizca harf/rakam/_/./- icerebilir.");
  }
  const fs::path hedefYol = libKlasoru / paketAdi;
  if (fs::exists(hedefYol)) {
    throw std::runtime_error("Hata: '" + hedefYol.string() + "' zaten mevcut.");
  }

  std::error_code ec;
  if (fs::exists(cozulmusKaynak, ec) && !ec) {
    fs::copy(cozulmusKaynak, hedefYol,
             fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    if (ec) {
      throw std::runtime_error("Hata: Yerel paket kopyalanamadi: " + ec.message());
    }
  } else if (uzakKaynakMi(cozulmusKaynak)) {
    if (!uzakKaynakGuvenliMi(cozulmusKaynak)) {
      throw std::runtime_error(
          "Hata: Paket kaynagi guvenli degil. Tehlikeli karakterler iceriyor.");
    }
    if (!paketKaynakAllowlistteMi(cozulmusKaynak)) {
      throw std::runtime_error(
          "Hata: Paket kaynagi allowlist disinda. ORHUN_PAKET_ALLOWLIST ile izinli alan adlarini genisletebilirsiniz.");
    }
    const int kod = processCalistir(
        {"git", "clone", "--depth", "1", cozulmusKaynak, hedefYol.string()});
    if (kod != 0) {
      throw std::runtime_error(
          "Hata: Paket indirilemedi. Git clone cikis kodu: " +
          std::to_string(kod));
    }
  } else {
    throw std::runtime_error(
        "Hata: Paket kaynagi bulunamadi. Yerel yol ya da git URL verin.");
  }

  orhunYapBagimlilikEkle(paketAdi);
  orhunLockKaydet(paketAdi, cozulmusKaynak);
  std::cout << "Paket kuruldu: " << paketAdi << " -> " << hedefYol.string() << "\n";
  return 0;
}

int komutPaketListe() {
  namespace fs = std::filesystem;
  const fs::path libKlasoru = fs::current_path() / "lib";
  if (!fs::exists(libKlasoru)) {
    std::cout << "Kurulu paket yok (lib klasoru bulunamadi).\n";
    return 0;
  }

  std::vector<std::string> paketler;
  for (const auto& giris : fs::directory_iterator(libKlasoru)) {
    if (giris.is_directory()) {
      paketler.push_back(giris.path().filename().u8string());
    }
  }
  std::sort(paketler.begin(), paketler.end());

  if (paketler.empty()) {
    std::cout << "Kurulu paket yok.\n";
    return 0;
  }

  std::cout << "Kurulu paketler:\n";
  for (const auto& ad : paketler) {
    std::cout << "- " << ad << "\n";
  }
  return 0;
}

bool vmFallbackAcikMi() {
  const char* deger = std::getenv("ORHUN_VM_FALLBACK");
  if (deger == nullptr) {
    return true;
  }
  std::string metin(deger);
  std::transform(metin.begin(), metin.end(), metin.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return !(metin == "0" || metin == "false" || metin == "off" || metin == "hayir" ||
           metin == "no");
}

bool vmFallbackIzinliHata(const std::string& hata) {
  static const std::vector<std::string> izinliImzalar = {
      "ORH-COMP-001", "ORH-COMP-002", "ORH-COMP-003",
      "Faz 2 kapsaminda degil", "Atama hedefi VM Faz 2 tarafinda desteklenmiyor"};
  for (const auto& imza : izinliImzalar) {
    if (hata.find(imza) != std::string::npos) {
      return true;
    }
  }
  return false;
}

int dosyaCalistirVM(const std::string& dosyaYolu, bool katiMod) {
  if (dosyaYolu.size() < 3 || dosyaYolu.substr(dosyaYolu.size() - 3) != ".oh") {
    throw std::runtime_error("Hata: VM calistirma icin .oh dosyasi bekleniyor.");
  }

  const std::string kod = dosyaOku(dosyaYolu);
  if (katiMod) {
    kodCalistirVM(kod);
    return 0;
  }

  try {
    kodCalistirVM(kod);
  } catch (const std::exception& ex) {
    if (katiMod || !vmFallbackAcikMi() || !vmFallbackIzinliHata(ex.what())) {
      throw;
    }
    // Uretim modunda VM hatalarinda da yorumlayiciya dus: kullanici acisindan
    // davranis korunur; vm-kati modunda ise dogrudan hata verilir.
    static bool uyariYazildi = false;
    if (!uyariYazildi) {
      std::cerr << "[uyari] VM fallback devrede. ORHUN_VM_FALLBACK=0 ile kapatabilirsiniz.\n";
      uyariYazildi = true;
    }
    Interpreter yorumlayici;
    kodCalistir(kod, yorumlayici);
  }
  return 0;
}

template <typename F>
double tekOlcumMilisaniye(F&& fonksiyon) {
  const auto bas = std::chrono::steady_clock::now();
  fonksiyon();
  const auto son = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(son - bas).count();
}

template <typename F>
std::vector<double> dagilimOlcMilisaniye(F&& fonksiyon, int tekrar) {
  if (tekrar <= 0) {
    throw std::runtime_error("hiz komutu icin tekrar sayisi pozitif olmali.");
  }
  std::vector<double> olcumler;
  olcumler.reserve(static_cast<std::size_t>(tekrar));
  for (int i = 0; i < tekrar; ++i) {
    olcumler.push_back(tekOlcumMilisaniye(fonksiyon));
  }
  return olcumler;
}

double toplamMilisaniye(const std::vector<double>& olcumler) {
  double toplam = 0.0;
  for (double ms : olcumler) {
    toplam += ms;
  }
  return toplam;
}

double yuzdeDilimi(std::vector<double> olcumler, double yuzde) {
  if (olcumler.empty()) {
    return 0.0;
  }
  std::sort(olcumler.begin(), olcumler.end());
  const double clamped = std::clamp(yuzde, 0.0, 100.0);
  const double konum = (clamped / 100.0) * static_cast<double>(olcumler.size() - 1);
  const auto altIndex = static_cast<std::size_t>(std::floor(konum));
  const auto ustIndex = static_cast<std::size_t>(std::ceil(konum));
  if (altIndex == ustIndex) {
    return olcumler[altIndex];
  }
  const double oran = konum - static_cast<double>(altIndex);
  return olcumler[altIndex] + (olcumler[ustIndex] - olcumler[altIndex]) * oran;
}

std::string jsonKacis(const std::string& metin) {
  std::ostringstream ss;
  for (char c : metin) {
    switch (c) {
      case '\\':
        ss << "\\\\";
        break;
      case '"':
        ss << "\\\"";
        break;
      case '\n':
        ss << "\\n";
        break;
      case '\r':
        ss << "\\r";
        break;
      case '\t':
        ss << "\\t";
        break;
      default:
        ss << c;
        break;
    }
  }
  return ss.str();
}

int komutHiz(const std::string& dosyaYolu, int tekrar, bool jsonCikti) {
  if (dosyaYolu.size() < 3 || dosyaYolu.substr(dosyaYolu.size() - 3) != ".oh") {
    throw std::runtime_error("Hata: hiz komutu icin .oh dosyasi bekleniyor.");
  }

  const std::string kod = dosyaOku(dosyaYolu);
  std::ostringstream yut;
  auto* eskiCout = std::cout.rdbuf(yut.rdbuf());
  try {
    const std::vector<double> yorumlayiciOlcumleri = dagilimOlcMilisaniye([&]() {
      Interpreter yorumlayici;
      kodCalistir(kod, yorumlayici);
    }, tekrar);

    const std::vector<double> vmOlcumleri = dagilimOlcMilisaniye([&]() {
      kodCalistirVM(kod);
    }, tekrar);

    std::cout.rdbuf(eskiCout);

    const double yorumlayiciToplam = toplamMilisaniye(yorumlayiciOlcumleri);
    const double vmToplam = toplamMilisaniye(vmOlcumleri);
    const double yorumlayiciP50 = yuzdeDilimi(yorumlayiciOlcumleri, 50.0);
    const double yorumlayiciP90 = yuzdeDilimi(yorumlayiciOlcumleri, 90.0);
    const double vmP50 = yuzdeDilimi(vmOlcumleri, 50.0);
    const double vmP90 = yuzdeDilimi(vmOlcumleri, 90.0);
    const double hizlanma = vmToplam > 0.0 ? (yorumlayiciToplam / vmToplam) : 0.0;
    const double p50Hizlanma = vmP50 > 0.0 ? (yorumlayiciP50 / vmP50) : 0.0;
    const double p90Hizlanma = vmP90 > 0.0 ? (yorumlayiciP90 / vmP90) : 0.0;

    if (jsonCikti) {
      std::cout << "{"
                << "\"dosya\":\"" << jsonKacis(dosyaYolu) << "\","
                << "\"tekrar\":" << tekrar << ","
                << "\"interpreter\":{\"toplam_ms\":" << yorumlayiciToplam
                << ",\"p50_ms\":" << yorumlayiciP50 << ",\"p90_ms\":" << yorumlayiciP90
                << "},"
                << "\"vm\":{\"toplam_ms\":" << vmToplam << ",\"p50_ms\":" << vmP50
                << ",\"p90_ms\":" << vmP90 << "},"
                << "\"hizlanma\":{\"toplam_x\":" << hizlanma << ",\"p50_x\":" << p50Hizlanma
                << ",\"p90_x\":" << p90Hizlanma << "}"
                << "}\n";
      return 0;
    }

    std::cout << "Dosya: " << dosyaYolu << "\n";
    std::cout << "Tekrar: " << tekrar << "\n";
    std::cout << "Interpreter toplam: " << yorumlayiciToplam << " ms\n";
    std::cout << "Interpreter P50/P90: " << yorumlayiciP50 << " / " << yorumlayiciP90
              << " ms\n";
    std::cout << "VM toplam: " << vmToplam << " ms\n";
    std::cout << "VM P50/P90: " << vmP50 << " / " << vmP90 << " ms\n";
    if (vmToplam > 0.0) {
      std::cout << "Hizlanma (toplam): " << hizlanma << "x\n";
    }
    if (vmP50 > 0.0) {
      std::cout << "Hizlanma (P50): " << p50Hizlanma << "x\n";
    }
    if (vmP90 > 0.0) {
      std::cout << "Hizlanma (P90): " << p90Hizlanma << "x\n";
    }
    return 0;
  } catch (...) {
    std::cout.rdbuf(eskiCout);
    throw;
  }
}

int komutObcCalistir(const std::string& obcDosyaYolu) {
  const std::vector<std::uint8_t> ham = dosyaOkuIkili(obcDosyaYolu);
  BytecodeChunk chunk = chunkCoz(ham);
  VM vm;
  vm.calistir(chunk);
  return 0;
}

int komutDerle(const std::string& kaynakYolu, const std::string& calisanExeYolu,
               const std::string& ciktiTemel) {
  namespace fs = std::filesystem;
  if (kaynakYolu.size() < 3 || kaynakYolu.substr(kaynakYolu.size() - 3) != ".oh") {
    throw std::runtime_error("Hata: derle komutu .oh dosyasi bekler.");
  }

  const BytecodeChunk chunk = bytecodeDerle(dosyaOku(kaynakYolu));
  const std::vector<std::uint8_t> payload = chunkSerilestir(chunk);

  fs::path temel = ciktiTemel.empty() ? fs::path(kaynakYolu) : fs::path(ciktiTemel);
  if (temel.has_extension()) {
    temel.replace_extension("");
  } else if (ciktiTemel.empty()) {
    temel.replace_extension("");
  }

  fs::path obcYolu = temel;
  obcYolu.replace_extension(".obc");
  fs::path exeYolu = temel;
  exeYolu.replace_extension(".exe");
  fs::path metaYolu = temel;
  metaYolu.replace_extension(".obc.meta.json");

  dosyaYazIkili(obcYolu.string(), payload);
  paketliExeUret(calisanExeYolu, exeYolu.string(), payload);

  const std::uint32_t payloadCrc = crc32Hesapla(payload);
  std::ostringstream meta;
  meta << "{\n"
       << "  \"format\": \"orhun-obc-v1\",\n"
       << "  \"payload_size\": " << payload.size() << ",\n"
       << "  \"payload_crc32\": \"" << std::hex << std::setfill('0') << std::setw(8)
       << payloadCrc << "\",\n"
       << "  \"source_name\": \"" << jsonKacis(fs::path(kaynakYolu).filename().u8string())
       << "\"\n"
       << "}\n";
  dosyaYaz(metaYolu.string(), meta.str());

  std::cout << "Bytecode uretildi: " << obcYolu.string() << "\n";
  std::cout << "Paketli exe uretildi: " << exeYolu.string() << "\n";
  std::cout << "Metadata yazildi: " << metaYolu.string() << "\n";
  return 0;
}

std::string evetHayir(bool deger) { return deger ? "evet" : "hayir"; }

int komutDoctor() {
  namespace fs = std::filesystem;
  const bool hasTests = fs::exists("tests/run_tests.ps1") || fs::exists("tests/run_tests.sh");
  const bool hasCases = fs::exists("tests/cases");
  const bool hasStdLib = fs::exists("Yerlesik.h");
  const bool hasCompiler = fs::exists("Compiler.cpp") && fs::exists("VM.cpp");
  const bool hasLspTool = fs::exists("tools/vscode/package.json") ||
                          fs::exists("tools/vscode-orhun/package.json");
  const bool lockVar = fs::exists("orhun.lock");
  const bool fallback = vmFallbackAcikMi();

  const bool gitVar = programYoldaVarMi("git");

  std::cout << "Orhun Doctor Raporu\n";
  std::cout << "-------------------\n";
  std::cout << "VM derleyici dosyalari: " << evetHayir(hasCompiler) << "\n";
  std::cout << "StdLib cekirdegi (Yerlesik.h): " << evetHayir(hasStdLib) << "\n";
  std::cout << "Test altyapisi: " << evetHayir(hasTests && hasCases) << "\n";
  std::cout << "LSP/VSCode araclari: " << evetHayir(hasLspTool) << "\n";
  std::cout << "Paket lock dosyasi (orhun.lock): " << evetHayir(lockVar) << "\n";
  std::cout << "Git erisimi: " << evetHayir(gitVar) << "\n";
  std::cout << "VM fallback varsayilan durumu: "
            << (fallback ? "acik (ORHUN_VM_FALLBACK=0 ile kapat)" : "kapali")
            << "\n";
#ifdef _WIN32
  std::cout << "Windows Console UTF-8: "
            << evetHayir(GetConsoleOutputCP() == CP_UTF8 && GetConsoleCP() == CP_UTF8)
            << "\n";
#endif
  const bool saglikli = hasCompiler && hasStdLib && hasTests;
  std::cout << "Genel durum: " << (saglikli ? "hazir" : "eksikler var") << "\n";
  return saglikli ? 0 : 2;
}

std::string soldanBoslukKirp(std::string metin) {
  std::size_t i = 0;
  while (i < metin.size() && std::isspace(static_cast<unsigned char>(metin[i]))) {
    ++i;
  }
  return metin.substr(i);
}

std::optional<std::string> lspMesajOku(std::istream& in) {
  std::string satir;
  std::size_t icerikUzunlugu = 0;
  bool uzunlukBulundu = false;

  while (std::getline(in, satir)) {
    if (!satir.empty() && satir.back() == '\r') {
      satir.pop_back();
    }
    if (satir.empty()) {
      break;
    }
    const std::string onEk = "Content-Length:";
    if (satir.rfind(onEk, 0) == 0) {
      std::string sayi = soldanBoslukKirp(satir.substr(onEk.size()));
      icerikUzunlugu = static_cast<std::size_t>(std::stoul(sayi));
      uzunlukBulundu = true;
    }
  }

  if (!uzunlukBulundu) {
    return std::nullopt;
  }

  std::string govde(icerikUzunlugu, '\0');
  in.read(govde.data(), static_cast<std::streamsize>(icerikUzunlugu));
  if (static_cast<std::size_t>(in.gcount()) != icerikUzunlugu) {
    return std::nullopt;
  }
  return govde;
}

std::optional<std::string> lspIdTokenBul(const std::string& json) {
  const std::size_t idPos = json.find("\"id\"");
  if (idPos == std::string::npos) {
    return std::nullopt;
  }
  std::size_t i = json.find(':', idPos + 4);
  if (i == std::string::npos) {
    return std::nullopt;
  }
  ++i;
  while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) {
    ++i;
  }
  if (i >= json.size()) {
    return std::nullopt;
  }

  if (json[i] == '"') {
    const std::size_t bas = i++;
    bool kacis = false;
    while (i < json.size()) {
      if (!kacis && json[i] == '"') {
        ++i;
        return json.substr(bas, i - bas);
      }
      if (!kacis && json[i] == '\\') {
        kacis = true;
      } else {
        kacis = false;
      }
      ++i;
    }
    return std::nullopt;
  }

  const std::size_t bas = i;
  while (i < json.size() && json[i] != ',' && json[i] != '}' && json[i] != ']') {
    ++i;
  }
  return sagaBoslukKirp(json.substr(bas, i - bas));
}

bool lspMethodMu(const std::string& json, const std::string& method) {
  return json.find("\"method\":\"" + method + "\"") != std::string::npos ||
         json.find("\"method\": \"" + method + "\"") != std::string::npos;
}

void lspYanitYaz(std::ostream& out, const std::string& idToken,
                 const std::string& resultJson) {
  const std::string yuk = std::string("{\"jsonrpc\":\"2.0\",\"id\":") + idToken +
                          ",\"result\":" + resultJson + "}";
  out << "Content-Length: " << yuk.size() << "\r\n\r\n" << yuk;
  out.flush();
}

std::string lspTamamlamaSonucuJson() {
  static const std::vector<std::string> anahtarlar = {
      "yazdır",   "olsun",     "eğer",      "ise",      "değilse",
      "doğru",    "yanlış",    "eşit",      "eşit_değil", "büyük",
      "küçük",    "ve",        "veya",      "değil",    "tekrarla",
      "kez",      "sürece",    "sor",       "işlev",    "döndür",
      "tip",      "yeni",      "benim",     "ust",      "deneme",
      "yakala",   "kır",       "devam",     "dahil_et", "için",
      "içinde"};
  std::ostringstream ss;
  ss << "{\"isIncomplete\":false,\"items\":[";
  for (std::size_t i = 0; i < anahtarlar.size(); ++i) {
    if (i > 0) {
      ss << ",";
    }
    ss << "{\"label\":\"" << anahtarlar[i]
       << "\",\"kind\":14,\"detail\":\"Orhun anahtar kelimesi\"}";
  }
  ss << "]}";
  return ss.str();
}

std::optional<std::string> lspJsonStringAlanBul(const std::string& json,
                                                const std::string& alan,
                                                std::size_t baslangic = 0) {
  const std::string anahtar = "\"" + alan + "\"";
  std::size_t p = json.find(anahtar, baslangic);
  if (p == std::string::npos) {
    return std::nullopt;
  }
  p = json.find(':', p + anahtar.size());
  if (p == std::string::npos) {
    return std::nullopt;
  }
  ++p;
  while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) {
    ++p;
  }
  if (p >= json.size() || json[p] != '"') {
    return std::nullopt;
  }
  ++p;

  std::string sonuc;
  bool kacis = false;
  while (p < json.size()) {
    const char c = json[p++];
    if (kacis) {
      switch (c) {
        case 'n':
          sonuc.push_back('\n');
          break;
        case 'r':
          sonuc.push_back('\r');
          break;
        case 't':
          sonuc.push_back('\t');
          break;
        case '\\':
          sonuc.push_back('\\');
          break;
        case '"':
          sonuc.push_back('"');
          break;
        default:
          sonuc.push_back(c);
          break;
      }
      kacis = false;
      continue;
    }
    if (c == '\\') {
      kacis = true;
      continue;
    }
    if (c == '"') {
      return sonuc;
    }
    sonuc.push_back(c);
  }
  return std::nullopt;
}

std::vector<int> lspJsonSayiAlanlariBul(const std::string& json,
                                        const std::string& alan) {
  std::vector<int> sonuc;
  const std::string anahtar = "\"" + alan + "\"";
  std::size_t pos = 0;
  while (true) {
    pos = json.find(anahtar, pos);
    if (pos == std::string::npos) {
      break;
    }
    std::size_t i = json.find(':', pos + anahtar.size());
    if (i == std::string::npos) {
      break;
    }
    ++i;
    while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) {
      ++i;
    }
    std::size_t j = i;
    if (j < json.size() && (json[j] == '-' || json[j] == '+')) {
      ++j;
    }
    while (j < json.size() && std::isdigit(static_cast<unsigned char>(json[j]))) {
      ++j;
    }
    if (j > i) {
      sonuc.push_back(std::stoi(json.substr(i, j - i)));
    }
    pos = j;
  }
  return sonuc;
}

std::vector<std::string> metniSatirlaraBol(const std::string& metin) {
  std::vector<std::string> satirlar;
  std::istringstream ak(metin);
  for (std::string satir; std::getline(ak, satir);) {
    if (!satir.empty() && satir.back() == '\r') {
      satir.pop_back();
    }
    satirlar.push_back(std::move(satir));
  }
  if (satirlar.empty()) {
    satirlar.emplace_back("");
  }
  return satirlar;
}

std::optional<std::string> satirdanTanimAdiBul(const std::string& satir,
                                               const std::string& onEk) {
  if (satir.rfind(onEk, 0) != 0) {
    return std::nullopt;
  }
  std::size_t i = onEk.size();
  while (i < satir.size() && std::isspace(static_cast<unsigned char>(satir[i]))) {
    ++i;
  }
  std::size_t j = i;
  while (j < satir.size()) {
    const unsigned char c = static_cast<unsigned char>(satir[j]);
    if (!(std::isalnum(c) || c == '_' || c >= 128)) {
      break;
    }
    ++j;
  }
  if (j <= i) {
    return std::nullopt;
  }
  return satir.substr(i, j - i);
}

std::string lspDocumentSymbolJson(const std::string& uri, const std::string& metin) {
  const auto satirlar = metniSatirlaraBol(metin);
  struct Sembol {
    std::string ad;
    int kind = 13;
    std::size_t satir = 0;
  };
  std::vector<Sembol> semboller;

  for (std::size_t i = 0; i < satirlar.size(); ++i) {
    const std::string trim = soldanBoslukKirp(sagaBoslukKirp(satirlar[i]));
    if (trim.empty() || trim[0] == '#') {
      continue;
    }
    if (auto ad = satirdanTanimAdiBul(trim, "işlev "); ad.has_value()) {
      semboller.push_back({*ad, 12, i});
      continue;
    }
    if (auto ad = satirdanTanimAdiBul(trim, "islev "); ad.has_value()) {
      semboller.push_back({*ad, 12, i});
      continue;
    }
    if (auto ad = satirdanTanimAdiBul(trim, "tip "); ad.has_value()) {
      semboller.push_back({*ad, 5, i});
      continue;
    }
    const std::size_t olsunPos = trim.find(" olsun ");
    const std::size_t esitPos = trim.find(" = ");
    std::size_t pos = std::string::npos;
    if (olsunPos != std::string::npos) {
      pos = olsunPos;
    } else if (esitPos != std::string::npos) {
      pos = esitPos;
    }
    if (pos != std::string::npos) {
      std::string ad = sagaBoslukKirp(trim.substr(0, pos));
      if (!ad.empty()) {
        semboller.push_back({ad, 13, i});
      }
    }
  }

  std::ostringstream ss;
  ss << "[";
  for (std::size_t i = 0; i < semboller.size(); ++i) {
    if (i > 0) {
      ss << ",";
    }
    ss << "{\"name\":\"" << jsonKacis(semboller[i].ad)
       << "\",\"kind\":" << semboller[i].kind
       << ",\"location\":{\"uri\":\"" << jsonKacis(uri)
       << "\",\"range\":{\"start\":{\"line\":" << semboller[i].satir
       << ",\"character\":0},\"end\":{\"line\":" << semboller[i].satir
       << ",\"character\":1}}}}";
  }
  ss << "]";
  return ss.str();
}

int hataMesajindanSatirBul(const std::string& mesaj) {
  const std::string etiket = "satir ";
  std::size_t p = mesaj.find(etiket);
  if (p == std::string::npos) {
    return 0;
  }
  p += etiket.size();
  std::size_t j = p;
  while (j < mesaj.size() && std::isdigit(static_cast<unsigned char>(mesaj[j]))) {
    ++j;
  }
  if (j == p) {
    return 0;
  }
  int satir = std::stoi(mesaj.substr(p, j - p));
  if (satir <= 0) {
    return 0;
  }
  return satir - 1;
}

std::string lspDiagnosticJson(const std::string& metin) {
  try {
    static_cast<void>(parseEt(metin));
    return "{\"kind\":\"full\",\"items\":[]}";
  } catch (const std::exception& ex) {
    const std::string mesaj = ex.what();
    const int satir = hataMesajindanSatirBul(mesaj);
    std::ostringstream ss;
    ss << "{\"kind\":\"full\",\"items\":[{\"range\":{\"start\":{\"line\":" << satir
       << ",\"character\":0},\"end\":{\"line\":" << satir
       << ",\"character\":1}},\"severity\":1,\"source\":\"orhun\",\"message\":\""
       << jsonKacis(mesaj) << "\"}]}";
    return ss.str();
  }
}

std::string lspDefinitionJson(const std::string& uri, const std::string& metin,
                              int satirNo, int karakterNo) {
  const auto satirlar = metniSatirlaraBol(metin);
  if (satirNo < 0 || static_cast<std::size_t>(satirNo) >= satirlar.size()) {
    return "[]";
  }
  const std::string& satir = satirlar[static_cast<std::size_t>(satirNo)];
  if (satir.empty()) {
    return "[]";
  }
  if (karakterNo < 0) {
    karakterNo = 0;
  }
  std::size_t idx = static_cast<std::size_t>(std::min<int>(
      karakterNo, static_cast<int>(satir.size() - 1)));
  auto kelimeKarakteri = [](unsigned char c) {
    return std::isalnum(c) || c == '_' || c >= 128;
  };
  if (!kelimeKarakteri(static_cast<unsigned char>(satir[idx]))) {
    return "[]";
  }
  std::size_t sol = idx;
  while (sol > 0 && kelimeKarakteri(static_cast<unsigned char>(satir[sol - 1]))) {
    --sol;
  }
  std::size_t sag = idx;
  while (sag + 1 < satir.size() &&
         kelimeKarakteri(static_cast<unsigned char>(satir[sag + 1]))) {
    ++sag;
  }
  const std::string aranan = satir.substr(sol, sag - sol + 1);
  if (aranan.empty()) {
    return "[]";
  }

  for (std::size_t i = 0; i < satirlar.size(); ++i) {
    const std::string trim = soldanBoslukKirp(sagaBoslukKirp(satirlar[i]));
    if (trim.rfind("işlev " + aranan, 0) == 0 || trim.rfind("islev " + aranan, 0) == 0 ||
        trim.rfind("tip " + aranan, 0) == 0 || trim.rfind(aranan + " olsun ", 0) == 0 ||
        trim.rfind(aranan + " = ", 0) == 0) {
      std::ostringstream ss;
      ss << "[{\"uri\":\"" << jsonKacis(uri)
         << "\",\"range\":{\"start\":{\"line\":" << i
         << ",\"character\":0},\"end\":{\"line\":" << i
         << ",\"character\":1}}}]";
      return ss.str();
    }
  }
  return "[]";
}

void lspBildirimYaz(std::ostream& out, const std::string& method,
                    const std::string& paramsJson) {
  const std::string yuk = std::string("{\"jsonrpc\":\"2.0\",\"method\":\"") + method +
                          "\",\"params\":" + paramsJson + "}";
  out << "Content-Length: " << yuk.size() << "\r\n\r\n" << yuk;
  out.flush();
}

int komutLsp(bool stdioModu) {
  (void)stdioModu;
  bool shutdownIstendi = false;
  std::unordered_map<std::string, std::string> acikBelgeler;
  while (true) {
    std::optional<std::string> gelen = lspMesajOku(std::cin);
    if (!gelen.has_value()) {
      break;
    }
    const std::string& mesaj = *gelen;
    const std::optional<std::string> idToken = lspIdTokenBul(mesaj);

    if (lspMethodMu(mesaj, "initialize")) {
      if (idToken.has_value()) {
        lspYanitYaz(std::cout, *idToken,
                    "{\"capabilities\":{\"textDocumentSync\":1,"
                    "\"completionProvider\":{\"resolveProvider\":false},"
                    "\"definitionProvider\":true,"
                    "\"documentSymbolProvider\":true,"
                    "\"diagnosticProvider\":{\"interFileDependencies\":false,"
                    "\"workspaceDiagnostics\":false}}}");
      }
      continue;
    }
    if (lspMethodMu(mesaj, "textDocument/didOpen")) {
      auto uri = lspJsonStringAlanBul(mesaj, "uri");
      auto text = lspJsonStringAlanBul(mesaj, "text");
      if (uri.has_value() && text.has_value()) {
        acikBelgeler[*uri] = *text;
        // lspDiagnosticJson çıktısı {"kind":"full","items":[...]} formatında;
        // publishDiagnostics için yalnız items listesi gerekir.
        const std::string tanilama = lspDiagnosticJson(*text);
        const std::size_t itemsPos = tanilama.find("\"items\":");
        std::string items = "[]";
        if (itemsPos != std::string::npos) {
          const std::size_t bas = tanilama.find('[', itemsPos);
          const std::size_t son = tanilama.rfind(']');
          if (bas != std::string::npos && son != std::string::npos && son >= bas) {
            items = tanilama.substr(bas, son - bas + 1);
          }
        }
        lspBildirimYaz(std::cout, "textDocument/publishDiagnostics",
                       "{\"uri\":\"" + jsonKacis(*uri) + "\",\"diagnostics\":" + items +
                           "}");
      }
      continue;
    }
    if (lspMethodMu(mesaj, "textDocument/didChange")) {
      auto uri = lspJsonStringAlanBul(mesaj, "uri");
      auto text = lspJsonStringAlanBul(mesaj, "text");
      if (uri.has_value() && text.has_value()) {
        acikBelgeler[*uri] = *text;
        const std::string tanilama = lspDiagnosticJson(*text);
        const std::size_t itemsPos = tanilama.find("\"items\":");
        std::string items = "[]";
        if (itemsPos != std::string::npos) {
          const std::size_t bas = tanilama.find('[', itemsPos);
          const std::size_t son = tanilama.rfind(']');
          if (bas != std::string::npos && son != std::string::npos && son >= bas) {
            items = tanilama.substr(bas, son - bas + 1);
          }
        }
        lspBildirimYaz(std::cout, "textDocument/publishDiagnostics",
                       "{\"uri\":\"" + jsonKacis(*uri) + "\",\"diagnostics\":" + items +
                           "}");
      }
      continue;
    }
    if (lspMethodMu(mesaj, "initialized")) {
      continue;
    }
    if (lspMethodMu(mesaj, "shutdown")) {
      shutdownIstendi = true;
      if (idToken.has_value()) {
        lspYanitYaz(std::cout, *idToken, "null");
      }
      continue;
    }
    if (lspMethodMu(mesaj, "exit")) {
      return shutdownIstendi ? 0 : 1;
    }
    if (lspMethodMu(mesaj, "textDocument/completion")) {
      if (idToken.has_value()) {
        lspYanitYaz(std::cout, *idToken, lspTamamlamaSonucuJson());
      }
      continue;
    }
    if (lspMethodMu(mesaj, "textDocument/documentSymbol")) {
      if (idToken.has_value()) {
        const auto uri = lspJsonStringAlanBul(mesaj, "uri");
        if (!uri.has_value()) {
          lspYanitYaz(std::cout, *idToken, "[]");
          continue;
        }
        const auto it = acikBelgeler.find(*uri);
        const std::string metin = it == acikBelgeler.end() ? "" : it->second;
        lspYanitYaz(std::cout, *idToken, lspDocumentSymbolJson(*uri, metin));
      }
      continue;
    }
    if (lspMethodMu(mesaj, "textDocument/diagnostic")) {
      if (idToken.has_value()) {
        const auto uri = lspJsonStringAlanBul(mesaj, "uri");
        if (!uri.has_value()) {
          lspYanitYaz(std::cout, *idToken, "{\"kind\":\"full\",\"items\":[]}");
          continue;
        }
        const auto it = acikBelgeler.find(*uri);
        const std::string metin = it == acikBelgeler.end() ? "" : it->second;
        lspYanitYaz(std::cout, *idToken, lspDiagnosticJson(metin));
      }
      continue;
    }
    if (lspMethodMu(mesaj, "textDocument/definition")) {
      if (idToken.has_value()) {
        const auto uri = lspJsonStringAlanBul(mesaj, "uri");
        const auto satirlar = lspJsonSayiAlanlariBul(mesaj, "line");
        const auto karakterler = lspJsonSayiAlanlariBul(mesaj, "character");
        if (!uri.has_value() || satirlar.empty() || karakterler.empty()) {
          lspYanitYaz(std::cout, *idToken, "[]");
          continue;
        }
        const auto it = acikBelgeler.find(*uri);
        const std::string metin = it == acikBelgeler.end() ? "" : it->second;
        lspYanitYaz(std::cout, *idToken,
                    lspDefinitionJson(*uri, metin, satirlar.front(),
                                      karakterler.front()));
      }
      continue;
    }
    if (idToken.has_value()) {
      lspYanitYaz(std::cout, *idToken, "null");
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif

  try {
    auto dahiliKomutMu = [](const std::string& deger) {
      return deger == "fmt" || deger == "paket" || deger == "vm" ||
             deger == "vm-kati" || deger == "obc" || deger == "derle" ||
             deger == "hiz" || deger == "lint" || deger == "lsp" ||
             deger == "doctor";
    };

    if (argc < 2) {
      if (gomuluPaketiCalistir(argv[0])) {
        return 0;
      }
      return replCalistir();
    }

    const std::string komut = argv[1];
    if (komut == "fmt") {
      if (argc < 3) {
        throw std::runtime_error("Hata: fmt komutu icin dosya adi bekleniyor.");
      }
      return komutFmt(argv[2]);
    }

    if (komut == "lint") {
      if (argc < 3) {
        throw std::runtime_error("Hata: lint komutu icin dosya adi bekleniyor.");
      }
      bool strict = false;
      if (argc >= 4) {
        const std::string secenek = argv[3];
        if (secenek == "--strict") {
          strict = true;
        } else {
          throw std::runtime_error("Hata: bilinmeyen lint secenegi. Yalnizca '--strict' destekleniyor.");
        }
      }
      return komutLint(argv[2], strict);
    }

    if (komut == "paket") {
      if (argc < 3) {
        throw std::runtime_error(
            "Hata: paket komutlari: yeni | kur | ekle | liste | ara | depo-baslat | depo-ekle");
      }

      const std::string alt = argv[2];
      if (alt == "yeni") {
        if (argc < 4) {
          throw std::runtime_error(
              "Hata: paket yeni icin proje adi bekleniyor.");
        }
        return komutPaketYeni(argv[3]);
      }

      if (alt == "kur") {
        if (argc < 4) {
          throw std::runtime_error(
              "Hata: paket kur <kaynak> [paket_adi] kullanin.");
        }
        const std::string hedefAdi = argc >= 5 ? argv[4] : "";
        return komutPaketKur(argv[3], hedefAdi);
      }

      if (alt == "ekle") {
        if (argc < 4) {
          throw std::runtime_error(
              "Hata: paket ekle <kaynak> [paket_adi] kullanin.");
        }
        const std::string hedefAdi = argc >= 5 ? argv[4] : "";
        return komutPaketKur(argv[3], hedefAdi);
      }

      if (alt == "liste") {
        return komutPaketListe();
      }

      if (alt == "ara") {
        if (argc < 4) {
          throw std::runtime_error("Hata: paket ara <kelime> kullanin.");
        }
        return komutPaketAra(argv[3]);
      }

      if (alt == "depo-baslat") {
        const std::string klasor = argc >= 4 ? argv[3] : "";
        return komutPaketDepoBaslat(klasor);
      }

      if (alt == "depo-ekle") {
        if (argc < 5) {
          throw std::runtime_error(
              "Hata: paket depo-ekle <ad> <kaynak> [aciklama] kullanin.");
        }
        std::string aciklama;
        if (argc >= 6) {
          aciklama = argv[5];
          for (int i = 6; i < argc; ++i) {
            aciklama += " ";
            aciklama += argv[i];
          }
        }
        return komutPaketDepoEkle(argv[3], argv[4], aciklama);
      }

      throw std::runtime_error(
          "Hata: bilinmeyen paket komutu. 'yeni', 'kur', 'ekle', 'liste', 'ara', 'depo-baslat' veya 'depo-ekle' kullanin.");
    }

    if (komut == "vm") {
      if (argc < 3) {
        throw std::runtime_error("Hata: vm komutu icin .oh dosyasi bekleniyor.");
      }
      return dosyaCalistirVM(argv[2], false);
    }

    if (komut == "vm-kati") {
      if (argc < 3) {
        throw std::runtime_error("Hata: vm-kati komutu icin .oh dosyasi bekleniyor.");
      }
      return dosyaCalistirVM(argv[2], true);
    }

    if (komut == "obc") {
      if (argc < 3) {
        throw std::runtime_error("Hata: obc komutu icin .obc dosyasi bekleniyor.");
      }
      return komutObcCalistir(argv[2]);
    }

    if (komut == "derle") {
      if (argc < 3) {
        throw std::runtime_error("Hata: derle komutu icin kaynak .oh bekleniyor.");
      }
      const std::string ciktiTemel = argc >= 4 ? argv[3] : "";
      return komutDerle(argv[2], argv[0], ciktiTemel);
    }

    if (komut == "hiz") {
      if (argc < 3) {
        throw std::runtime_error("Hata: hiz komutu icin kaynak .oh bekleniyor.");
      }
      int tekrar = 20;
      bool json = false;
      for (int i = 3; i < argc; ++i) {
        const std::string secenek = argv[i];
        if (secenek == "--json") {
          json = true;
          continue;
        }
        if (secenek.rfind("--tekrar=", 0) == 0) {
          tekrar = std::stoi(secenek.substr(9));
          continue;
        }
        if (!secenek.empty() && secenek[0] != '-') {
          tekrar = std::stoi(secenek);
          continue;
        }
        throw std::runtime_error(
            "Hata: hiz secenekleri: [tekrar] [--json] [--tekrar=N]");
      }
      return komutHiz(argv[2], tekrar, json);
    }

    if (komut == "lsp") {
      bool stdioModu = true;
      if (argc >= 3) {
        const std::string secenek = argv[2];
        if (secenek != "--stdio") {
          throw std::runtime_error("Hata: lsp komutu yalnizca '--stdio' destekler.");
        }
      }
      return komutLsp(stdioModu);
    }

    if (komut == "doctor") {
      return komutDoctor();
    }

    // Paketli exe, dahili komut dışındaki çağrılarda gömülü payload'ı
    // çalıştırır. Böylece "oyun.exe --mod hızlı" gibi kullanımda ana script
    // devreye girer.
    if (!dahiliKomutMu(komut) && gomuluPaketiCalistir(argv[0])) {
      return 0;
    }

    // Varsayilan motor VM'dir; desteklenmeyen ozellikte otomatik Interpreter
    // fallback yapilir.
    return dosyaCalistirVM(komut, false);
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}
