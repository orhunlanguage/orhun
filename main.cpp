#include "Chunk.h"
#include "Compiler.h"
#include "Interpreter.h"
#include "Lexer.h"
#include "Parser.h"
#include "VM.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
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

bool vmDerlemeHatasiMi(const std::exception& ex) {
  const std::string mesaj = ex.what();
  return mesaj.rfind("VM Derleme Hatasi", 0) == 0;
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

  fs::path libKlasoru = fs::current_path() / "lib";
  fs::create_directories(libKlasoru);

  const std::string paketAdi = hedefAdi.empty() ? paketAdiCikar(kaynak) : hedefAdi;
  const fs::path hedefYol = libKlasoru / paketAdi;
  if (fs::exists(hedefYol)) {
    throw std::runtime_error("Hata: '" + hedefYol.string() + "' zaten mevcut.");
  }

  std::error_code ec;
  if (fs::exists(kaynak, ec) && !ec) {
    fs::copy(kaynak, hedefYol,
             fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    if (ec) {
      throw std::runtime_error("Hata: Yerel paket kopyalanamadi: " + ec.message());
    }
  } else if (uzakKaynakMi(kaynak)) {
    const std::string komut = "git clone --depth 1 \"" + kaynak + "\" \"" +
                              hedefYol.string() + "\"";
    const int kod = std::system(komut.c_str());
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

int dosyaCalistir(const std::string& dosyaYolu) {
  if (dosyaYolu.size() < 3 || dosyaYolu.substr(dosyaYolu.size() - 3) != ".oh") {
    throw std::runtime_error("Hata: Orhun kaynak dosyasi .oh uzantili olmalidir.");
  }

  Interpreter yorumlayici;
  kodCalistir(dosyaOku(dosyaYolu), yorumlayici);
  return 0;
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
    if (!vmDerlemeHatasiMi(ex)) {
      throw;
    }
    std::cerr << "[VM] " << ex.what() << "\n";
    std::cerr << "[VM] Desteklenmeyen ozellik bulundu, Interpreter moduna geciliyor.\n";
    Interpreter yorumlayici;
    kodCalistir(kod, yorumlayici);
  }
  return 0;
}

template <typename F>
double olcMilisaniye(F&& fonksiyon, int tekrar) {
  if (tekrar <= 0) {
    throw std::runtime_error("hiz komutu icin tekrar sayisi pozitif olmali.");
  }
  const auto bas = std::chrono::steady_clock::now();
  for (int i = 0; i < tekrar; ++i) {
    fonksiyon();
  }
  const auto son = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(son - bas).count();
}

int komutHiz(const std::string& dosyaYolu, int tekrar) {
  if (dosyaYolu.size() < 3 || dosyaYolu.substr(dosyaYolu.size() - 3) != ".oh") {
    throw std::runtime_error("Hata: hiz komutu icin .oh dosyasi bekleniyor.");
  }

  const std::string kod = dosyaOku(dosyaYolu);
  std::ostringstream yut;
  auto* eskiCout = std::cout.rdbuf(yut.rdbuf());
  try {
    const double yorumlayiciMs = olcMilisaniye([&]() {
      Interpreter yorumlayici;
      kodCalistir(kod, yorumlayici);
    }, tekrar);

    const double vmMs = olcMilisaniye([&]() {
      kodCalistirVM(kod);
    }, tekrar);

    std::cout.rdbuf(eskiCout);

    std::cout << "Dosya: " << dosyaYolu << "\n";
    std::cout << "Tekrar: " << tekrar << "\n";
    std::cout << "Interpreter toplam: " << yorumlayiciMs << " ms\n";
    std::cout << "VM toplam: " << vmMs << " ms\n";
    if (vmMs > 0.0) {
      std::cout << "Hizlanma: " << (yorumlayiciMs / vmMs) << "x\n";
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

  dosyaYazIkili(obcYolu.string(), payload);
  paketliExeUret(calisanExeYolu, exeYolu.string(), payload);

  std::cout << "Bytecode uretildi: " << obcYolu.string() << "\n";
  std::cout << "Paketli exe uretildi: " << exeYolu.string() << "\n";
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
             deger == "hiz" || deger == "lint";
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
            "Hata: paket komutlari: yeni | kur | ekle | liste");
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

      throw std::runtime_error(
          "Hata: bilinmeyen paket komutu. 'yeni', 'kur', 'ekle' veya 'liste' kullanin.");
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
      if (argc >= 4) {
        tekrar = std::stoi(argv[3]);
      }
      return komutHiz(argv[2], tekrar);
    }

    // Paketli exe, dahili komut dışındaki çağrılarda gömülü payload'ı
    // çalıştırır. Böylece "oyun.exe --mod hızlı" gibi kullanımda ana script
    // devreye girer.
    if (!dahiliKomutMu(komut) && gomuluPaketiCalistir(argv[0])) {
      return 0;
    }

    return dosyaCalistir(komut);
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}
