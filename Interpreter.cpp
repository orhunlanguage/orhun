#include "Interpreter.h"
#include "Lexer.h"
#include "Parser.h"

#include "DynamicLibrary.h"
#include "Yardimci.h"
#include "Yerlesik.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace {
// İşlev içinde döndür yakalamak için iç kontrol sinyali.
struct DondurSinyali {
  explicit DondurSinyali(OrhunDegeri v) : deger(std::move(v)) {}
  OrhunDegeri deger;
};

// Döngü kontrol sinyalleri.
struct KirSinyali {};
struct DevamSinyali {};

std::string yakalamaMesajiTemizle(const std::string &mesaj) {
  const std::string etiket = "\n\nStack Trace:";
  const std::size_t konum = mesaj.find(etiket);
  if (konum == std::string::npos) {
    return mesaj;
  }
  return mesaj.substr(0, konum);
}

void adayEkleTekil(std::vector<std::string> &adaylar,
                   std::unordered_set<std::string> &gorulen,
                   const std::string &aday) {
  if (aday.empty()) {
    return;
  }
  if (gorulen.insert(aday).second) {
    adaylar.push_back(aday);
  }
}

std::string oneriliMesaj(const std::string &mesaj, const std::string &aranan,
                         const std::vector<std::string> &adaylar) {
  if (aranan.empty()) {
    return mesaj;
  }
  const std::size_t maxMesafe =
      utf8KodNoktalarinaCevir(aranan).size() >= 7 ? 3 : 2;
  const auto oneri = enYakinOneri(aranan, adaylar, maxMesafe);
  if (!oneri.has_value()) {
    return mesaj;
  }
  return mesaj + " Bunu mu demek istediniz: '" + oneri.value() + "'?";
}

std::vector<std::string> sabitMetinMetodAdaylari() {
  return {"buyuk", "kucuk", "parcala", "uzunluk"};
}

std::vector<std::string> sabitListeMetodAdaylari() {
  return {"ekle", "sil", "uzunluk"};
}

std::vector<std::string> sabitSozlukMetodAdaylari() {
  return {"anahtarlar", "degerler", "sil", "uzunluk"};
}

std::vector<std::string> noktaIleBol(const std::string &metin) {
  std::vector<std::string> parcalar;
  std::size_t bas = 0;
  while (true) {
    const std::size_t nokta = metin.find('.', bas);
    if (nokta == std::string::npos) {
      parcalar.push_back(metin.substr(bas));
      break;
    }
    parcalar.push_back(metin.substr(bas, nokta - bas));
    bas = nokta + 1;
  }
  return parcalar;
}

std::string kodNoktasiUtf8(char32_t cp) {
  std::string sonuc;
  if (cp <= 0x7F) {
    sonuc.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    sonuc.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
    sonuc.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    sonuc.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
    sonuc.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    sonuc.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  return sonuc;
}

bool ortamDegiskeniAcik(const char *ad) {
  const char *v = std::getenv(ad);
  if (v == nullptr) {
    return false;
  }
  std::string s(v);
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return !(s.empty() || s == "0" || s == "false" || s == "off" ||
           s == "hayir" || s == "no");
}

bool sistemKomutuKisitliModDisi() {
  return ortamDegiskeniAcik("ORHUN_UNSAFE") ||
         ortamDegiskeniAcik("ORHUN_SYSTEM_UNSAFE");
}

bool sistemKomutuGuvenliMi(const std::string &komut) {
  if (komut.empty()) {
    return false;
  }
  for (char c : komut) {
    if (c == '&' || c == '|' || c == ';' || c == '<' || c == '>' || c == '`' ||
        c == '$' || c == '\n' || c == '\r' || c == '"' || c == '\'') {
      return false;
    }
  }
  return true;
}

std::string asciiKucult(std::string metin) {
  std::transform(metin.begin(), metin.end(), metin.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  return metin;
}

std::string metniKirp(std::string metin) {
  std::size_t sol = 0;
  while (sol < metin.size() &&
         std::isspace(static_cast<unsigned char>(metin[sol]))) {
    ++sol;
  }
  std::size_t sag = metin.size();
  while (sag > sol &&
         std::isspace(static_cast<unsigned char>(metin[sag - 1]))) {
    --sag;
  }
  return metin.substr(sol, sag - sol);
}

enum class FFIPolitikasi { Off, Allowlist, Full };

std::string releaseKanaliniBul() {
  const char *kanal = std::getenv("ORHUN_CHANNEL");
  if (kanal == nullptr || *kanal == '\0') {
    kanal = std::getenv("ORHUN_RELEASE_CHANNEL");
  }
  if (kanal == nullptr || *kanal == '\0') {
    return "stable";
  }
  return asciiKucult(kanal);
}

FFIPolitikasi ffiPolitikasiBul() {
  const char *env = std::getenv("ORHUN_FFI_POLICY");
  if (env != nullptr && *env != '\0') {
    const std::string politika = asciiKucult(metniKirp(env));
    if (politika == "off") {
      return FFIPolitikasi::Off;
    }
    if (politika == "allowlist") {
      return FFIPolitikasi::Allowlist;
    }
    if (politika == "full") {
      return FFIPolitikasi::Full;
    }
    throw std::runtime_error(
        "ORHUN_FFI_POLICY yalnizca off|allowlist|full degerini alir.");
  }
  if (releaseKanaliniBul() == "stable") {
    return FFIPolitikasi::Allowlist;
  }
  return FFIPolitikasi::Full;
}

std::unordered_set<std::string> ffiAllowlistiniBul() {
  std::unordered_set<std::string> izinliler = {
      "kernel32.dll",          "user32.dll",
      "advapi32.dll",          "ws2_32.dll",
      "libc.so.6",             "libm.so.6",
      "libdl.so.2",            "libpthread.so.0",
      "libsystem.b.dylib",     "/usr/lib/libsystem.b.dylib",
      "/usr/lib/libsystem.dylib"};
  if (const char *env = std::getenv("ORHUN_FFI_ALLOWLIST")) {
    std::istringstream ak(env);
    for (std::string parca; std::getline(ak, parca, ',');) {
      const std::string trim = asciiKucult(metniKirp(parca));
      if (!trim.empty()) {
        izinliler.insert(trim);
      }
    }
  }
  return izinliler;
}

bool ffiYolAllowlistteMi(const std::string &yol,
                         const std::unordered_set<std::string> &izinliler) {
  const std::string norm = asciiKucult(metniKirp(yol));
  if (norm.empty()) {
    return false;
  }
  if (izinliler.find(norm) != izinliler.end()) {
    return true;
  }
  const std::filesystem::path p(norm);
  const std::string dosyaAdi = p.filename().string();
  return !dosyaAdi.empty() && izinliler.find(dosyaAdi) != izinliler.end();
}

bool jsonTamsayiMi(double d) { return std::isfinite(d) && std::floor(d) == d; }

yerlesik::JsonDeger orhunDegerindenYerlesikJson(const OrhunDegeri &deger) {
  if (const auto *tam = std::get_if<int>(&deger.veri)) {
    return yerlesik::JsonDeger(static_cast<double>(*tam));
  }
  if (const auto *ondalik = std::get_if<double>(&deger.veri)) {
    return yerlesik::JsonDeger(*ondalik);
  }
  if (const auto *metin = std::get_if<std::string>(&deger.veri)) {
    return yerlesik::JsonDeger(*metin);
  }
  if (const auto *listePtr = std::get_if<OrhunDegeri::ListeTipi>(&deger.veri)) {
    yerlesik::JsonDeger::Liste liste;
    if (*listePtr) {
      liste.reserve((*listePtr)->size());
      for (const OrhunDegeri &oge : *(*listePtr)) {
        liste.push_back(orhunDegerindenYerlesikJson(oge));
      }
    }
    return yerlesik::JsonDeger(std::move(liste));
  }
  if (const auto *sozlukPtr =
          std::get_if<OrhunDegeri::SozlukTipi>(&deger.veri)) {
    yerlesik::JsonDeger::Sozluk sozluk;
    if (*sozlukPtr) {
      for (const auto &[anahtar, alt] : *(*sozlukPtr)) {
        sozluk[anahtar] = orhunDegerindenYerlesikJson(alt);
      }
    }
    return yerlesik::JsonDeger(std::move(sozluk));
  }
  if (const auto *nesnePtr = std::get_if<OrhunDegeri::NesneTipi>(&deger.veri)) {
    if (!(*nesnePtr) || !(*nesnePtr)->alanlar) {
      return yerlesik::JsonDeger(nullptr);
    }
    yerlesik::JsonDeger::Sozluk sozluk;
    sozluk["__sinif"] = yerlesik::JsonDeger((*nesnePtr)->sinifAdi);
    for (const auto &[anahtar, alt] : *(*nesnePtr)->alanlar) {
      sozluk[anahtar] = orhunDegerindenYerlesikJson(alt);
    }
    return yerlesik::JsonDeger(std::move(sozluk));
  }
  return yerlesik::JsonDeger(nullptr);
}

OrhunDegeri yerlesikJsondanOrhunDegere(const yerlesik::JsonDeger &deger) {
  if (std::holds_alternative<std::nullptr_t>(deger.veri)) {
    return OrhunDegeri(0);
  }
  if (const auto *m = std::get_if<bool>(&deger.veri)) {
    return OrhunDegeri(*m ? 1 : 0);
  }
  if (const auto *s = std::get_if<double>(&deger.veri)) {
    if (jsonTamsayiMi(*s) &&
        *s >= static_cast<double>(std::numeric_limits<int>::min()) &&
        *s <= static_cast<double>(std::numeric_limits<int>::max())) {
      return OrhunDegeri(static_cast<int>(*s));
    }
    return OrhunDegeri(*s);
  }
  if (const auto *metin = std::get_if<std::string>(&deger.veri)) {
    return OrhunDegeri(*metin);
  }
  if (const auto *listePtr =
          std::get_if<yerlesik::JsonDeger::ListePtr>(&deger.veri)) {
    OrhunDegeri::ListeVeri liste;
    if (*listePtr) {
      liste.reserve((*listePtr)->size());
      for (const auto &oge : *(*listePtr)) {
        liste.push_back(yerlesikJsondanOrhunDegere(oge));
      }
    }
    return OrhunDegeri(std::move(liste));
  }
  const auto *sozlukPtr =
      std::get_if<yerlesik::JsonDeger::SozlukPtr>(&deger.veri);
  OrhunDegeri::SozlukVeri sozluk;
  if (sozlukPtr && *sozlukPtr) {
    for (const auto &[anahtar, alt] : *(*sozlukPtr)) {
      sozluk[anahtar] = yerlesikJsondanOrhunDegere(alt);
    }
  }
  return OrhunDegeri(std::move(sozluk));
}

class JsonCozucu {
public:
  explicit JsonCozucu(std::string metin) : metin_(std::move(metin)) {}

  OrhunDegeri coz() {
    bosluklariAtla();
    OrhunDegeri sonuc = degerCoz();
    bosluklariAtla();
    if (!sonaGeldim()) {
      hata("Beklenmeyen fazladan karakter");
    }
    return sonuc;
  }

private:
  std::string metin_;
  std::size_t konum_ = 0;

  bool sonaGeldim() const { return konum_ >= metin_.size(); }

  char bak() const { return sonaGeldim() ? '\0' : metin_[konum_]; }

  char ilerle() { return sonaGeldim() ? '\0' : metin_[konum_++]; }

  bool eslesir(char hedef) {
    if (bak() == hedef) {
      ++konum_;
      return true;
    }
    return false;
  }

  [[noreturn]] void hata(const std::string &mesaj) const {
    throw std::runtime_error("JSON hatası (konum " + std::to_string(konum_) +
                             "): " + mesaj);
  }

  void bosluklariAtla() {
    while (!sonaGeldim()) {
      const unsigned char c = static_cast<unsigned char>(bak());
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        ++konum_;
        continue;
      }
      break;
    }
  }

  bool literalEslesir(const char *literal) {
    const std::size_t bas = konum_;
    for (std::size_t i = 0; literal[i] != '\0'; ++i) {
      if (sonaGeldim() || metin_[konum_] != literal[i]) {
        konum_ = bas;
        return false;
      }
      ++konum_;
    }
    return true;
  }

  std::string metinCoz() {
    if (!eslesir('"')) {
      hata("Metin için açılış tırnağı bekleniyordu");
    }

    std::string sonuc;
    while (!sonaGeldim()) {
      const char c = ilerle();
      if (c == '"') {
        return sonuc;
      }

      if (c != '\\') {
        sonuc.push_back(c);
        continue;
      }

      if (sonaGeldim()) {
        hata("Kaçış dizisi eksik");
      }

      const char k = ilerle();
      switch (k) {
      case '"':
        sonuc.push_back('"');
        break;
      case '\\':
        sonuc.push_back('\\');
        break;
      case '/':
        sonuc.push_back('/');
        break;
      case 'b':
        sonuc.push_back('\b');
        break;
      case 'f':
        sonuc.push_back('\f');
        break;
      case 'n':
        sonuc.push_back('\n');
        break;
      case 'r':
        sonuc.push_back('\r');
        break;
      case 't':
        sonuc.push_back('\t');
        break;
      case 'u': {
        if (konum_ + 4 > metin_.size()) {
          hata("\\u kaçışında 4 hex karakter bekleniyordu");
        }

        int cp = 0;
        for (int i = 0; i < 4; ++i) {
          const char h = metin_[konum_++];
          cp <<= 4;
          if (h >= '0' && h <= '9') {
            cp += h - '0';
          } else if (h >= 'a' && h <= 'f') {
            cp += 10 + (h - 'a');
          } else if (h >= 'A' && h <= 'F') {
            cp += 10 + (h - 'A');
          } else {
            hata("\\u kaçışında geçersiz hex karakter");
          }
        }
        sonuc += kodNoktasiUtf8(static_cast<char32_t>(cp));
        break;
      }
      default:
        hata("Geçersiz kaçış dizisi");
      }
    }

    hata("Kapanmayan JSON metin değeri");
  }

  OrhunDegeri sayiCoz() {
    const std::size_t baslangic = konum_;
    bool ondalik = false;

    if (eslesir('-')) {
      // Bilerek boş.
    }

    if (eslesir('0')) {
      // 0 ile başlayan sayı.
    } else {
      if (!(bak() >= '1' && bak() <= '9')) {
        hata("Geçersiz sayı");
      }
      while (bak() >= '0' && bak() <= '9') {
        ++konum_;
      }
    }

    if (eslesir('.')) {
      ondalik = true;
      if (!(bak() >= '0' && bak() <= '9')) {
        hata("Ondalık noktadan sonra en az bir rakam bekleniyordu");
      }
      while (bak() >= '0' && bak() <= '9') {
        ++konum_;
      }
    }

    if (bak() == 'e' || bak() == 'E') {
      ondalik = true;
      ++konum_;
      if (bak() == '+' || bak() == '-') {
        ++konum_;
      }
      if (!(bak() >= '0' && bak() <= '9')) {
        hata("Üs gösteriminde sayı bekleniyordu");
      }
      while (bak() >= '0' && bak() <= '9') {
        ++konum_;
      }
    }

    const std::string parca = metin_.substr(baslangic, konum_ - baslangic);
    char *bitis = nullptr;
    const double d = std::strtod(parca.c_str(), &bitis);
    if (bitis == nullptr || *bitis != '\0') {
      hata("Sayı çözümlenemedi");
    }

    if (!ondalik && d >= static_cast<double>(std::numeric_limits<int>::min()) &&
        d <= static_cast<double>(std::numeric_limits<int>::max())) {
      return OrhunDegeri(static_cast<int>(d));
    }
    return OrhunDegeri(d);
  }

  OrhunDegeri listeCoz() {
    if (!eslesir('[')) {
      hata("Liste için '[' bekleniyordu");
    }

    OrhunDegeri::ListeVeri liste;
    bosluklariAtla();
    if (eslesir(']')) {
      return OrhunDegeri(std::move(liste));
    }

    while (true) {
      liste.push_back(degerCoz());
      bosluklariAtla();
      if (eslesir(']')) {
        return OrhunDegeri(std::move(liste));
      }
      if (!eslesir(',')) {
        hata("Listede ',' veya ']' bekleniyordu");
      }
      bosluklariAtla();
    }
  }

  OrhunDegeri sozlukCoz() {
    if (!eslesir('{')) {
      hata("Sözlük için '{' bekleniyordu");
    }

    OrhunDegeri::SozlukVeri sozluk;
    bosluklariAtla();
    if (eslesir('}')) {
      return OrhunDegeri(std::move(sozluk));
    }

    while (true) {
      bosluklariAtla();
      if (bak() != '"') {
        hata("Sözlük anahtarı metin olmalıdır");
      }
      const std::string anahtar = metinCoz();
      bosluklariAtla();
      if (!eslesir(':')) {
        hata("Sözlükte anahtardan sonra ':' bekleniyordu");
      }

      OrhunDegeri deger = degerCoz();
      sozluk[anahtar] = std::move(deger);

      bosluklariAtla();
      if (eslesir('}')) {
        return OrhunDegeri(std::move(sozluk));
      }
      if (!eslesir(',')) {
        hata("Sözlükte ',' veya '}' bekleniyordu");
      }
      bosluklariAtla();
    }
  }

  OrhunDegeri degerCoz() {
    bosluklariAtla();
    const char c = bak();
    if (c == '"') {
      return OrhunDegeri(metinCoz());
    }
    if (c == '{') {
      return sozlukCoz();
    }
    if (c == '[') {
      return listeCoz();
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      return sayiCoz();
    }
    if (literalEslesir("true")) {
      return OrhunDegeri(1);
    }
    if (literalEslesir("false")) {
      return OrhunDegeri(0);
    }
    if (literalEslesir("null")) {
      return OrhunDegeri(0);
    }
    hata("Geçersiz JSON değer başlangıcı");
  }
};

std::string kirpilmisKopya(const std::string &metin) {
  std::size_t sol = 0;
  while (sol < metin.size() &&
         std::isspace(static_cast<unsigned char>(metin[sol])) != 0) {
    ++sol;
  }

  std::size_t sag = metin.size();
  while (sag > sol &&
         std::isspace(static_cast<unsigned char>(metin[sag - 1])) != 0) {
    --sag;
  }
  return metin.substr(sol, sag - sol);
}

bool yerTutucuParcaGecerliMi(const std::string &parca) {
  if (parca.empty()) {
    return false;
  }

  auto baslangicGecerli = [](unsigned char c) {
    return c == '_' || std::isalpha(c) != 0 || c >= 128;
  };
  auto govdeGecerli = [](unsigned char c) {
    return c == '_' || std::isalnum(c) != 0 || c >= 128;
  };

  if (!baslangicGecerli(static_cast<unsigned char>(parca.front()))) {
    return false;
  }
  for (std::size_t i = 1; i < parca.size(); ++i) {
    if (!govdeGecerli(static_cast<unsigned char>(parca[i]))) {
      return false;
    }
  }
  return true;
}

bool yerTutucuYoluGecerliMi(const std::string &yol) {
  if (yol.empty()) {
    return false;
  }

  std::size_t bas = 0;
  while (true) {
    const std::size_t nokta = yol.find('.', bas);
    const std::string parca = nokta == std::string::npos
                                  ? yol.substr(bas)
                                  : yol.substr(bas, nokta - bas);
    if (!yerTutucuParcaGecerliMi(parca)) {
      return false;
    }
    if (nokta == std::string::npos) {
      break;
    }
    bas = nokta + 1;
  }
  return true;
}

std::string jsonMetniKacisla(const std::string &metin) {
  std::string sonuc;
  sonuc.reserve(metin.size() + 8);
  for (unsigned char c : metin) {
    switch (c) {
    case '\\':
      sonuc += "\\\\";
      break;
    case '"':
      sonuc += "\\\"";
      break;
    case '\b':
      sonuc += "\\b";
      break;
    case '\f':
      sonuc += "\\f";
      break;
    case '\n':
      sonuc += "\\n";
      break;
    case '\r':
      sonuc += "\\r";
      break;
    case '\t':
      sonuc += "\\t";
      break;
    default:
      if (c < 0x20) {
        const char *hex = "0123456789ABCDEF";
        sonuc += "\\u00";
        sonuc.push_back(hex[(c >> 4) & 0x0F]);
        sonuc.push_back(hex[c & 0x0F]);
      } else {
        sonuc.push_back(static_cast<char>(c));
      }
      break;
    }
  }
  return sonuc;
}

std::string orhunDegeriniJsonaCevir(const OrhunDegeri &deger) {
  if (const auto *tam = std::get_if<int>(&deger.veri)) {
    return std::to_string(*tam);
  }
  if (const auto *ondalik = std::get_if<double>(&deger.veri)) {
    std::ostringstream oss;
    oss << *ondalik;
    return oss.str();
  }
  if (const auto *metin = std::get_if<std::string>(&deger.veri)) {
    return "\"" + jsonMetniKacisla(*metin) + "\"";
  }
  if (const auto *listePtr = std::get_if<OrhunDegeri::ListeTipi>(&deger.veri)) {
    std::string json = "[";
    const OrhunDegeri::ListeVeri bosListe;
    const auto &liste = (*listePtr) ? *(*listePtr) : bosListe;
    for (std::size_t i = 0; i < liste.size(); ++i) {
      if (i > 0) {
        json += ",";
      }
      json += orhunDegeriniJsonaCevir(liste[i]);
    }
    json += "]";
    return json;
  }
  if (const auto *sozlukPtr =
          std::get_if<OrhunDegeri::SozlukTipi>(&deger.veri)) {
    std::string json = "{";
    const OrhunDegeri::SozlukVeri bosSozluk;
    const auto &sozluk = (*sozlukPtr) ? *(*sozlukPtr) : bosSozluk;
    bool ilk = true;
    for (const auto &[anahtar, altDeger] : sozluk) {
      if (!ilk) {
        json += ",";
      }
      ilk = false;
      json += "\"" + jsonMetniKacisla(anahtar) + "\":";
      json += orhunDegeriniJsonaCevir(altDeger);
    }
    json += "}";
    return json;
  }
  if (const auto *nesnePtr = std::get_if<OrhunDegeri::NesneTipi>(&deger.veri)) {
    if (!(*nesnePtr) || !(*nesnePtr)->alanlar) {
      return "null";
    }

    std::string json = "{";
    bool ilk = true;
    json += "\"__sinif\":\"" + jsonMetniKacisla((*nesnePtr)->sinifAdi) + "\"";
    ilk = false;
    for (const auto &[anahtar, altDeger] : *(*nesnePtr)->alanlar) {
      if (!ilk) {
        json += ",";
      }
      ilk = false;
      json += "\"" + jsonMetniKacisla(anahtar) + "\":";
      json += orhunDegeriniJsonaCevir(altDeger);
    }
    json += "}";
    return json;
  }

  return "null";
}

std::string jsonGirintiUret(int seviye, int adim) {
  if (seviye <= 0 || adim <= 0) {
    return std::string();
  }
  return std::string(static_cast<std::size_t>(seviye * adim), ' ');
}

std::string orhunDegeriniJsonaGuzelCevir(const OrhunDegeri &deger, int adim,
                                         int seviye) {
  if (const auto *tam = std::get_if<int>(&deger.veri)) {
    return std::to_string(*tam);
  }
  if (const auto *ondalik = std::get_if<double>(&deger.veri)) {
    std::ostringstream oss;
    oss << *ondalik;
    return oss.str();
  }
  if (const auto *metin = std::get_if<std::string>(&deger.veri)) {
    return "\"" + jsonMetniKacisla(*metin) + "\"";
  }
  if (const auto *listePtr = std::get_if<OrhunDegeri::ListeTipi>(&deger.veri)) {
    const OrhunDegeri::ListeVeri bosListe;
    const auto &liste = (*listePtr) ? *(*listePtr) : bosListe;
    if (liste.empty()) {
      return "[]";
    }

    std::string json = "[\n";
    for (std::size_t i = 0; i < liste.size(); ++i) {
      json += jsonGirintiUret(seviye + 1, adim);
      json += orhunDegeriniJsonaGuzelCevir(liste[i], adim, seviye + 1);
      if (i + 1 < liste.size()) {
        json += ",";
      }
      json += "\n";
    }
    json += jsonGirintiUret(seviye, adim) + "]";
    return json;
  }
  if (const auto *sozlukPtr =
          std::get_if<OrhunDegeri::SozlukTipi>(&deger.veri)) {
    const OrhunDegeri::SozlukVeri bosSozluk;
    const auto &sozluk = (*sozlukPtr) ? *(*sozlukPtr) : bosSozluk;
    if (sozluk.empty()) {
      return "{}";
    }

    std::string json = "{\n";
    std::size_t i = 0;
    for (const auto &[anahtar, altDeger] : sozluk) {
      json += jsonGirintiUret(seviye + 1, adim);
      json += "\"" + jsonMetniKacisla(anahtar) + "\": ";
      json += orhunDegeriniJsonaGuzelCevir(altDeger, adim, seviye + 1);
      if (i + 1 < sozluk.size()) {
        json += ",";
      }
      json += "\n";
      ++i;
    }
    json += jsonGirintiUret(seviye, adim) + "}";
    return json;
  }
  if (const auto *nesnePtr = std::get_if<OrhunDegeri::NesneTipi>(&deger.veri)) {
    if (!(*nesnePtr) || !(*nesnePtr)->alanlar) {
      return "null";
    }
    return orhunDegeriniJsonaGuzelCevir(OrhunDegeri(*(*nesnePtr)->alanlar),
                                        adim, seviye);
  }

  return "null";
}

#ifdef _WIN32
std::wstring utf8denWstringe(const std::string &metin) {
  if (metin.empty()) {
    return std::wstring();
  }
  const int gerekli = MultiByteToWideChar(
      CP_UTF8, 0, metin.c_str(), static_cast<int>(metin.size()), nullptr, 0);
  if (gerekli <= 0) {
    throw std::runtime_error("UTF-8 -> UTF-16 dönüşümü başarısız.");
  }
  std::wstring sonuc(static_cast<std::size_t>(gerekli), L'\0');
  const int yazilan = MultiByteToWideChar(CP_UTF8, 0, metin.c_str(),
                                          static_cast<int>(metin.size()),
                                          sonuc.data(), gerekli);
  if (yazilan <= 0) {
    throw std::runtime_error("UTF-8 -> UTF-16 dönüşümü başarısız.");
  }
  return sonuc;
}

std::string windowsHataMesaji(DWORD kod) {
  LPSTR tampon = nullptr;
  const DWORD uzunluk = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, kod, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPSTR>(&tampon), 0, nullptr);

  if (uzunluk == 0 || tampon == nullptr) {
    return "Win32 hata kodu: " + std::to_string(kod);
  }

  std::string mesaj(tampon, uzunluk);
  LocalFree(tampon);
  while (!mesaj.empty() && (mesaj.back() == '\n' || mesaj.back() == '\r' ||
                            mesaj.back() == ' ' || mesaj.back() == '\t')) {
    mesaj.pop_back();
  }
  return mesaj;
}

LRESULT CALLBACK orhunGrafikPencereProc(HWND hwnd, UINT mesaj, WPARAM wparam,
                                        LPARAM lparam) {
  switch (mesaj) {
  case WM_CLOSE:
    DestroyWindow(hwnd);
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    return DefWindowProcW(hwnd, mesaj, wparam, lparam);
  }
}

struct GrafikDurumu {
  bool sinifKayitli = false;
  std::wstring sinifAdi = L"OrhunGrafikSinifi";
  HWND pencere = nullptr;
  HDC cizimAlani = nullptr;
};

GrafikDurumu &grafikDurumu() {
  static GrafikDurumu durum;
  return durum;
}

struct GdiApi {
  using CreateSolidBrushFn = HBRUSH(WINAPI *)(COLORREF);
  using DeleteObjectFn = BOOL(WINAPI *)(HGDIOBJ);
  using CreatePenFn = HPEN(WINAPI *)(int, int, COLORREF);
  using SelectObjectFn = HGDIOBJ(WINAPI *)(HDC, HGDIOBJ);
  using MoveToExFn = BOOL(WINAPI *)(HDC, int, int, LPPOINT);
  using LineToFn = BOOL(WINAPI *)(HDC, int, int);
  using SetBkModeFn = int(WINAPI *)(HDC, int);
  using SetTextColorFn = COLORREF(WINAPI *)(HDC, COLORREF);
  using TextOutWFn = BOOL(WINAPI *)(HDC, int, int, LPCWSTR, int);

  HMODULE kutuphane = nullptr;
  CreateSolidBrushFn createSolidBrush = nullptr;
  DeleteObjectFn deleteObject = nullptr;
  CreatePenFn createPen = nullptr;
  SelectObjectFn selectObject = nullptr;
  MoveToExFn moveToEx = nullptr;
  LineToFn lineTo = nullptr;
  SetBkModeFn setBkMode = nullptr;
  SetTextColorFn setTextColor = nullptr;
  TextOutWFn textOutW = nullptr;

  GdiApi() {
    kutuphane = LoadLibraryW(L"gdi32.dll");
    if (!kutuphane) {
      throw std::runtime_error("gdi32.dll yüklenemedi: " +
                               windowsHataMesaji(GetLastError()));
    }

    try {
      yukle(createSolidBrush, "CreateSolidBrush");
      yukle(deleteObject, "DeleteObject");
      yukle(createPen, "CreatePen");
      yukle(selectObject, "SelectObject");
      yukle(moveToEx, "MoveToEx");
      yukle(lineTo, "LineTo");
      yukle(setBkMode, "SetBkMode");
      yukle(setTextColor, "SetTextColor");
      yukle(textOutW, "TextOutW");
    } catch (...) {
      FreeLibrary(kutuphane);
      kutuphane = nullptr;
      throw;
    }
  }

  ~GdiApi() {
    if (kutuphane != nullptr) {
      FreeLibrary(kutuphane);
      kutuphane = nullptr;
    }
  }

private:
  template <typename T> void yukle(T &hedef, const char *ad) {
    FARPROC ham = GetProcAddress(kutuphane, ad);
    if (!ham) {
      throw std::runtime_error(std::string("GDI sembolü yüklenemedi: ") + ad);
    }
    static_assert(sizeof(hedef) == sizeof(ham),
                  "GDI fonksiyon işaretçisi boyutu FARPROC ile uyumsuz.");
    std::memcpy(&hedef, &ham, sizeof(hedef));
  }
};

GdiApi &gdiApi() {
  static GdiApi api;
  return api;
}

void grafikMesajlariIsle() {
  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

void grafikSinifiniKaydet() {
  GrafikDurumu &durum = grafikDurumu();
  if (durum.sinifKayitli) {
    return;
  }

  WNDCLASSEXW wc;
  std::memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = orhunGrafikPencereProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = durum.sinifAdi.c_str();
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

  if (RegisterClassExW(&wc) == 0) {
    throw std::runtime_error(
        "grafik.pencere_ac: pencere sınıfı kaydı başarısız (" +
        windowsHataMesaji(GetLastError()) + ")");
  }
  durum.sinifKayitli = true;
}

void grafikKapat() {
  GrafikDurumu &durum = grafikDurumu();
  if (durum.cizimAlani != nullptr && durum.pencere != nullptr) {
    ReleaseDC(durum.pencere, durum.cizimAlani);
    durum.cizimAlani = nullptr;
  }
  if (durum.pencere != nullptr) {
    DestroyWindow(durum.pencere);
    durum.pencere = nullptr;
  }
}

void grafikPencereAc(const std::wstring &baslik, int genislik, int yukseklik) {
  grafikSinifiniKaydet();
  grafikKapat();

  GrafikDurumu &durum = grafikDurumu();
  HWND pencere = CreateWindowExW(
      0, durum.sinifAdi.c_str(), baslik.c_str(),
      WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
      std::max(genislik, 200), std::max(yukseklik, 150), nullptr, nullptr,
      GetModuleHandleW(nullptr), nullptr);

  if (pencere == nullptr) {
    throw std::runtime_error("grafik.pencere_ac başarısız: " +
                             windowsHataMesaji(GetLastError()));
  }

  HDC hdc = GetDC(pencere);
  if (hdc == nullptr) {
    DestroyWindow(pencere);
    throw std::runtime_error("grafik.pencere_ac çizim alanı alınamadı: " +
                             windowsHataMesaji(GetLastError()));
  }

  durum.pencere = pencere;
  durum.cizimAlani = hdc;
  ShowWindow(durum.pencere, SW_SHOW);
  UpdateWindow(durum.pencere);
}

void grafikPencereKontrol() {
  GrafikDurumu &durum = grafikDurumu();
  if (durum.pencere == nullptr || durum.cizimAlani == nullptr) {
    throw std::runtime_error(
        "Önce grafik.pencere_ac(\"Baslik\", genislik, yukseklik) çağırılmalı.");
  }
}

COLORREF rgbRenk(int r, int g, int b) {
  return RGB(std::clamp(r, 0, 255), std::clamp(g, 0, 255),
             std::clamp(b, 0, 255));
}

void grafikTemizle(int r, int g, int b) {
  grafikPencereKontrol();
  GrafikDurumu &durum = grafikDurumu();
  GdiApi &api = gdiApi();

  RECT alan;
  GetClientRect(durum.pencere, &alan);
  HBRUSH firca = api.createSolidBrush(rgbRenk(r, g, b));
  FillRect(durum.cizimAlani, &alan, firca);
  api.deleteObject(firca);
}

void grafikDikdortgen(int x, int y, int genislik, int yukseklik, int r, int g,
                      int b) {
  grafikPencereKontrol();
  GrafikDurumu &durum = grafikDurumu();
  GdiApi &api = gdiApi();

  RECT rc{x, y, x + std::max(genislik, 0), y + std::max(yukseklik, 0)};
  HBRUSH firca = api.createSolidBrush(rgbRenk(r, g, b));
  FillRect(durum.cizimAlani, &rc, firca);
  api.deleteObject(firca);
}

void grafikCizgi(int x1, int y1, int x2, int y2, int r, int g, int b) {
  grafikPencereKontrol();
  GrafikDurumu &durum = grafikDurumu();
  GdiApi &api = gdiApi();

  HPEN kalem = api.createPen(PS_SOLID, 1, rgbRenk(r, g, b));
  HGDIOBJ eski = api.selectObject(durum.cizimAlani, kalem);
  api.moveToEx(durum.cizimAlani, x1, y1, nullptr);
  api.lineTo(durum.cizimAlani, x2, y2);
  api.selectObject(durum.cizimAlani, eski);
  api.deleteObject(kalem);
}

void grafikYazi(const std::wstring &metin, int x, int y, int r, int g, int b) {
  grafikPencereKontrol();
  GrafikDurumu &durum = grafikDurumu();
  GdiApi &api = gdiApi();

  api.setBkMode(durum.cizimAlani, TRANSPARENT);
  api.setTextColor(durum.cizimAlani, rgbRenk(r, g, b));
  api.textOutW(durum.cizimAlani, x, y, metin.c_str(),
               static_cast<int>(metin.size()));
}

void grafikGuncelle() {
  grafikMesajlariIsle();
  GrafikDurumu &durum = grafikDurumu();
  if (durum.pencere != nullptr) {
    UpdateWindow(durum.pencere);
  }
}

void grafikBekle(int milisaniye) {
  const int adim = 10;
  int kalan = std::max(0, milisaniye);
  while (kalan > 0) {
    grafikMesajlariIsle();
    const int bekle = std::min(adim, kalan);
    Sleep(static_cast<DWORD>(bekle));
    kalan -= bekle;
  }
}
#endif

#if defined(_WIN32)
#define ORHUN_FFI_CALLCONV WINAPI
#else
#define ORHUN_FFI_CALLCONV
#endif

template <typename T> T sembolDonustur(std::uintptr_t ham) {
  T sonuc{};
  static_assert(sizeof(sonuc) == sizeof(ham),
                "Fonksiyon isaretcisi boyutu uyusmuyor.");
  std::memcpy(&sonuc, &ham, sizeof(sonuc));
  return sonuc;
}

std::intptr_t ffiHamCagir(std::uintptr_t fonksiyon,
                          const std::vector<std::intptr_t> &argumanlar) {
  switch (argumanlar.size()) {
  case 0: {
    using Fn = std::intptr_t(ORHUN_FFI_CALLCONV *)();
    return sembolDonustur<Fn>(fonksiyon)();
  }
  case 1: {
    using Fn = std::intptr_t(ORHUN_FFI_CALLCONV *)(std::intptr_t);
    return sembolDonustur<Fn>(fonksiyon)(argumanlar[0]);
  }
  case 2: {
    using Fn =
        std::intptr_t(ORHUN_FFI_CALLCONV *)(std::intptr_t, std::intptr_t);
    return sembolDonustur<Fn>(fonksiyon)(argumanlar[0], argumanlar[1]);
  }
  case 3: {
    using Fn = std::intptr_t(ORHUN_FFI_CALLCONV *)(std::intptr_t, std::intptr_t,
                                                   std::intptr_t);
    return sembolDonustur<Fn>(fonksiyon)(argumanlar[0], argumanlar[1],
                                         argumanlar[2]);
  }
  case 4: {
    using Fn = std::intptr_t(ORHUN_FFI_CALLCONV *)(
        std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t);
    return sembolDonustur<Fn>(fonksiyon)(argumanlar[0], argumanlar[1],
                                         argumanlar[2], argumanlar[3]);
  }
  case 5: {
    using Fn = std::intptr_t(ORHUN_FFI_CALLCONV *)(std::intptr_t, std::intptr_t,
                                                   std::intptr_t, std::intptr_t,
                                                   std::intptr_t);
    return sembolDonustur<Fn>(fonksiyon)(argumanlar[0], argumanlar[1],
                                         argumanlar[2], argumanlar[3],
                                         argumanlar[4]);
  }
  case 6: {
    using Fn = std::intptr_t(ORHUN_FFI_CALLCONV *)(
        std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t,
        std::intptr_t, std::intptr_t);
    return sembolDonustur<Fn>(fonksiyon)(argumanlar[0], argumanlar[1],
                                         argumanlar[2], argumanlar[3],
                                         argumanlar[4], argumanlar[5]);
  }
  case 7: {
    using Fn = std::intptr_t(ORHUN_FFI_CALLCONV *)(
        std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t,
        std::intptr_t, std::intptr_t, std::intptr_t);
    return sembolDonustur<Fn>(fonksiyon)(
        argumanlar[0], argumanlar[1], argumanlar[2], argumanlar[3],
        argumanlar[4], argumanlar[5], argumanlar[6]);
  }
  case 8: {
    using Fn = std::intptr_t(ORHUN_FFI_CALLCONV *)(
        std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t,
        std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t);
    return sembolDonustur<Fn>(fonksiyon)(
        argumanlar[0], argumanlar[1], argumanlar[2], argumanlar[3],
        argumanlar[4], argumanlar[5], argumanlar[6], argumanlar[7]);
  }
  default:
    throw std::runtime_error(
        "ffi.cagir simdilik en fazla 8 arguman destekliyor.");
  }
}

double ffiCiftCagir(std::uintptr_t fonksiyon,
                    const std::vector<double> &argumanlar) {
  switch (argumanlar.size()) {
  case 0: {
    using Fn = double(ORHUN_FFI_CALLCONV *)();
    return sembolDonustur<Fn>(fonksiyon)();
  }
  case 1: {
    using Fn = double(ORHUN_FFI_CALLCONV *)(double);
    return sembolDonustur<Fn>(fonksiyon)(argumanlar[0]);
  }
  case 2: {
    using Fn = double(ORHUN_FFI_CALLCONV *)(double, double);
    return sembolDonustur<Fn>(fonksiyon)(argumanlar[0], argumanlar[1]);
  }
  case 3: {
    using Fn = double(ORHUN_FFI_CALLCONV *)(double, double, double);
    return sembolDonustur<Fn>(fonksiyon)(argumanlar[0], argumanlar[1],
                                         argumanlar[2]);
  }
  case 4: {
    using Fn = double(ORHUN_FFI_CALLCONV *)(double, double, double, double);
    return sembolDonustur<Fn>(fonksiyon)(argumanlar[0], argumanlar[1],
                                         argumanlar[2], argumanlar[3]);
  }
  case 5: {
    using Fn =
        double(ORHUN_FFI_CALLCONV *)(double, double, double, double, double);
    return sembolDonustur<Fn>(fonksiyon)(argumanlar[0], argumanlar[1],
                                         argumanlar[2], argumanlar[3],
                                         argumanlar[4]);
  }
  case 6: {
    using Fn = double(ORHUN_FFI_CALLCONV *)(double, double, double, double,
                                            double, double);
    return sembolDonustur<Fn>(fonksiyon)(argumanlar[0], argumanlar[1],
                                         argumanlar[2], argumanlar[3],
                                         argumanlar[4], argumanlar[5]);
  }
  case 7: {
    using Fn = double(ORHUN_FFI_CALLCONV *)(double, double, double, double,
                                            double, double, double);
    return sembolDonustur<Fn>(fonksiyon)(
        argumanlar[0], argumanlar[1], argumanlar[2], argumanlar[3],
        argumanlar[4], argumanlar[5], argumanlar[6]);
  }
  case 8: {
    using Fn = double(ORHUN_FFI_CALLCONV *)(double, double, double, double,
                                            double, double, double, double);
    return sembolDonustur<Fn>(fonksiyon)(
        argumanlar[0], argumanlar[1], argumanlar[2], argumanlar[3],
        argumanlar[4], argumanlar[5], argumanlar[6], argumanlar[7]);
  }
  default:
    throw std::runtime_error(
        "ffi.cagir_tanimli simdilik en fazla 8 arguman destekliyor.");
  }
}

#ifdef _WIN32
struct WinHttpApi {
  using CrackUrlFn = BOOL(WINAPI *)(LPCWSTR, DWORD, DWORD, LPURL_COMPONENTS);
  using OpenFn = HINTERNET(WINAPI *)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
  using ConnectFn = HINTERNET(WINAPI *)(HINTERNET, LPCWSTR, INTERNET_PORT,
                                        DWORD);
  using OpenRequestFn = HINTERNET(WINAPI *)(HINTERNET, LPCWSTR, LPCWSTR,
                                            LPCWSTR, LPCWSTR, LPCWSTR const *,
                                            DWORD);
  using SetTimeoutsFn = BOOL(WINAPI *)(HINTERNET, int, int, int, int);
  using SendRequestFn = BOOL(WINAPI *)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD,
                                       DWORD, DWORD_PTR);
  using ReceiveResponseFn = BOOL(WINAPI *)(HINTERNET, LPVOID);
  using QueryHeadersFn = BOOL(WINAPI *)(HINTERNET, DWORD, LPCWSTR, LPVOID,
                                        LPDWORD, LPDWORD);
  using QueryDataAvailableFn = BOOL(WINAPI *)(HINTERNET, LPDWORD);
  using ReadDataFn = BOOL(WINAPI *)(HINTERNET, LPVOID, DWORD, LPDWORD);
  using CloseHandleFn = BOOL(WINAPI *)(HINTERNET);

  HMODULE kutuphane = nullptr;
  CrackUrlFn crackUrl = nullptr;
  OpenFn open = nullptr;
  ConnectFn connect = nullptr;
  OpenRequestFn openRequest = nullptr;
  SetTimeoutsFn setTimeouts = nullptr;
  SendRequestFn sendRequest = nullptr;
  ReceiveResponseFn receiveResponse = nullptr;
  QueryHeadersFn queryHeaders = nullptr;
  QueryDataAvailableFn queryDataAvailable = nullptr;
  ReadDataFn readData = nullptr;
  CloseHandleFn closeHandle = nullptr;

  WinHttpApi() {
    kutuphane = LoadLibraryW(L"winhttp.dll");
    if (!kutuphane) {
      throw std::runtime_error("winhttp.dll yüklenemedi: " +
                               windowsHataMesaji(GetLastError()));
    }
    try {
      yukle(crackUrl, "WinHttpCrackUrl");
      yukle(open, "WinHttpOpen");
      yukle(connect, "WinHttpConnect");
      yukle(openRequest, "WinHttpOpenRequest");
      yukle(setTimeouts, "WinHttpSetTimeouts");
      yukle(sendRequest, "WinHttpSendRequest");
      yukle(receiveResponse, "WinHttpReceiveResponse");
      yukle(queryHeaders, "WinHttpQueryHeaders");
      yukle(queryDataAvailable, "WinHttpQueryDataAvailable");
      yukle(readData, "WinHttpReadData");
      yukle(closeHandle, "WinHttpCloseHandle");
    } catch (...) {
      FreeLibrary(kutuphane);
      kutuphane = nullptr;
      throw;
    }
  }

  ~WinHttpApi() {
    if (kutuphane != nullptr) {
      FreeLibrary(kutuphane);
      kutuphane = nullptr;
    }
  }

private:
  template <typename T> void yukle(T &hedef, const char *ad) {
    FARPROC ham = GetProcAddress(kutuphane, ad);
    if (!ham) {
      throw std::runtime_error(std::string("WinHTTP sembolü yüklenemedi: ") +
                               ad);
    }
    static_assert(sizeof(hedef) == sizeof(ham),
                  "Fonksiyon imza boyutu FARPROC ile uyumsuz.");
    std::memcpy(&hedef, &ham, sizeof(hedef));
  }
};

WinHttpApi &winHttpApi() {
  static WinHttpApi api;
  return api;
}

[[maybe_unused]] std::string internetIcerigiGetir(const std::string &url) {
  WinHttpApi &api = winHttpApi();
  const std::wstring urlW = utf8denWstringe(url);

  URL_COMPONENTS parcalar;
  std::memset(&parcalar, 0, sizeof(parcalar));
  parcalar.dwStructSize = sizeof(parcalar);
  parcalar.dwSchemeLength = static_cast<DWORD>(-1);
  parcalar.dwHostNameLength = static_cast<DWORD>(-1);
  parcalar.dwUrlPathLength = static_cast<DWORD>(-1);
  parcalar.dwExtraInfoLength = static_cast<DWORD>(-1);

  if (!api.crackUrl(urlW.c_str(), static_cast<DWORD>(urlW.size()), 0,
                    &parcalar)) {
    throw std::runtime_error("URL çözümlenemedi: " +
                             windowsHataMesaji(GetLastError()));
  }

  const std::wstring host(parcalar.lpszHostName, parcalar.dwHostNameLength);
  std::wstring yol =
      parcalar.dwUrlPathLength > 0
          ? std::wstring(parcalar.lpszUrlPath, parcalar.dwUrlPathLength)
          : L"/";
  if (parcalar.dwExtraInfoLength > 0) {
    yol.append(parcalar.lpszExtraInfo, parcalar.dwExtraInfoLength);
  }

  const DWORD istekBayraklari =
      parcalar.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;

  HINTERNET oturum =
      api.open(L"Orhun/0.8.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!oturum) {
    throw std::runtime_error("WinHttpOpen başarısız: " +
                             windowsHataMesaji(GetLastError()));
  }

  HINTERNET baglanti = api.connect(oturum, host.c_str(), parcalar.nPort, 0);
  if (!baglanti) {
    const std::string hata = windowsHataMesaji(GetLastError());
    api.closeHandle(oturum);
    throw std::runtime_error("WinHttpConnect başarısız: " + hata);
  }

  HINTERNET istek = api.openRequest(
      baglanti, L"GET", yol.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, istekBayraklari);
  if (!istek) {
    const std::string hata = windowsHataMesaji(GetLastError());
    api.closeHandle(baglanti);
    api.closeHandle(oturum);
    throw std::runtime_error("WinHttpOpenRequest başarısız: " + hata);
  }

  api.setTimeouts(istek, 15000, 15000, 30000, 30000);

  if (!api.sendRequest(istek, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                       WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
    const std::string hata = windowsHataMesaji(GetLastError());
    api.closeHandle(istek);
    api.closeHandle(baglanti);
    api.closeHandle(oturum);
    throw std::runtime_error("WinHttpSendRequest başarısız: " + hata);
  }

  if (!api.receiveResponse(istek, nullptr)) {
    const std::string hata = windowsHataMesaji(GetLastError());
    api.closeHandle(istek);
    api.closeHandle(baglanti);
    api.closeHandle(oturum);
    throw std::runtime_error("WinHttpReceiveResponse başarısız: " + hata);
  }

  DWORD durumKodu = 0;
  DWORD durumUzunluk = sizeof(durumKodu);
  if (api.queryHeaders(istek,
                       WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                       WINHTTP_HEADER_NAME_BY_INDEX, &durumKodu, &durumUzunluk,
                       WINHTTP_NO_HEADER_INDEX)) {
    if (durumKodu >= 400) {
      api.closeHandle(istek);
      api.closeHandle(baglanti);
      api.closeHandle(oturum);
      throw std::runtime_error("HTTP durum kodu: " + std::to_string(durumKodu));
    }
  }

  std::string icerik;
  while (true) {
    DWORD mevcut = 0;
    if (!api.queryDataAvailable(istek, &mevcut)) {
      const std::string hata = windowsHataMesaji(GetLastError());
      api.closeHandle(istek);
      api.closeHandle(baglanti);
      api.closeHandle(oturum);
      throw std::runtime_error("WinHttpQueryDataAvailable başarısız: " + hata);
    }

    if (mevcut == 0) {
      break;
    }

    std::string parca(mevcut, '\0');
    DWORD okunan = 0;
    if (!api.readData(istek, parca.data(), mevcut, &okunan)) {
      const std::string hata = windowsHataMesaji(GetLastError());
      api.closeHandle(istek);
      api.closeHandle(baglanti);
      api.closeHandle(oturum);
      throw std::runtime_error("WinHttpReadData başarısız: " + hata);
    }
    parca.resize(okunan);
    icerik += parca;
  }

  api.closeHandle(istek);
  api.closeHandle(baglanti);
  api.closeHandle(oturum);

  return icerik;
}
#else
[[maybe_unused]] std::string internetIcerigiGetir(const std::string &url) {
  return yerlesik::internetIcerigiGetir(url);
}
#endif
} // namespace

Interpreter::Interpreter(std::vector<std::string> programArgumanlari)
    : programArgumanlari_(std::move(programArgumanlari)) {
  gomuluIslevleriYukle();
  yerlesikModulleriYukle();
}

Interpreter::~Interpreter() {
#ifdef _WIN32
  grafikKapat();
#endif
  ffiIslevBaglantilari_.clear();
  ffiKutuphaneleri_.clear();
  ffiKutuphaneKimlikleri_.clear();
}

void Interpreter::gomuluIslevleriYukle() {
  // Genel amaçlı yardımcı kütüphane.
  gomuluIslevler_["uzunluk"] = [this](const std::vector<OrhunDegeri> &args,
                                      std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "uzunluk(liste_veya_metin) tek argüman alır.");
    }

    const OrhunDegeri &hedef = args[0];
    if (std::holds_alternative<std::string>(hedef.veri)) {
      return OrhunDegeri(
          static_cast<int>(std::get<std::string>(hedef.veri).size()));
    }
    if (std::holds_alternative<OrhunDegeri::ListeTipi>(hedef.veri)) {
      const auto &liste = std::get<OrhunDegeri::ListeTipi>(hedef.veri);
      return OrhunDegeri(static_cast<int>(liste ? liste->size() : 0));
    }
    if (std::holds_alternative<OrhunDegeri::SozlukTipi>(hedef.veri)) {
      const auto &sozluk = std::get<OrhunDegeri::SozlukTipi>(hedef.veri);
      return OrhunDegeri(static_cast<int>(sozluk ? sozluk->size() : 0));
    }

    hataFirlat(
        satir,
        "uzunluk yalnızca metin, liste veya sözlük üzerinde kullanılabilir.");
  };

  gomuluIslevler_["listeye_ekle"] = [this](const std::vector<OrhunDegeri> &args,
                                           std::size_t satir) -> OrhunDegeri {
    if (args.size() != 2) {
      hataFirlat(satir, "listeye_ekle(liste, eleman) iki argüman alır.");
    }

    if (!std::holds_alternative<OrhunDegeri::ListeTipi>(args[0].veri)) {
      hataFirlat(satir,
                 "listeye_ekle fonksiyonunun ilk argümanı liste olmalıdır.");
    }

    const auto &mevcutListe = std::get<OrhunDegeri::ListeTipi>(args[0].veri);
    if (!mevcutListe) {
      hataFirlat(satir, "listeye_ekle boş liste referansı üzerinde çalışamaz.");
    }
    mevcutListe->push_back(args[1]);
    return OrhunDegeri(0);
  };

  auto aralikOlustur = [this](const std::vector<OrhunDegeri> &args,
                              std::size_t satir) -> OrhunDegeri {
    if (args.empty() || args.size() > 3) {
      hataFirlat(satir, "aralik([baslangic], bitis, [adim]) bir, iki veya uc "
                       "arguman alir.");
    }

    const long long baslangic =
        args.size() == 1
            ? 0
            : static_cast<long long>(
                  std::llround(sayiDegeri(args[0], satir, "aralik")));
    const long long bitis = static_cast<long long>(std::llround(sayiDegeri(
        args.size() == 1 ? args[0] : args[1], satir, "aralik")));
    const long long adim =
        args.size() < 3
            ? 1
            : static_cast<long long>(
                  std::llround(sayiDegeri(args[2], satir, "aralik")));

    OrhunDegeri::ListeVeri sonuc;
    if (adim == 0) {
      return OrhunDegeri(std::move(sonuc));
    }

    auto sayiDegeriOlustur = [](long long deger) -> OrhunDegeri {
      if (deger >= std::numeric_limits<int>::min() &&
          deger <= std::numeric_limits<int>::max()) {
        return OrhunDegeri(static_cast<int>(deger));
      }
      return OrhunDegeri(static_cast<double>(deger));
    };

    for (long long i = baslangic;
         adim > 0 ? i < bitis : i > bitis; i += adim) {
      sonuc.push_back(sayiDegeriOlustur(i));
    }
    return OrhunDegeri(std::move(sonuc));
  };
  gomuluIslevler_["aralik"] = aralikOlustur;
  gomuluIslevler_["aralık"] = aralikOlustur;

  auto koleksiyonUzunluguBul = [this](const OrhunDegeri &deger,
                                      std::size_t satir,
                                      const std::string &baglam) -> std::size_t {
    if (std::holds_alternative<std::string>(deger.veri)) {
      return std::get<std::string>(deger.veri).size();
    }
    if (std::holds_alternative<OrhunDegeri::ListeTipi>(deger.veri)) {
      const auto &liste = std::get<OrhunDegeri::ListeTipi>(deger.veri);
      return liste ? liste->size() : 0;
    }
    if (std::holds_alternative<OrhunDegeri::SozlukTipi>(deger.veri)) {
      const auto &sozluk = std::get<OrhunDegeri::SozlukTipi>(deger.veri);
      return sozluk ? sozluk->size() : 0;
    }
    hataFirlat(satir, baglam + ": metin, liste veya sozluk bekleniyor.");
  };

  gomuluIslevler_["bos_mu"] = [this, koleksiyonUzunluguBul](
                                  const std::vector<OrhunDegeri> &args,
                                  std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "bos_mu(deger) tek arguman alir.");
    }
    return OrhunDegeri(koleksiyonUzunluguBul(args[0], satir, "bos_mu") == 0
                           ? 1
                           : 0);
  };
  gomuluIslevler_["boş_mu"] = gomuluIslevler_["bos_mu"];

  gomuluIslevler_["dolu_mu"] = [this, koleksiyonUzunluguBul](
                                   const std::vector<OrhunDegeri> &args,
                                   std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "dolu_mu(deger) tek arguman alir.");
    }
    return OrhunDegeri(koleksiyonUzunluguBul(args[0], satir, "dolu_mu") > 0
                           ? 1
                           : 0);
  };

  gomuluIslevler_["ilk"] = [this](const std::vector<OrhunDegeri> &args,
                                  std::size_t satir) -> OrhunDegeri {
    if (args.empty() || args.size() > 2) {
      hataFirlat(satir, "ilk(liste, [yedek]) bir veya iki arguman alir.");
    }
    if (!std::holds_alternative<OrhunDegeri::ListeTipi>(args[0].veri)) {
      hataFirlat(satir, "ilk icin ilk arguman liste olmalidir.");
    }
    const auto &liste = std::get<OrhunDegeri::ListeTipi>(args[0].veri);
    if (liste && !liste->empty()) {
      return liste->front();
    }
    if (args.size() == 2) {
      return args[1];
    }
    hataFirlat(satir, "ilk bos liste icin yedek arguman ister.");
  };

  gomuluIslevler_["son"] = [this](const std::vector<OrhunDegeri> &args,
                                  std::size_t satir) -> OrhunDegeri {
    if (args.empty() || args.size() > 2) {
      hataFirlat(satir, "son(liste, [yedek]) bir veya iki arguman alir.");
    }
    if (!std::holds_alternative<OrhunDegeri::ListeTipi>(args[0].veri)) {
      hataFirlat(satir, "son icin ilk arguman liste olmalidir.");
    }
    const auto &liste = std::get<OrhunDegeri::ListeTipi>(args[0].veri);
    if (liste && !liste->empty()) {
      return liste->back();
    }
    if (args.size() == 2) {
      return args[1];
    }
    hataFirlat(satir, "son bos liste icin yedek arguman ister.");
  };

  gomuluIslevler_["dosya_oku"] = [this](const std::vector<OrhunDegeri> &args,
                                        std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "dosya_oku(\"dosya.oh\") tek argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "dosya_oku için dosya yolu metin olmalıdır.");
    }

    const std::string yol = std::get<std::string>(args[0].veri);
    std::ifstream dosya(yol, std::ios::binary);
    if (!dosya.is_open()) {
      hataFirlat(satir, "'" + yol + "' dosyası okunamadı.");
    }

    std::ostringstream tampon;
    tampon << dosya.rdbuf();
    return OrhunDegeri(tampon.str());
  };

  gomuluIslevler_["dosyaya_yaz"] = [this](const std::vector<OrhunDegeri> &args,
                                          std::size_t satir) -> OrhunDegeri {
    if (args.size() != 2) {
      hataFirlat(satir, "dosyaya_yaz(\"dosya.oh\", icerik) iki argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "dosyaya_yaz için dosya yolu metin olmalıdır.");
    }

    const std::string yol = std::get<std::string>(args[0].veri);
    const std::string icerik = metneCevir(args[1]);

    std::ofstream dosya(yol, std::ios::binary | std::ios::trunc);
    if (!dosya.is_open()) {
      hataFirlat(satir, "'" + yol + "' dosyasına yazılamadı.");
    }

    dosya << icerik;
    return OrhunDegeri(1);
  };

  gomuluIslevler_["dosya.var_mi"] = [this](const std::vector<OrhunDegeri> &args,
                                           std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "dosya.var_mi(\"dosya\") tek argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "dosya.var_mi için dosya yolu metin olmalıdır.");
    }

    const std::string yol = std::get<std::string>(args[0].veri);
    std::error_code ec;
    const bool var = std::filesystem::exists(yol, ec);
    if (ec) {
      hataFirlat(satir, "dosya.var_mi başarısız: " + ec.message());
    }
    return OrhunDegeri(var ? 1 : 0);
  };

  gomuluIslevler_["dosya.sil"] = [this](const std::vector<OrhunDegeri> &args,
                                        std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "dosya.sil(\"dosya\") tek argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "dosya.sil için dosya yolu metin olmalıdır.");
    }

    const std::string yol = std::get<std::string>(args[0].veri);
    std::error_code ec;
    const bool silindi = std::filesystem::remove(yol, ec);
    if (ec) {
      hataFirlat(satir, "dosya.sil başarısız: " + ec.message());
    }
    return OrhunDegeri(silindi ? 1 : 0);
  };

  gomuluIslevler_["dosya.ekle_satir"] =
      [this](const std::vector<OrhunDegeri> &args,
             std::size_t satir) -> OrhunDegeri {
    if (args.size() != 2) {
      hataFirlat(satir, "dosya.ekle_satir(\"dosya\", metin) iki argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "dosya.ekle_satir için dosya yolu metin olmalıdır.");
    }

    const std::string yol = std::get<std::string>(args[0].veri);
    const std::string icerik = metneCevir(args[1]);
    std::ofstream dosya(yol, std::ios::binary | std::ios::app);
    if (!dosya.is_open()) {
      hataFirlat(satir, "'" + yol + "' dosyasına ekleme yapılamadı.");
    }
    dosya << icerik << '\n';
    return OrhunDegeri(1);
  };

  gomuluIslevler_["dosya.listele"] =
      [this](const std::vector<OrhunDegeri> &args,
             std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "dosya.listele(\"klasor\") tek argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "dosya.listele için klasör yolu metin olmalıdır.");
    }

    const std::string yol = std::get<std::string>(args[0].veri);
    std::error_code ec;
    if (!std::filesystem::exists(yol, ec)) {
      if (ec) {
        hataFirlat(satir, "dosya.listele başarısız: " + ec.message());
      }
      hataFirlat(satir, "dosya.listele: '" + yol + "' bulunamadı.");
    }

    OrhunDegeri::ListeVeri sonuc;
    for (const auto &giris : std::filesystem::directory_iterator(yol, ec)) {
      if (ec) {
        hataFirlat(satir, "dosya.listele başarısız: " + ec.message());
      }
      sonuc.emplace_back(giris.path().filename().u8string());
    }
    return OrhunDegeri(std::move(sonuc));
  };

  gomuluIslevler_["dosya.klasor_olustur"] =
      [this](const std::vector<OrhunDegeri> &args,
             std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "dosya.klasor_olustur(\"yol\") tek argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "dosya.klasor_olustur için yol metin olmalıdır.");
    }

    const std::string yol = std::get<std::string>(args[0].veri);
    std::error_code ec;
    const bool olustu = std::filesystem::create_directories(yol, ec);
    if (ec) {
      hataFirlat(satir, "dosya.klasor_olustur başarısız: " + ec.message());
    }
    return OrhunDegeri(olustu ? 1 : 0);
  };

  // Ağ / JSON / sistem modülleri.
  gomuluIslevler_["internet.getir"] =
      [this](const std::vector<OrhunDegeri> &args,
             std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "internet.getir(url) tek argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "internet.getir(url) için metin URL bekleniyor.");
    }

    try {
      return OrhunDegeri(
          yerlesik::internetIcerigiGetir(std::get<std::string>(args[0].veri)));
    } catch (const std::exception &ex) {
      hataFirlat(satir, "internet.getir başarısız: " + std::string(ex.what()));
    }
  };

  gomuluIslevler_["internet.indir"] =
      [this](const std::vector<OrhunDegeri> &args,
             std::size_t satir) -> OrhunDegeri {
    if (args.size() != 2) {
      hataFirlat(satir,
                 "internet.indir(url, \"hedef_dosya\") iki argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri) ||
        !std::holds_alternative<std::string>(args[1].veri)) {
      hataFirlat(satir,
                 "internet.indir için url ve dosya yolu metin olmalıdır.");
    }

    const std::string url = std::get<std::string>(args[0].veri);
    const std::string hedef = std::get<std::string>(args[1].veri);
    try {
      const std::string icerik = yerlesik::internetIcerigiGetir(url);
      std::ofstream dosya(hedef, std::ios::binary | std::ios::trunc);
      if (!dosya.is_open()) {
        hataFirlat(satir,
                   "internet.indir: '" + hedef + "' dosyasına yazılamadı.");
      }
      dosya.write(icerik.data(), static_cast<std::streamsize>(icerik.size()));
      return OrhunDegeri(static_cast<int>(icerik.size()));
    } catch (const std::exception &ex) {
      hataFirlat(satir, "internet.indir başarısız: " + std::string(ex.what()));
    }
  };

  gomuluIslevler_["json.coz"] = [this](const std::vector<OrhunDegeri> &args,
                                       std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "json.coz(metin) tek argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "json.coz(metin) için metin argümanı bekleniyor.");
    }

    try {
      return yerlesikJsondanOrhunDegere(
          yerlesik::jsonCoz(std::get<std::string>(args[0].veri)));
    } catch (const std::exception &ex) {
      hataFirlat(satir, "json.coz hatası: " + std::string(ex.what()));
    }
  };

  gomuluIslevler_["json.yaz"] = [this](const std::vector<OrhunDegeri> &args,
                                       std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "json.yaz(deger) tek argüman alır.");
    }
    try {
      return OrhunDegeri(
          yerlesik::jsonYaz(orhunDegerindenYerlesikJson(args[0])));
    } catch (const std::exception &ex) {
      hataFirlat(satir, "json.yaz hatası: " + std::string(ex.what()));
    }
  };

  gomuluIslevler_["json.guzel_yaz"] =
      [this](const std::vector<OrhunDegeri> &args,
             std::size_t satir) -> OrhunDegeri {
    if (args.empty() || args.size() > 2) {
      hataFirlat(satir,
                 "json.guzel_yaz(deger, [girinti]) bir veya iki argüman alır.");
    }

    int girinti = 2;
    if (args.size() == 2) {
      if (std::holds_alternative<int>(args[1].veri)) {
        girinti = std::get<int>(args[1].veri);
      } else if (std::holds_alternative<double>(args[1].veri)) {
        const double d = std::get<double>(args[1].veri);
        if (!tamSayiMi(d)) {
          hataFirlat(satir,
                     "json.guzel_yaz girinti değeri tam sayı olmalıdır.");
        }
        girinti = static_cast<int>(d);
      } else {
        hataFirlat(satir, "json.guzel_yaz girinti değeri sayı olmalıdır.");
      }
      if (girinti < 0 || girinti > 16) {
        hataFirlat(satir, "json.guzel_yaz girinti aralığı 0..16 olmalıdır.");
      }
    }

    try {
      return OrhunDegeri(yerlesik::jsonYaz(orhunDegerindenYerlesikJson(args[0]),
                                           true, girinti));
    } catch (const std::exception &ex) {
      hataFirlat(satir, "json.guzel_yaz hatası: " + std::string(ex.what()));
    }
  };

  // Basit kalıcı sözlük tabanlı veritabanı.
  auto veritabaniYolCoz = [this](const std::vector<OrhunDegeri> &args,
                                 std::size_t argIndex,
                                 std::size_t satir) -> std::string {
    if (args.size() > argIndex) {
      if (!std::holds_alternative<std::string>(args[argIndex].veri)) {
        hataFirlat(satir, "veritabani için dosya yolu metin olmalıdır.");
      }
      return std::get<std::string>(args[argIndex].veri);
    }
    return "_orhun_veritabani.json";
  };

  auto veritabaniOku = [this](const std::string &yol,
                              std::size_t satir) -> OrhunDegeri::SozlukVeri {
    std::ifstream dosya(yol, std::ios::binary);
    if (!dosya.is_open()) {
      return {};
    }
    std::ostringstream tampon;
    tampon << dosya.rdbuf();
    const std::string icerik = tampon.str();
    if (icerik.empty()) {
      return {};
    }

    try {
      OrhunDegeri kok = yerlesikJsondanOrhunDegere(yerlesik::jsonCoz(icerik));
      if (!std::holds_alternative<OrhunDegeri::SozlukTipi>(kok.veri)) {
        hataFirlat(satir,
                   "veritabani dosyası sözlük/nesne formatında değil: " + yol);
      }
      const auto &ptr = std::get<OrhunDegeri::SozlukTipi>(kok.veri);
      return ptr ? *ptr : OrhunDegeri::SozlukVeri{};
    } catch (const std::exception &ex) {
      hataFirlat(satir,
                 "veritabani.oku çözümlenemedi: " + std::string(ex.what()));
    }
  };

  auto veritabaniYaz = [this](const std::string &yol,
                              const OrhunDegeri::SozlukVeri &veri,
                              std::size_t satir) {
    std::ofstream dosya(yol, std::ios::binary | std::ios::trunc);
    if (!dosya.is_open()) {
      hataFirlat(satir, "veritabani dosyasına yazılamadı: " + yol);
    }

    const std::string json = yerlesik::jsonYaz(
        orhunDegerindenYerlesikJson(OrhunDegeri(veri)), true, 2);
    dosya.write(json.data(), static_cast<std::streamsize>(json.size()));
  };

  gomuluIslevler_["veritabani.kaydet"] =
      [this, veritabaniYolCoz, veritabaniOku,
       veritabaniYaz](const std::vector<OrhunDegeri> &args,
                      std::size_t satir) -> OrhunDegeri {
    if (args.size() < 2 || args.size() > 3) {
      hataFirlat(satir, "veritabani.kaydet(\"anahtar\", deger, [\"dosya\"]) "
                        "iki veya üç argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "veritabani.kaydet için anahtar metin olmalıdır.");
    }

    const std::string anahtar = std::get<std::string>(args[0].veri);
    const std::string yol = veritabaniYolCoz(args, 2, satir);
    OrhunDegeri::SozlukVeri db = veritabaniOku(yol, satir);
    db[anahtar] = args[1];
    veritabaniYaz(yol, db, satir);
    return OrhunDegeri(1);
  };

  gomuluIslevler_["veritabani.oku"] = [this, veritabaniYolCoz, veritabaniOku](
                                          const std::vector<OrhunDegeri> &args,
                                          std::size_t satir) -> OrhunDegeri {
    if (args.size() < 1 || args.size() > 2) {
      hataFirlat(satir, "veritabani.oku(\"anahtar\", [\"dosya\"]) bir veya iki "
                        "argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "veritabani.oku için anahtar metin olmalıdır.");
    }

    const std::string anahtar = std::get<std::string>(args[0].veri);
    const std::string yol = veritabaniYolCoz(args, 1, satir);
    const OrhunDegeri::SozlukVeri db = veritabaniOku(yol, satir);
    const auto it = db.find(anahtar);
    if (it == db.end()) {
      std::vector<std::string> adaylar;
      adaylar.reserve(db.size());
      for (const auto &[k, _] : db) {
        adaylar.push_back(k);
      }
      hataFirlat(satir, oneriliMesaj("'" + anahtar +
                                         "' anahtarı veritabanında bulunamadı.",
                                     anahtar, adaylar));
    }
    return it->second;
  };

  gomuluIslevler_["veritabani.sil"] =
      [this, veritabaniYolCoz, veritabaniOku,
       veritabaniYaz](const std::vector<OrhunDegeri> &args,
                      std::size_t satir) -> OrhunDegeri {
    if (args.size() < 1 || args.size() > 2) {
      hataFirlat(satir, "veritabani.sil(\"anahtar\", [\"dosya\"]) bir veya iki "
                        "argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "veritabani.sil için anahtar metin olmalıdır.");
    }

    const std::string anahtar = std::get<std::string>(args[0].veri);
    const std::string yol = veritabaniYolCoz(args, 1, satir);
    OrhunDegeri::SozlukVeri db = veritabaniOku(yol, satir);
    const std::size_t silinen = db.erase(anahtar);
    if (silinen > 0) {
      veritabaniYaz(yol, db, satir);
    }
    return OrhunDegeri(static_cast<int>(silinen));
  };

  gomuluIslevler_["veritabani.listele"] =
      [this, veritabaniYolCoz,
       veritabaniOku](const std::vector<OrhunDegeri> &args,
                      std::size_t satir) -> OrhunDegeri {
    if (args.size() > 1) {
      hataFirlat(
          satir,
          "veritabani.listele([\"dosya\"]) sıfır veya bir argüman alır.");
    }
    const std::string yol = veritabaniYolCoz(args, 0, satir);
    const OrhunDegeri::SozlukVeri db = veritabaniOku(yol, satir);
    OrhunDegeri::ListeVeri anahtarlar;
    anahtarlar.reserve(db.size());
    for (const auto &[anahtar, _] : db) {
      anahtarlar.emplace_back(anahtar);
    }
    return OrhunDegeri(std::move(anahtarlar));
  };

  gomuluIslevler_["sunucu.baslat"] =
      [this](const std::vector<OrhunDegeri> &args,
             std::size_t satir) -> OrhunDegeri {
    if (args.empty() || args.size() > 2) {
      hataFirlat(
          satir,
          "sunucu.baslat(port, [\"klasor\"]) bir veya iki argüman alır.");
    }

    int port = 0;
    if (std::holds_alternative<int>(args[0].veri)) {
      port = std::get<int>(args[0].veri);
    } else if (std::holds_alternative<double>(args[0].veri)) {
      const double d = std::get<double>(args[0].veri);
      if (!tamSayiMi(d)) {
        hataFirlat(satir, "sunucu.baslat için port tam sayı olmalıdır.");
      }
      port = static_cast<int>(d);
    } else {
      hataFirlat(satir,
                 "sunucu.baslat için ilk argüman port sayısı olmalıdır.");
    }

    std::string klasor = ".";
    if (args.size() == 2) {
      if (!std::holds_alternative<std::string>(args[1].veri)) {
        hataFirlat(
            satir,
            "sunucu.baslat için ikinci argüman klasör yolu (metin) olmalıdır.");
      }
      klasor = std::get<std::string>(args[1].veri);
    }

    std::string hata;
    if (!yerlesik::paylasimliHttpSunucu().baslat(port, klasor, &hata)) {
      hataFirlat(satir, "sunucu.baslat başarısız: " + hata);
    }
    return OrhunDegeri(1);
  };

  gomuluIslevler_["sunucu.durdur"] =
      [this](const std::vector<OrhunDegeri> &args,
             std::size_t satir) -> OrhunDegeri {
    if (!args.empty()) {
      hataFirlat(satir, "sunucu.durdur() argüman almaz.");
    }
    yerlesik::paylasimliHttpSunucu().durdur();
    return OrhunDegeri(1);
  };

  gomuluIslevler_["sunucu.calisiyor_mu"] =
      [this](const std::vector<OrhunDegeri> &args,
             std::size_t satir) -> OrhunDegeri {
    if (!args.empty()) {
      hataFirlat(satir, "sunucu.calisiyor_mu() argüman almaz.");
    }
    return OrhunDegeri(yerlesik::paylasimliHttpSunucu().calisiyorMu() ? 1 : 0);
  };

  gomuluIslevler_["sistem.komut"] = [this](const std::vector<OrhunDegeri> &args,
                                           std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "sistem.komut(komut) tek argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "sistem.komut(komut) için metin komut bekleniyor.");
    }

    const std::string komut = std::get<std::string>(args[0].veri);
    if (!sistemKomutuKisitliModDisi() && !sistemKomutuGuvenliMi(komut)) {
      hataFirlat(satir,
                 "sistem.komut kısıtlı modda tehlikeli karakter içeremez. "
                 "Gerekirse ORHUN_UNSAFE=1 ile açın.");
    }
    const int cikis = yerlesik::komutCalistirGuvenli(komut);
    if (cikis == -1) {
      hataFirlat(satir, "sistem.komut çalıştırılamadı.");
    }
    return OrhunDegeri(cikis);
  };

  gomuluIslevler_["sonuc.ok"] = [this](const std::vector<OrhunDegeri> &args,
                                        std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "sonuc.ok(deger) tek argüman alır.");
    }
    OrhunDegeri::SozlukVeri kayit;
    kayit["ok"] = OrhunDegeri(1);
    kayit["deger"] = args[0];
    kayit["hata"] = OrhunDegeri(0);
    return OrhunDegeri(std::move(kayit));
  };

  gomuluIslevler_["sonuc.hata"] = [this](const std::vector<OrhunDegeri> &args,
                                          std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "sonuc.hata(hata) tek argüman alır.");
    }
    OrhunDegeri::SozlukVeri kayit;
    kayit["ok"] = OrhunDegeri(0);
    kayit["deger"] = OrhunDegeri(0);
    kayit["hata"] = args[0];
    return OrhunDegeri(std::move(kayit));
  };

  // FFI (Native C/C++ fonksiyon çağrısı) çekirdeği.
  auto ffiKullanimDogrula = [this](std::size_t satir,
                                   const std::string &baglam) {
    if (ffiPolitikasiBul() == FFIPolitikasi::Off) {
      hataFirlat(satir,
                 baglam + ": FFI politikası kapalı "
                              "(ORHUN_FFI_POLICY=off).");
    }
  };
  auto ffiKutuphaneErisimDogrula = [this, ffiKullanimDogrula](
                                       const std::string &kutuphaneYolu,
                                       std::size_t satir,
                                       const std::string &baglam) {
    ffiKullanimDogrula(satir, baglam);
    if (ffiPolitikasiBul() == FFIPolitikasi::Full) {
      return;
    }
    const auto izinliler = ffiAllowlistiniBul();
    if (ffiYolAllowlistteMi(kutuphaneYolu, izinliler)) {
      return;
    }
    hataFirlat(
        satir, baglam + ": FFI allowlist dışı kütüphane. ORHUN_FFI_ALLOWLIST "
                        "veya ORHUN_FFI_POLICY=full kullanın.");
  };
  auto ffiKimlikCoz = [this](const OrhunDegeri &deger, std::size_t satir,
                             const std::string &baglam) -> int {
    if (std::holds_alternative<int>(deger.veri)) {
      return std::get<int>(deger.veri);
    }
    if (std::holds_alternative<double>(deger.veri)) {
      const double d = std::get<double>(deger.veri);
      if (!tamSayiMi(d)) {
        hataFirlat(satir, baglam + " için tutamac tam sayı olmalıdır.");
      }
      return static_cast<int>(d);
    }
    if (std::holds_alternative<std::string>(deger.veri)) {
      const auto yuklenen = gomuluIslevler_.at("ffi.yukle")(
          std::vector<OrhunDegeri>{deger}, satir);
      return std::get<int>(yuklenen.veri);
    }
    hataFirlat(satir, baglam + " için ilk argüman tutamac(int) veya kütüphane "
                               "adı(metin) olmalıdır.");
  };

  auto ffiKutuphaneGetir = [this, ffiKimlikCoz](const OrhunDegeri &deger,
                                                std::size_t satir,
                                                const std::string &baglam)
      -> std::pair<int, std::shared_ptr<runtime::DynamicLibrary>> {
    const int kimlik = ffiKimlikCoz(deger, satir, baglam);
    const auto it = ffiKutuphaneleri_.find(kimlik);
    if (it == ffiKutuphaneleri_.end() || !it->second ||
        !it->second->isLoaded()) {
      hataFirlat(satir, baglam + ": geçersiz kütüphane tutamacı #" +
                            std::to_string(kimlik));
    }
    return std::make_pair(kimlik, it->second);
  };

  auto ffiTipCoz = [this](const std::string &tipMetni,
                          std::size_t satir) -> FFIType {
    if (tipMetni == "void" || tipMetni == "bos") {
      return FFIType::NONE;
    }
    if (tipMetni == "int" || tipMetni == "tam" || tipMetni == "int64") {
      return FFIType::INT64;
    }
    if (tipMetni == "double" || tipMetni == "ondalik" || tipMetni == "sayi") {
      return FFIType::DOUBLE;
    }
    if (tipMetni == "string" || tipMetni == "metin" || tipMetni == "str") {
      return FFIType::STRING;
    }
    if (tipMetni == "pointer" || tipMetni == "isaretci" || tipMetni == "ptr") {
      return FFIType::POINTER;
    }
    hataFirlat(satir, "FFI tipi tanınmıyor: '" + tipMetni + "'.");
  };

  gomuluIslevler_["ffi.yukle"] =
      [this, ffiKutuphaneErisimDogrula](const std::vector<OrhunDegeri> &args,
                                        std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "ffi.yukle(\"kutuphane\") tek argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "ffi.yukle için metin kütüphane yolu bekleniyor.");
    }

    const std::string kutuphaneYolu = std::get<std::string>(args[0].veri);
    ffiKutuphaneErisimDogrula(kutuphaneYolu, satir, "ffi.yukle");
    const auto mevcut = ffiKutuphaneKimlikleri_.find(kutuphaneYolu);
    if (mevcut != ffiKutuphaneKimlikleri_.end()) {
      return OrhunDegeri(mevcut->second);
    }

    auto kutuphane = std::make_shared<runtime::DynamicLibrary>(kutuphaneYolu);
    std::string hata;
    if (!kutuphane->load(&hata)) {
      hataFirlat(satir,
                 "ffi.yukle başarısız: '" + kutuphaneYolu + "' (" + hata + ")");
    }

    const int kimlik = ffiSonrakiKimlik_++;
    ffiKutuphaneleri_[kimlik] = kutuphane;
    ffiKutuphaneKimlikleri_[kutuphaneYolu] = kimlik;
    return OrhunDegeri(kimlik);
  };

  gomuluIslevler_["ffi.cagir"] =
      [this, ffiKutuphaneGetir,
       ffiKullanimDogrula](const std::vector<OrhunDegeri> &args,
                           std::size_t satir) -> OrhunDegeri {
    ffiKullanimDogrula(satir, "ffi.cagir");
    if (args.size() < 2) {
      hataFirlat(satir, "ffi.cagir(tutamac, \"fonksiyon\", ...argumanlar) en "
                        "az 2 argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[1].veri)) {
      hataFirlat(satir, "ffi.cagir içinde fonksiyon adı metin olmalıdır.");
    }
    const auto [kimlik, kutuphane] =
        ffiKutuphaneGetir(args[0], satir, "ffi.cagir");
    (void)kimlik;
    const std::string fonksiyonAdi = std::get<std::string>(args[1].veri);
    std::string sembolHatasi;
    std::uintptr_t fonksiyon =
        kutuphane->getSymbol(fonksiyonAdi, &sembolHatasi);
    if (fonksiyon == 0) {
      hataFirlat(satir, "ffi.cagir: '" + fonksiyonAdi +
                            "' sembolü bulunamadı (" + sembolHatasi + ")");
    }

    std::size_t metinSayisi = 0;
    for (std::size_t i = 2; i < args.size(); ++i) {
      if (std::holds_alternative<std::string>(args[i].veri)) {
        ++metinSayisi;
      }
    }

    std::vector<std::string> metinSahipligi;
    metinSahipligi.reserve(metinSayisi);
    std::vector<std::intptr_t> hamArgumanlar;
    hamArgumanlar.reserve(args.size() - 2);

    for (std::size_t i = 2; i < args.size(); ++i) {
      const OrhunDegeri &arg = args[i];
      if (std::holds_alternative<int>(arg.veri)) {
        hamArgumanlar.push_back(
            static_cast<std::intptr_t>(std::get<int>(arg.veri)));
        continue;
      }
      if (std::holds_alternative<double>(arg.veri)) {
        const double d = std::get<double>(arg.veri);
        if (!tamSayiMi(d)) {
          hataFirlat(
              satir,
              "ffi.cagir şimdilik ondalık argümanları desteklemiyor (arg #" +
                  std::to_string(i - 1) + ").");
        }
        hamArgumanlar.push_back(static_cast<std::intptr_t>(d));
        continue;
      }
      if (std::holds_alternative<std::string>(arg.veri)) {
        metinSahipligi.push_back(std::get<std::string>(arg.veri));
        hamArgumanlar.push_back(
            reinterpret_cast<std::intptr_t>(metinSahipligi.back().c_str()));
        continue;
      }

      hataFirlat(satir, "ffi.cagir sadece int/double(tam sayı)/metin "
                        "argümanlarını destekliyor.");
    }

    try {
      const std::intptr_t donus = ffiHamCagir(fonksiyon, hamArgumanlar);
      if (donus <=
              static_cast<std::intptr_t>(std::numeric_limits<int>::max()) &&
          donus >=
              static_cast<std::intptr_t>(std::numeric_limits<int>::min())) {
        return OrhunDegeri(static_cast<int>(donus));
      }
      return OrhunDegeri(static_cast<double>(donus));
    } catch (const std::exception &ex) {
      hataFirlat(satir, "ffi.cagir çalışma hatası: " + std::string(ex.what()));
    }
  };

  gomuluIslevler_["ffi.sembol_var_mi"] =
      [this, ffiKimlikCoz,
       ffiKullanimDogrula](const std::vector<OrhunDegeri> &args,
                           std::size_t satir) -> OrhunDegeri {
    ffiKullanimDogrula(satir, "ffi.sembol_var_mi");
    if (args.size() != 2) {
      hataFirlat(satir,
                 "ffi.sembol_var_mi(tutamac, \"fonksiyon\") iki argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[1].veri)) {
      hataFirlat(satir,
                 "ffi.sembol_var_mi içinde fonksiyon adı metin olmalıdır.");
    }

    int kimlik = 0;
    if (std::holds_alternative<int>(args[0].veri) ||
        std::holds_alternative<double>(args[0].veri) ||
        std::holds_alternative<std::string>(args[0].veri)) {
      kimlik = ffiKimlikCoz(args[0], satir, "ffi.sembol_var_mi");
    } else {
      hataFirlat(satir, "ffi.sembol_var_mi için ilk argüman tutamac(int) veya "
                        "kütüphane adı(metin) olmalıdır.");
    }

    const auto itKutuphane = ffiKutuphaneleri_.find(kimlik);
    if (itKutuphane == ffiKutuphaneleri_.end() || !itKutuphane->second ||
        !itKutuphane->second->isLoaded()) {
      return OrhunDegeri(0);
    }

    const std::string fonksiyonAdi = std::get<std::string>(args[1].veri);
    const std::uintptr_t fonksiyon =
        itKutuphane->second->getSymbol(fonksiyonAdi, nullptr);
    return OrhunDegeri(fonksiyon != 0 ? 1 : 0);
  };

  gomuluIslevler_["ffi.cagir_metin"] =
      [this, ffiKutuphaneGetir,
       ffiKullanimDogrula](const std::vector<OrhunDegeri> &args,
                           std::size_t satir) -> OrhunDegeri {
    ffiKullanimDogrula(satir, "ffi.cagir_metin");
    if (args.size() < 2) {
      hataFirlat(satir, "ffi.cagir_metin(tutamac, \"fonksiyon\", "
                        "...argumanlar) en az 2 argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[1].veri)) {
      hataFirlat(satir,
                 "ffi.cagir_metin içinde fonksiyon adı metin olmalıdır.");
    }

    const auto [kimlik, kutuphane] =
        ffiKutuphaneGetir(args[0], satir, "ffi.cagir_metin");
    (void)kimlik;
    const std::string fonksiyonAdi = std::get<std::string>(args[1].veri);
    std::string sembolHatasi;
    std::uintptr_t fonksiyon =
        kutuphane->getSymbol(fonksiyonAdi, &sembolHatasi);
    if (fonksiyon == 0) {
      hataFirlat(satir, "ffi.cagir_metin: '" + fonksiyonAdi +
                            "' sembolü bulunamadı (" + sembolHatasi + ")");
    }

    std::size_t metinSayisi = 0;
    for (std::size_t i = 2; i < args.size(); ++i) {
      if (std::holds_alternative<std::string>(args[i].veri)) {
        ++metinSayisi;
      }
    }

    std::vector<std::string> metinSahipligi;
    metinSahipligi.reserve(metinSayisi);
    std::vector<std::intptr_t> hamArgumanlar;
    hamArgumanlar.reserve(args.size() - 2);

    for (std::size_t i = 2; i < args.size(); ++i) {
      const OrhunDegeri &arg = args[i];
      if (std::holds_alternative<int>(arg.veri)) {
        hamArgumanlar.push_back(
            static_cast<std::intptr_t>(std::get<int>(arg.veri)));
        continue;
      }
      if (std::holds_alternative<double>(arg.veri)) {
        const double d = std::get<double>(arg.veri);
        if (!tamSayiMi(d)) {
          hataFirlat(satir, "ffi.cagir_metin şimdilik ondalık argümanları "
                            "desteklemiyor (arg #" +
                                std::to_string(i - 1) + ").");
        }
        hamArgumanlar.push_back(static_cast<std::intptr_t>(d));
        continue;
      }
      if (std::holds_alternative<std::string>(arg.veri)) {
        metinSahipligi.push_back(std::get<std::string>(arg.veri));
        hamArgumanlar.push_back(
            reinterpret_cast<std::intptr_t>(metinSahipligi.back().c_str()));
        continue;
      }

      hataFirlat(satir, "ffi.cagir_metin sadece int/double(tam sayı)/metin "
                        "argümanlarını destekliyor.");
    }

    try {
      const std::intptr_t donus = ffiHamCagir(fonksiyon, hamArgumanlar);
      if (donus == 0) {
        return OrhunDegeri(std::string{});
      }

      const char *metin = reinterpret_cast<const char *>(donus);
      return OrhunDegeri(std::string(metin));
    } catch (const std::exception &ex) {
      hataFirlat(satir,
                 "ffi.cagir_metin çalışma hatası: " + std::string(ex.what()));
    }
  };

  gomuluIslevler_["ffi.tanimla"] =
      [this, ffiKutuphaneGetir, ffiTipCoz,
       ffiKullanimDogrula](const std::vector<OrhunDegeri> &args,
                           std::size_t satir) -> OrhunDegeri {
    ffiKullanimDogrula(satir, "ffi.tanimla");
    if (args.size() < 3 || args.size() > 4) {
      hataFirlat(satir, "ffi.tanimla(kutuphane, \"sembol\", \"donus\", "
                        "[argTipleri]) 3 veya 4 argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[1].veri)) {
      hataFirlat(satir, "ffi.tanimla ikinci argümanda sembol adı bekler.");
    }
    if (!std::holds_alternative<std::string>(args[2].veri)) {
      hataFirlat(satir, "ffi.tanimla üçüncü argümanda dönüş tipi bekler.");
    }

    const auto [kimlik, kutuphane] =
        ffiKutuphaneGetir(args[0], satir, "ffi.tanimla");
    (void)kutuphane;

    FFISignature imza;
    imza.sembolAdi = std::get<std::string>(args[1].veri);
    imza.donusTipi = ffiTipCoz(std::get<std::string>(args[2].veri), satir);

    if (args.size() == 4) {
      if (!std::holds_alternative<OrhunDegeri::ListeTipi>(args[3].veri)) {
        hataFirlat(satir, "ffi.tanimla dördüncü argümanda tip listesi bekler.");
      }
      const auto listePtr = std::get<OrhunDegeri::ListeTipi>(args[3].veri);
      if (!listePtr) {
        hataFirlat(satir, "ffi.tanimla için argüman tipi listesi boş olamaz.");
      }
      for (const auto &tipDegeri : *listePtr) {
        if (!std::holds_alternative<std::string>(tipDegeri.veri)) {
          hataFirlat(satir, "ffi.tanimla tip listesi metinlerden oluşmalıdır.");
        }
        imza.argumanTipleri.push_back(
            ffiTipCoz(std::get<std::string>(tipDegeri.veri), satir));
      }
    }

    const int islevKimligi = ffiSonrakiIslevKimlik_++;
    ffiIslevBaglantilari_[islevKimligi] = FFIBinding{kimlik, std::move(imza)};
    return OrhunDegeri(islevKimligi);
  };

  gomuluIslevler_["ffi.cagir_tanimli"] =
      [this, ffiKullanimDogrula](const std::vector<OrhunDegeri> &args,
                                 std::size_t satir) -> OrhunDegeri {
    ffiKullanimDogrula(satir, "ffi.cagir_tanimli");
    if (args.empty()) {
      hataFirlat(satir, "ffi.cagir_tanimli(islevKimligi, ...argumanlar) en az "
                        "1 argüman alır.");
    }

    int islevKimligi = 0;
    if (std::holds_alternative<int>(args[0].veri)) {
      islevKimligi = std::get<int>(args[0].veri);
    } else if (std::holds_alternative<double>(args[0].veri)) {
      const double d = std::get<double>(args[0].veri);
      if (!tamSayiMi(d)) {
        hataFirlat(satir,
                   "ffi.cagir_tanimli için işlev kimliği tam sayı olmalıdır.");
      }
      islevKimligi = static_cast<int>(d);
    } else {
      hataFirlat(satir,
                 "ffi.cagir_tanimli için işlev kimliği tam sayı olmalıdır.");
    }

    const auto itBaglanti = ffiIslevBaglantilari_.find(islevKimligi);
    if (itBaglanti == ffiIslevBaglantilari_.end()) {
      hataFirlat(satir, "ffi.cagir_tanimli: geçersiz işlev kimliği #" +
                            std::to_string(islevKimligi));
    }

    const FFIBinding &baglanti = itBaglanti->second;
    const auto itKutuphane = ffiKutuphaneleri_.find(baglanti.kutuphaneKimligi);
    if (itKutuphane == ffiKutuphaneleri_.end() || !itKutuphane->second ||
        !itKutuphane->second->isLoaded()) {
      hataFirlat(satir, "ffi.cagir_tanimli: kütüphane tutamacı geçersiz.");
    }

    const std::size_t beklenenArguman = baglanti.imza.argumanTipleri.size();
    const std::size_t gelenArguman = args.size() - 1;
    if (beklenenArguman != gelenArguman) {
      hataFirlat(satir, "ffi.cagir_tanimli: beklenen argüman sayısı " +
                            std::to_string(beklenenArguman) + ", gelen " +
                            std::to_string(gelenArguman) + ".");
    }

    std::string sembolHatasi;
    std::uintptr_t fonksiyon =
        itKutuphane->second->getSymbol(baglanti.imza.sembolAdi, &sembolHatasi);
    if (fonksiyon == 0) {
      hataFirlat(satir, "ffi.cagir_tanimli: '" + baglanti.imza.sembolAdi +
                            "' sembolü bulunamadı (" + sembolHatasi + ")");
    }

    const bool butunArgumanlarCift =
        std::all_of(baglanti.imza.argumanTipleri.begin(),
                    baglanti.imza.argumanTipleri.end(),
                    [](FFIType tip) { return tip == FFIType::DOUBLE; });

    if (baglanti.imza.donusTipi == FFIType::DOUBLE) {
      if (!butunArgumanlarCift) {
        hataFirlat(satir, "ffi.cagir_tanimli: DOUBLE dönüş tipi için tüm "
                          "argümanlar DOUBLE olmalıdır. "
                          "Karışık ABI için libffi gerekir.");
      }

      std::vector<double> ciftArgumanlar;
      ciftArgumanlar.reserve(gelenArguman);
      for (std::size_t i = 0; i < gelenArguman; ++i) {
        ciftArgumanlar.push_back(
            sayiDegeri(args[i + 1], satir, "ffi.cagir_tanimli"));
      }

      try {
        return OrhunDegeri(ffiCiftCagir(fonksiyon, ciftArgumanlar));
      } catch (const std::exception &ex) {
        hataFirlat(satir, "ffi.cagir_tanimli çalışma hatası: " +
                              std::string(ex.what()));
      }
    }

    std::vector<std::intptr_t> hamArgumanlar;
    hamArgumanlar.reserve(gelenArguman);
    std::vector<std::string> metinSahipligi;
    metinSahipligi.reserve(gelenArguman);

    for (std::size_t i = 0; i < gelenArguman; ++i) {
      const FFIType tip = baglanti.imza.argumanTipleri[i];
      const OrhunDegeri &arg = args[i + 1];

      switch (tip) {
      case FFIType::INT64:
      case FFIType::POINTER: {
        if (std::holds_alternative<int>(arg.veri)) {
          hamArgumanlar.push_back(
              static_cast<std::intptr_t>(std::get<int>(arg.veri)));
          break;
        }
        if (std::holds_alternative<double>(arg.veri)) {
          const double d = std::get<double>(arg.veri);
          if (!tamSayiMi(d)) {
            hataFirlat(satir, "ffi.cagir_tanimli: #" + std::to_string(i + 1) +
                                  " argümanı tam sayı/pointer olmalıdır.");
          }
          hamArgumanlar.push_back(static_cast<std::intptr_t>(d));
          break;
        }
        hataFirlat(satir, "ffi.cagir_tanimli: #" + std::to_string(i + 1) +
                              " argümanı tam sayı/pointer olmalıdır.");
      }
      case FFIType::STRING: {
        if (!std::holds_alternative<std::string>(arg.veri)) {
          hataFirlat(satir, "ffi.cagir_tanimli: #" + std::to_string(i + 1) +
                                " argümanı metin olmalıdır.");
        }
        metinSahipligi.push_back(std::get<std::string>(arg.veri));
        hamArgumanlar.push_back(
            reinterpret_cast<std::intptr_t>(metinSahipligi.back().c_str()));
        break;
      }
      case FFIType::DOUBLE:
        hataFirlat(satir, "ffi.cagir_tanimli: DOUBLE argümanları için tüm imza "
                          "DOUBLE olmalıdır. "
                          "Karışık ABI için libffi gerekir.");
      case FFIType::NONE:
        hataFirlat(satir, "ffi.cagir_tanimli: VOID argüman tipi geçersiz.");
      }
    }

    try {
      const std::intptr_t donus = ffiHamCagir(fonksiyon, hamArgumanlar);
      switch (baglanti.imza.donusTipi) {
      case FFIType::NONE:
        return OrhunDegeri(0);
      case FFIType::INT64:
      case FFIType::POINTER:
        if (donus <=
                static_cast<std::intptr_t>(std::numeric_limits<int>::max()) &&
            donus >=
                static_cast<std::intptr_t>(std::numeric_limits<int>::min())) {
          return OrhunDegeri(static_cast<int>(donus));
        }
        return OrhunDegeri(static_cast<double>(donus));
      case FFIType::STRING: {
        if (donus == 0) {
          return OrhunDegeri(std::string{});
        }
        const char *metin = reinterpret_cast<const char *>(donus);
        return OrhunDegeri(std::string(metin));
      }
      case FFIType::DOUBLE:
        hataFirlat(satir, "ffi.cagir_tanimli: DOUBLE dönüş tipinde dahili "
                          "çağrı yolu seçimi başarısız.");
      }
    } catch (const std::exception &ex) {
      hataFirlat(satir,
                 "ffi.cagir_tanimli çalışma hatası: " + std::string(ex.what()));
    }
    return OrhunDegeri(0);
  };

  gomuluIslevler_["ffi.bosalt"] = [this](const std::vector<OrhunDegeri> &args,
                                         std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "ffi.bosalt(tutamac) tek argüman alır.");
    }

    int kimlik = 0;
    if (std::holds_alternative<int>(args[0].veri)) {
      kimlik = std::get<int>(args[0].veri);
    } else if (std::holds_alternative<double>(args[0].veri)) {
      const double d = std::get<double>(args[0].veri);
      if (!tamSayiMi(d)) {
        hataFirlat(satir, "ffi.bosalt için tutamac tam sayı olmalıdır.");
      }
      kimlik = static_cast<int>(d);
    } else {
      hataFirlat(satir, "ffi.bosalt için tutamac tam sayı olmalıdır.");
    }

    const auto it = ffiKutuphaneleri_.find(kimlik);
    if (it == ffiKutuphaneleri_.end()) {
      return OrhunDegeri(0);
    }

    if (it->second) {
      it->second->close();
    }
    ffiKutuphaneleri_.erase(it);
    for (auto kimlikIt = ffiKutuphaneKimlikleri_.begin();
         kimlikIt != ffiKutuphaneKimlikleri_.end();) {
      if (kimlikIt->second == kimlik) {
        kimlikIt = ffiKutuphaneKimlikleri_.erase(kimlikIt);
      } else {
        ++kimlikIt;
      }
    }
    for (auto islevIt = ffiIslevBaglantilari_.begin();
         islevIt != ffiIslevBaglantilari_.end();) {
      if (islevIt->second.kutuphaneKimligi == kimlik) {
        islevIt = ffiIslevBaglantilari_.erase(islevIt);
      } else {
        ++islevIt;
      }
    }
    return OrhunDegeri(1);
  };

  // Dahili grafik modülü (MVP, Windows).
  gomuluIslevler_["grafik.pencere_ac"] =
      [this](const std::vector<OrhunDegeri> &args,
             std::size_t satir) -> OrhunDegeri {
    if (args.size() != 3) {
      hataFirlat(satir, "grafik.pencere_ac(\"Baslik\", genislik, yukseklik) üç "
                        "argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir,
                 "grafik.pencere_ac ilk argüman olarak metin başlık bekler.");
    }

#ifdef _WIN32
    const double g = sayiDegeri(args[1], satir, "grafik.pencere_ac");
    const double y = sayiDegeri(args[2], satir, "grafik.pencere_ac");
    if (!tamSayiMi(g) || !tamSayiMi(y)) {
      hataFirlat(satir,
                 "grafik.pencere_ac genislik/yukseklik tam sayı olmalıdır.");
    }
    grafikPencereAc(utf8denWstringe(std::get<std::string>(args[0].veri)),
                    static_cast<int>(g), static_cast<int>(y));
    return OrhunDegeri(1);
#else
    hataFirlat(satir,
               "grafik modülü şu an yalnızca Windows üzerinde destekleniyor.");
#endif
  };

  gomuluIslevler_["grafik.temizle"] =
      [this](const std::vector<OrhunDegeri> &args,
             std::size_t satir) -> OrhunDegeri {
    if (args.size() != 3) {
      hataFirlat(satir, "grafik.temizle(r, g, b) üç argüman alır.");
    }
#ifdef _WIN32
    const int r =
        static_cast<int>(sayiDegeri(args[0], satir, "grafik.temizle"));
    const int g =
        static_cast<int>(sayiDegeri(args[1], satir, "grafik.temizle"));
    const int b =
        static_cast<int>(sayiDegeri(args[2], satir, "grafik.temizle"));
    grafikTemizle(r, g, b);
    return OrhunDegeri(1);
#else
    hataFirlat(satir,
               "grafik modülü şu an yalnızca Windows üzerinde destekleniyor.");
#endif
  };

  gomuluIslevler_["grafik.dikdortgen"] =
      [this](const std::vector<OrhunDegeri> &args,
             std::size_t satir) -> OrhunDegeri {
    if (args.size() != 7) {
      hataFirlat(satir,
                 "grafik.dikdortgen(x, y, w, h, r, g, b) yedi argüman alır.");
    }
#ifdef _WIN32
    const int x =
        static_cast<int>(sayiDegeri(args[0], satir, "grafik.dikdortgen"));
    const int y =
        static_cast<int>(sayiDegeri(args[1], satir, "grafik.dikdortgen"));
    const int w =
        static_cast<int>(sayiDegeri(args[2], satir, "grafik.dikdortgen"));
    const int h =
        static_cast<int>(sayiDegeri(args[3], satir, "grafik.dikdortgen"));
    const int r =
        static_cast<int>(sayiDegeri(args[4], satir, "grafik.dikdortgen"));
    const int g =
        static_cast<int>(sayiDegeri(args[5], satir, "grafik.dikdortgen"));
    const int b =
        static_cast<int>(sayiDegeri(args[6], satir, "grafik.dikdortgen"));
    grafikDikdortgen(x, y, w, h, r, g, b);
    return OrhunDegeri(1);
#else
    hataFirlat(satir,
               "grafik modülü şu an yalnızca Windows üzerinde destekleniyor.");
#endif
  };

  gomuluIslevler_["grafik.cizgi"] = [this](const std::vector<OrhunDegeri> &args,
                                           std::size_t satir) -> OrhunDegeri {
    if (args.size() != 7) {
      hataFirlat(satir,
                 "grafik.cizgi(x1, y1, x2, y2, r, g, b) yedi argüman alır.");
    }
#ifdef _WIN32
    const int x1 = static_cast<int>(sayiDegeri(args[0], satir, "grafik.cizgi"));
    const int y1 = static_cast<int>(sayiDegeri(args[1], satir, "grafik.cizgi"));
    const int x2 = static_cast<int>(sayiDegeri(args[2], satir, "grafik.cizgi"));
    const int y2 = static_cast<int>(sayiDegeri(args[3], satir, "grafik.cizgi"));
    const int r = static_cast<int>(sayiDegeri(args[4], satir, "grafik.cizgi"));
    const int g = static_cast<int>(sayiDegeri(args[5], satir, "grafik.cizgi"));
    const int b = static_cast<int>(sayiDegeri(args[6], satir, "grafik.cizgi"));
    grafikCizgi(x1, y1, x2, y2, r, g, b);
    return OrhunDegeri(1);
#else
    hataFirlat(satir,
               "grafik modülü şu an yalnızca Windows üzerinde destekleniyor.");
#endif
  };

  gomuluIslevler_["grafik.yazi"] = [this](const std::vector<OrhunDegeri> &args,
                                          std::size_t satir) -> OrhunDegeri {
    if (args.size() != 6) {
      hataFirlat(satir,
                 "grafik.yazi(\"metin\", x, y, r, g, b) altı argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "grafik.yazi ilk argüman olarak metin bekler.");
    }
#ifdef _WIN32
    const int x = static_cast<int>(sayiDegeri(args[1], satir, "grafik.yazi"));
    const int y = static_cast<int>(sayiDegeri(args[2], satir, "grafik.yazi"));
    const int r = static_cast<int>(sayiDegeri(args[3], satir, "grafik.yazi"));
    const int g = static_cast<int>(sayiDegeri(args[4], satir, "grafik.yazi"));
    const int b = static_cast<int>(sayiDegeri(args[5], satir, "grafik.yazi"));
    grafikYazi(utf8denWstringe(std::get<std::string>(args[0].veri)), x, y, r, g,
               b);
    return OrhunDegeri(1);
#else
    hataFirlat(satir,
               "grafik modülü şu an yalnızca Windows üzerinde destekleniyor.");
#endif
  };

  gomuluIslevler_["grafik.guncelle"] =
      [this](const std::vector<OrhunDegeri> &args,
             std::size_t satir) -> OrhunDegeri {
    if (!args.empty()) {
      hataFirlat(satir, "grafik.guncelle() argüman almaz.");
    }
#ifdef _WIN32
    grafikGuncelle();
    return OrhunDegeri(1);
#else
    hataFirlat(satir,
               "grafik modülü şu an yalnızca Windows üzerinde destekleniyor.");
#endif
  };

  gomuluIslevler_["grafik.bekle"] = [this](const std::vector<OrhunDegeri> &args,
                                           std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "grafik.bekle(milisaniye) tek argüman alır.");
    }
#ifdef _WIN32
    const int ms = static_cast<int>(sayiDegeri(args[0], satir, "grafik.bekle"));
    grafikBekle(ms);
    return OrhunDegeri(1);
#else
    hataFirlat(satir,
               "grafik modülü şu an yalnızca Windows üzerinde destekleniyor.");
#endif
  };

  gomuluIslevler_["grafik.kapat"] = [this](const std::vector<OrhunDegeri> &args,
                                           std::size_t satir) -> OrhunDegeri {
    if (!args.empty()) {
      hataFirlat(satir, "grafik.kapat() argüman almaz.");
    }
#ifdef _WIN32
    grafikKapat();
    return OrhunDegeri(1);
#else
    hataFirlat(satir,
               "grafik modülü şu an yalnızca Windows üzerinde destekleniyor.");
#endif
  };

  // Matematik kütüphanesi.
  gomuluIslevler_["karekok"] = [this](const std::vector<OrhunDegeri> &args,
                                      std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "karekok(x) tek argüman alır.");
    }
    const double x = sayiDegeri(args[0], satir, "karekok");
    if (x < 0.0) {
      hataFirlat(satir, "karekok negatif sayı için tanımsızdır.");
    }
    return OrhunDegeri(std::sqrt(x));
  };

  gomuluIslevler_["us"] = [this](const std::vector<OrhunDegeri> &args,
                                 std::size_t satir) -> OrhunDegeri {
    if (args.size() != 2) {
      hataFirlat(satir, "us(taban, kuvvet) iki argüman alır.");
    }
    const double taban = sayiDegeri(args[0], satir, "us");
    const double kuvvet = sayiDegeri(args[1], satir, "us");
    return OrhunDegeri(std::pow(taban, kuvvet));
  };

  gomuluIslevler_["mutlak"] = [this](const std::vector<OrhunDegeri> &args,
                                     std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "mutlak(x) tek argüman alır.");
    }
    if (std::holds_alternative<int>(args[0].veri)) {
      return OrhunDegeri(std::abs(std::get<int>(args[0].veri)));
    }
    return OrhunDegeri(std::fabs(sayiDegeri(args[0], satir, "mutlak")));
  };

  gomuluIslevler_["sin"] = [this](const std::vector<OrhunDegeri> &args,
                                  std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "sin(x) tek argüman alır.");
    }
    return OrhunDegeri(std::sin(sayiDegeri(args[0], satir, "sin")));
  };

  gomuluIslevler_["cos"] = [this](const std::vector<OrhunDegeri> &args,
                                  std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "cos(x) tek argüman alır.");
    }
    return OrhunDegeri(std::cos(sayiDegeri(args[0], satir, "cos")));
  };

  gomuluIslevler_["tan"] = [this](const std::vector<OrhunDegeri> &args,
                                  std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "tan(x) tek argüman alır.");
    }
    return OrhunDegeri(std::tan(sayiDegeri(args[0], satir, "tan")));
  };

  gomuluIslevler_["yuvarla"] = [this](const std::vector<OrhunDegeri> &args,
                                      std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "yuvarla(x) tek argüman alır.");
    }
    const double deger = std::round(sayiDegeri(args[0], satir, "yuvarla"));
    if (deger > static_cast<double>(std::numeric_limits<int>::max()) ||
        deger < static_cast<double>(std::numeric_limits<int>::min())) {
      hataFirlat(satir, "yuvarla sonucu int aralığını aşıyor.");
    }
    return OrhunDegeri(static_cast<int>(deger));
  };

  // Rastgelelik ve zaman.
  gomuluIslevler_["rastgele"] = [this](const std::vector<OrhunDegeri> &args,
                                       std::size_t satir) -> OrhunDegeri {
    if (args.size() != 2) {
      hataFirlat(satir, "rastgele(min, max) iki argüman alır.");
    }
    if (!tamSayiMi(args[0]) || !tamSayiMi(args[1])) {
      hataFirlat(satir,
                 "rastgele(min, max) için min ve max tam sayı olmalıdır.");
    }

    const long long minDeger =
        static_cast<long long>(sayiDegeri(args[0], satir, "rastgele"));
    const long long maxDeger =
        static_cast<long long>(sayiDegeri(args[1], satir, "rastgele"));
    if (minDeger > maxDeger) {
      hataFirlat(satir, "rastgele(min, max) içinde min, max'tan büyük olamaz.");
    }
    if (minDeger < static_cast<long long>(std::numeric_limits<int>::min()) ||
        maxDeger > static_cast<long long>(std::numeric_limits<int>::max())) {
      hataFirlat(satir, "rastgele aralığı int sınırları içinde olmalıdır.");
    }

    static std::mt19937 ureteci(std::random_device{}());
    std::uniform_int_distribution<int> dagilim(static_cast<int>(minDeger),
                                               static_cast<int>(maxDeger));
    return OrhunDegeri(dagilim(ureteci));
  };

  gomuluIslevler_["bekle"] = [this](const std::vector<OrhunDegeri> &args,
                                    std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "bekle(saniye) tek argüman alır.");
    }
    const double saniye = sayiDegeri(args[0], satir, "bekle");
    if (saniye < 0.0) {
      hataFirlat(satir, "bekle(saniye) negatif olamaz.");
    }
    std::this_thread::sleep_for(std::chrono::duration<double>(saniye));
    return OrhunDegeri(1);
  };

  gomuluIslevler_["zaman"] = [this](const std::vector<OrhunDegeri> &args,
                                    std::size_t satir) -> OrhunDegeri {
    if (!args.empty()) {
      hataFirlat(satir, "zaman() argüman almaz.");
    }
    const auto suan = std::chrono::system_clock::now();
    const auto epochSaniye = std::chrono::duration_cast<std::chrono::seconds>(
                                 suan.time_since_epoch())
                                 .count();
    if (epochSaniye > static_cast<long long>(std::numeric_limits<int>::max()) ||
        epochSaniye < static_cast<long long>(std::numeric_limits<int>::min())) {
      return OrhunDegeri(static_cast<double>(epochSaniye));
    }
    return OrhunDegeri(static_cast<int>(epochSaniye));
  };

  // Metin işleme.
  gomuluIslevler_["buyuk_harf"] = [this](const std::vector<OrhunDegeri> &args,
                                         std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "buyuk_harf(metin) tek argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "buyuk_harf(metin) için metin argümanı bekleniyor.");
    }
    std::string metin = std::get<std::string>(args[0].veri);
    std::transform(
        metin.begin(), metin.end(), metin.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return OrhunDegeri(std::move(metin));
  };

  gomuluIslevler_["kucuk_harf"] = [this](const std::vector<OrhunDegeri> &args,
                                         std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "kucuk_harf(metin) tek argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "kucuk_harf(metin) için metin argümanı bekleniyor.");
    }
    std::string metin = std::get<std::string>(args[0].veri);
    std::transform(
        metin.begin(), metin.end(), metin.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return OrhunDegeri(std::move(metin));
  };

  gomuluIslevler_["parcala"] = [this](const std::vector<OrhunDegeri> &args,
                                      std::size_t satir) -> OrhunDegeri {
    if (args.size() != 2) {
      hataFirlat(satir, "parcala(metin, ayirici) iki argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri) ||
        !std::holds_alternative<std::string>(args[1].veri)) {
      hataFirlat(
          satir,
          "parcala(metin, ayirici) yalnızca metin argümanları kabul eder.");
    }

    const std::string metin = std::get<std::string>(args[0].veri);
    const std::string ayirici = std::get<std::string>(args[1].veri);
    OrhunDegeri::ListeVeri sonuc;

    if (ayirici.empty()) {
      sonuc.reserve(metin.size());
      for (char c : metin) {
        sonuc.emplace_back(std::string(1, c));
      }
      return OrhunDegeri(std::move(sonuc));
    }

    std::size_t baslangic = 0;
    while (true) {
      const std::size_t konum = metin.find(ayirici, baslangic);
      if (konum == std::string::npos) {
        sonuc.emplace_back(metin.substr(baslangic));
        break;
      }
      sonuc.emplace_back(metin.substr(baslangic, konum - baslangic));
      baslangic = konum + ayirici.size();
    }

    return OrhunDegeri(std::move(sonuc));
  };

  gomuluIslevler_["birlestir"] = [this](const std::vector<OrhunDegeri> &args,
                                        std::size_t satir) -> OrhunDegeri {
    if (args.size() != 2) {
      hataFirlat(satir, "birlestir(liste, ayirici) iki argüman alır.");
    }
    if (!std::holds_alternative<OrhunDegeri::ListeTipi>(args[0].veri) ||
        !std::holds_alternative<std::string>(args[1].veri)) {
      hataFirlat(satir,
                 "birlestir(liste, ayirici) için liste ve metin bekleniyor.");
    }

    const auto &listePtr = std::get<OrhunDegeri::ListeTipi>(args[0].veri);
    const std::string ayirici = std::get<std::string>(args[1].veri);
    std::string sonuc;

    const OrhunDegeri::ListeVeri bosListe;
    const auto &liste = listePtr ? *listePtr : bosListe;
    for (std::size_t i = 0; i < liste.size(); ++i) {
      if (i > 0) {
        sonuc += ayirici;
      }
      sonuc += metneCevir(liste[i]);
    }

    return OrhunDegeri(std::move(sonuc));
  };

  gomuluIslevler_["metin_uzunluk"] =
      [this](const std::vector<OrhunDegeri> &args,
             std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "metin_uzunluk(metin) tek argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "metin_uzunluk(metin) için metin bekleniyor.");
    }
    return OrhunDegeri(
        static_cast<int>(std::get<std::string>(args[0].veri).size()));
  };

  gomuluIslevler_["icerir"] = [this](const std::vector<OrhunDegeri> &args,
                                     std::size_t satir) -> OrhunDegeri {
    if (args.size() != 2) {
      hataFirlat(satir, "icerir(metin, aranan) iki argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri) ||
        !std::holds_alternative<std::string>(args[1].veri)) {
      hataFirlat(
          satir,
          "icerir(metin, aranan) yalnızca metin argümanları kabul eder.");
    }
    const std::string &metin = std::get<std::string>(args[0].veri);
    const std::string &aranan = std::get<std::string>(args[1].veri);
    return OrhunDegeri(metin.find(aranan) != std::string::npos ? 1 : 0);
  };

  // Regex modülü.
  gomuluIslevler_["regex.eslesir"] =
      [this](const std::vector<OrhunDegeri> &args,
             std::size_t satir) -> OrhunDegeri {
    if (args.size() != 2) {
      hataFirlat(satir, "regex.eslesir(metin, desen) iki argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri) ||
        !std::holds_alternative<std::string>(args[1].veri)) {
      hataFirlat(
          satir,
          "regex.eslesir için metin ve desen argümanları metin olmalıdır.");
    }
    try {
      const std::regex desen(std::get<std::string>(args[1].veri));
      const bool eslesme =
          std::regex_search(std::get<std::string>(args[0].veri), desen);
      return OrhunDegeri(eslesme ? 1 : 0);
    } catch (const std::regex_error &ex) {
      hataFirlat(satir,
                 "regex.eslesir deseni geçersiz: " + std::string(ex.what()));
    }
  };

  gomuluIslevler_["regex.ilk"] = [this](const std::vector<OrhunDegeri> &args,
                                        std::size_t satir) -> OrhunDegeri {
    if (args.size() != 2) {
      hataFirlat(satir, "regex.ilk(metin, desen) iki argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri) ||
        !std::holds_alternative<std::string>(args[1].veri)) {
      hataFirlat(satir,
                 "regex.ilk için metin ve desen argümanları metin olmalıdır.");
    }
    try {
      const std::regex desen(std::get<std::string>(args[1].veri));
      std::smatch sonuc;
      const std::string metin = std::get<std::string>(args[0].veri);
      if (!std::regex_search(metin, sonuc, desen)) {
        return OrhunDegeri("");
      }
      return OrhunDegeri(sonuc.str());
    } catch (const std::regex_error &ex) {
      hataFirlat(satir, "regex.ilk deseni geçersiz: " + std::string(ex.what()));
    }
  };

  gomuluIslevler_["regex.tum"] = [this](const std::vector<OrhunDegeri> &args,
                                        std::size_t satir) -> OrhunDegeri {
    if (args.size() != 2) {
      hataFirlat(satir, "regex.tum(metin, desen) iki argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri) ||
        !std::holds_alternative<std::string>(args[1].veri)) {
      hataFirlat(satir,
                 "regex.tum için metin ve desen argümanları metin olmalıdır.");
    }
    try {
      const std::regex desen(std::get<std::string>(args[1].veri));
      const std::string metin = std::get<std::string>(args[0].veri);
      OrhunDegeri::ListeVeri sonuc;
      for (std::sregex_iterator it(metin.begin(), metin.end(), desen), son;
           it != son; ++it) {
        sonuc.emplace_back(it->str());
      }
      return OrhunDegeri(std::move(sonuc));
    } catch (const std::regex_error &ex) {
      hataFirlat(satir, "regex.tum deseni geçersiz: " + std::string(ex.what()));
    }
  };

  gomuluIslevler_["regex.degistir"] =
      [this](const std::vector<OrhunDegeri> &args,
             std::size_t satir) -> OrhunDegeri {
    if (args.size() != 3) {
      hataFirlat(satir, "regex.degistir(metin, desen, yeni) üç argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri) ||
        !std::holds_alternative<std::string>(args[1].veri) ||
        !std::holds_alternative<std::string>(args[2].veri)) {
      hataFirlat(satir, "regex.degistir için tüm argümanlar metin olmalıdır.");
    }
    try {
      const std::regex desen(std::get<std::string>(args[1].veri));
      return OrhunDegeri(
          std::regex_replace(std::get<std::string>(args[0].veri), desen,
                             std::get<std::string>(args[2].veri)));
    } catch (const std::regex_error &ex) {
      hataFirlat(satir,
                 "regex.degistir deseni geçersiz: " + std::string(ex.what()));
    }
  };

  // Tarih/Saat modülü.
  auto yerelZamaniFormatla = [this](const char *bicim,
                                    std::size_t satir) -> std::string {
    std::time_t an = std::time(nullptr);
    if (an == static_cast<std::time_t>(-1)) {
      hataFirlat(satir, "tarih zaman bilgisi okunamadı.");
    }

    std::tm tmDegeri{};
#ifdef _WIN32
    if (localtime_s(&tmDegeri, &an) != 0) {
      hataFirlat(satir, "tarih yerel zaman dönüşümü başarısız.");
    }
#else
    if (localtime_r(&an, &tmDegeri) == nullptr) {
      hataFirlat(satir, "tarih yerel zaman dönüşümü başarısız.");
    }
#endif
    char tampon[64];
    const std::size_t yazilan =
        std::strftime(tampon, sizeof(tampon), bicim, &tmDegeri);
    if (yazilan == 0) {
      hataFirlat(satir, "tarih biçimlendirilemedi.");
    }
    return std::string(tampon, yazilan);
  };

  gomuluIslevler_["tarih.simdi"] =
      [this, yerelZamaniFormatla](const std::vector<OrhunDegeri> &args,
                                  std::size_t satir) -> OrhunDegeri {
    if (!args.empty()) {
      hataFirlat(satir, "tarih.simdi() argüman almaz.");
    }
    return OrhunDegeri(yerelZamaniFormatla("%Y-%m-%d %H:%M:%S", satir));
  };

  gomuluIslevler_["tarih.bugun"] =
      [this, yerelZamaniFormatla](const std::vector<OrhunDegeri> &args,
                                  std::size_t satir) -> OrhunDegeri {
    if (!args.empty()) {
      hataFirlat(satir, "tarih.bugun() argüman almaz.");
    }
    return OrhunDegeri(yerelZamaniFormatla("%Y-%m-%d", satir));
  };

  gomuluIslevler_["tarih.unix"] = [this](const std::vector<OrhunDegeri> &args,
                                         std::size_t satir) -> OrhunDegeri {
    if (!args.empty()) {
      hataFirlat(satir, "tarih.unix() argüman almaz.");
    }
    const std::time_t an = std::time(nullptr);
    if (an == static_cast<std::time_t>(-1)) {
      hataFirlat(satir, "tarih.unix() zamanı okunamadı.");
    }
    return OrhunDegeri(static_cast<int>(an));
  };

  // Tip dönüşümleri.
  gomuluIslevler_["sayiya_cevir"] = [this](const std::vector<OrhunDegeri> &args,
                                           std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "sayiya_cevir(metin) tek argüman alır.");
    }
    if (!std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "sayiya_cevir(metin) için metin bekleniyor.");
    }

    std::string metin = std::get<std::string>(args[0].veri);
    const auto sol = metin.find_first_not_of(" \t\r\n");
    const auto sag = metin.find_last_not_of(" \t\r\n");
    if (sol == std::string::npos) {
      hataFirlat(satir, "sayiya_cevir için boş metin verildi.");
    }
    metin = metin.substr(sol, sag - sol + 1);

    try {
      std::size_t idx = 0;
      const int tam = std::stoi(metin, &idx, 10);
      if (idx == metin.size()) {
        return OrhunDegeri(tam);
      }
    } catch (...) {
      // tam sayı değilse aşağıda ondalık denenecek.
    }

    try {
      std::size_t idx = 0;
      const double ondalik = std::stod(metin, &idx);
      if (idx == metin.size()) {
        return OrhunDegeri(ondalik);
      }
    } catch (...) {
      // aşağıda hata fırlatılacak.
    }

    hataFirlat(satir, "'" + metin + "' sayıya çevrilemedi.");
  };

  gomuluIslevler_["metne_cevir"] = [this](const std::vector<OrhunDegeri> &args,
                                          std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "metne_cevir(deger) tek argüman alır.");
    }
    return OrhunDegeri(metneCevir(args[0]));
  };

  // yazdır ve sor, dilde ayrıca anahtar kelime olarak da var.
  gomuluIslevler_["yazdır"] = [this](const std::vector<OrhunDegeri> &args,
                                     std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "yazdır tek argüman alır.");
    }
    std::cout << metneCevir(args[0]) << '\n';
    return args[0];
  };
  gomuluIslevler_["yaz"] = gomuluIslevler_["yazdır"];

  gomuluIslevler_["sor"] = [this](const std::vector<OrhunDegeri> &args,
                                  std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1) {
      hataFirlat(satir, "sor tek argüman alır.");
    }
    std::cout << metneCevir(args[0]);
    std::cout.flush();

    std::string giris;
    std::getline(std::cin, giris);
    if (!std::cin && giris.empty()) {
      hataFirlat(satir, "Kullanıcı girdisi okunamadı.");
    }
    return OrhunDegeri(std::move(giris));
  };
  gomuluIslevler_["oku"] = gomuluIslevler_["sor"];
}

void Interpreter::yerlesikModulleriYukle() {
  // Modül isim uzayı altında aynı gömülü işlevlerin aliaslarını üret.
  gomuluIslevler_["matematik.sin"] = gomuluIslevler_["sin"];
  gomuluIslevler_["matematik.cos"] = gomuluIslevler_["cos"];
  gomuluIslevler_["matematik.tan"] = gomuluIslevler_["tan"];
  gomuluIslevler_["matematik.karekok"] = gomuluIslevler_["karekok"];
  gomuluIslevler_["matematik.us"] = gomuluIslevler_["us"];
  gomuluIslevler_["matematik.rastgele"] = gomuluIslevler_["rastgele"];

  gomuluIslevler_["zaman.simdi"] = gomuluIslevler_["zaman"];
  gomuluIslevler_["zaman.bekle"] = gomuluIslevler_["bekle"];
  gomuluIslevler_["metin.buyuk"] = gomuluIslevler_["buyuk_harf"];
  gomuluIslevler_["metin.kucuk"] = gomuluIslevler_["kucuk_harf"];
  gomuluIslevler_["metin.parcala"] = gomuluIslevler_["parcala"];
  gomuluIslevler_["metin.birlestir"] = gomuluIslevler_["birlestir"];
  gomuluIslevler_["metin.uzunluk"] = gomuluIslevler_["metin_uzunluk"];
  gomuluIslevler_["metin.utf8_uzunluk"] =
      [this](const std::vector<OrhunDegeri> &args,
             std::size_t satir) -> OrhunDegeri {
    if (args.size() != 1 ||
        !std::holds_alternative<std::string>(args[0].veri)) {
      hataFirlat(satir, "metin.utf8_uzunluk(metin) tek metin argumani alir.");
    }
    return OrhunDegeri(static_cast<int>(
        utf8KodNoktalarinaCevir(std::get<std::string>(args[0].veri)).size()));
  };
  gomuluIslevler_["metin.icerir"] = gomuluIslevler_["icerir"];
  gomuluIslevler_["dosya.oku"] = gomuluIslevler_["dosya_oku"];
  gomuluIslevler_["dosya.yaz"] = gomuluIslevler_["dosyaya_yaz"];

  // Python benzeri erişim için hazır sözlük modülleri.
  OrhunDegeri::SozlukVeri matematik;
  matematik["pi"] = OrhunDegeri(3.14159265358979323846);
  matematik["sin"] = OrhunDegeri("__islev_ref__:matematik.sin");
  matematik["cos"] = OrhunDegeri("__islev_ref__:matematik.cos");
  matematik["tan"] = OrhunDegeri("__islev_ref__:matematik.tan");
  matematik["karekok"] = OrhunDegeri("__islev_ref__:matematik.karekok");
  matematik["us"] = OrhunDegeri("__islev_ref__:matematik.us");
  matematik["rastgele"] = OrhunDegeri("__islev_ref__:matematik.rastgele");
  globalHafiza_["matematik"] = OrhunDegeri(std::move(matematik));

  OrhunDegeri::SozlukVeri zaman;
  zaman["simdi"] = OrhunDegeri("__islev_ref__:zaman.simdi");
  zaman["bekle"] = OrhunDegeri("__islev_ref__:zaman.bekle");
  globalHafiza_["zaman"] = OrhunDegeri(std::move(zaman));

  OrhunDegeri::SozlukVeri internet;
  internet["getir"] = OrhunDegeri("__islev_ref__:internet.getir");
  internet["indir"] = OrhunDegeri("__islev_ref__:internet.indir");
  globalHafiza_["internet"] = OrhunDegeri(std::move(internet));

  OrhunDegeri::SozlukVeri veritabani;
  veritabani["kaydet"] = OrhunDegeri("__islev_ref__:veritabani.kaydet");
  veritabani["oku"] = OrhunDegeri("__islev_ref__:veritabani.oku");
  veritabani["sil"] = OrhunDegeri("__islev_ref__:veritabani.sil");
  veritabani["listele"] = OrhunDegeri("__islev_ref__:veritabani.listele");
  globalHafiza_["veritabani"] = OrhunDegeri(std::move(veritabani));

  OrhunDegeri::SozlukVeri sunucu;
  sunucu["baslat"] = OrhunDegeri("__islev_ref__:sunucu.baslat");
  sunucu["durdur"] = OrhunDegeri("__islev_ref__:sunucu.durdur");
  sunucu["calisiyor_mu"] = OrhunDegeri("__islev_ref__:sunucu.calisiyor_mu");
  globalHafiza_["sunucu"] = OrhunDegeri(std::move(sunucu));

  OrhunDegeri::SozlukVeri json;
  json["coz"] = OrhunDegeri("__islev_ref__:json.coz");
  json["yaz"] = OrhunDegeri("__islev_ref__:json.yaz");
  json["guzel_yaz"] = OrhunDegeri("__islev_ref__:json.guzel_yaz");
  globalHafiza_["json"] = OrhunDegeri(std::move(json));

  OrhunDegeri::SozlukVeri sistem;
  OrhunDegeri::ListeVeri programArgumanlari;
  programArgumanlari.reserve(programArgumanlari_.size());
  for (const std::string &arguman : programArgumanlari_) {
    programArgumanlari.emplace_back(arguman);
  }
  sistem["argumanlar"] = OrhunDegeri(std::move(programArgumanlari));
  sistem["komut"] = OrhunDegeri("__islev_ref__:sistem.komut");
  globalHafiza_["sistem"] = OrhunDegeri(std::move(sistem));

  OrhunDegeri::SozlukVeri sonuc;
  sonuc["ok"] = OrhunDegeri("__islev_ref__:sonuc.ok");
  sonuc["hata"] = OrhunDegeri("__islev_ref__:sonuc.hata");
  globalHafiza_["sonuc"] = OrhunDegeri(std::move(sonuc));

  OrhunDegeri::SozlukVeri dosya;
  dosya["oku"] = OrhunDegeri("__islev_ref__:dosya.oku");
  dosya["yaz"] = OrhunDegeri("__islev_ref__:dosya.yaz");
  dosya["var_mi"] = OrhunDegeri("__islev_ref__:dosya.var_mi");
  dosya["sil"] = OrhunDegeri("__islev_ref__:dosya.sil");
  dosya["ekle_satir"] = OrhunDegeri("__islev_ref__:dosya.ekle_satir");
  dosya["listele"] = OrhunDegeri("__islev_ref__:dosya.listele");
  dosya["klasor_olustur"] = OrhunDegeri("__islev_ref__:dosya.klasor_olustur");
  globalHafiza_["dosya"] = OrhunDegeri(std::move(dosya));

  OrhunDegeri::SozlukVeri metin;
  metin["buyuk"] = OrhunDegeri("__islev_ref__:metin.buyuk");
  metin["kucuk"] = OrhunDegeri("__islev_ref__:metin.kucuk");
  metin["parcala"] = OrhunDegeri("__islev_ref__:metin.parcala");
  metin["birlestir"] = OrhunDegeri("__islev_ref__:metin.birlestir");
  metin["uzunluk"] = OrhunDegeri("__islev_ref__:metin.uzunluk");
  metin["utf8_uzunluk"] = OrhunDegeri("__islev_ref__:metin.utf8_uzunluk");
  metin["icerir"] = OrhunDegeri("__islev_ref__:metin.icerir");
  globalHafiza_["metin"] = OrhunDegeri(std::move(metin));

  OrhunDegeri::SozlukVeri regex;
  regex["eslesir"] = OrhunDegeri("__islev_ref__:regex.eslesir");
  regex["ilk"] = OrhunDegeri("__islev_ref__:regex.ilk");
  regex["tum"] = OrhunDegeri("__islev_ref__:regex.tum");
  regex["degistir"] = OrhunDegeri("__islev_ref__:regex.degistir");
  globalHafiza_["regex"] = OrhunDegeri(std::move(regex));

  OrhunDegeri::SozlukVeri tarih;
  tarih["simdi"] = OrhunDegeri("__islev_ref__:tarih.simdi");
  tarih["bugun"] = OrhunDegeri("__islev_ref__:tarih.bugun");
  tarih["unix"] = OrhunDegeri("__islev_ref__:tarih.unix");
  globalHafiza_["tarih"] = OrhunDegeri(std::move(tarih));

  OrhunDegeri::SozlukVeri grafik;
  grafik["pencere_ac"] = OrhunDegeri("__islev_ref__:grafik.pencere_ac");
  grafik["temizle"] = OrhunDegeri("__islev_ref__:grafik.temizle");
  grafik["dikdortgen"] = OrhunDegeri("__islev_ref__:grafik.dikdortgen");
  grafik["cizgi"] = OrhunDegeri("__islev_ref__:grafik.cizgi");
  grafik["yazi"] = OrhunDegeri("__islev_ref__:grafik.yazi");
  grafik["guncelle"] = OrhunDegeri("__islev_ref__:grafik.guncelle");
  grafik["bekle"] = OrhunDegeri("__islev_ref__:grafik.bekle");
  grafik["kapat"] = OrhunDegeri("__islev_ref__:grafik.kapat");
  globalHafiza_["grafik"] = OrhunDegeri(std::move(grafik));

  OrhunDegeri::SozlukVeri ffi;
  ffi["yukle"] = OrhunDegeri("__islev_ref__:ffi.yukle");
  ffi["cagir"] = OrhunDegeri("__islev_ref__:ffi.cagir");
  ffi["tanimla"] = OrhunDegeri("__islev_ref__:ffi.tanimla");
  ffi["cagir_tanimli"] = OrhunDegeri("__islev_ref__:ffi.cagir_tanimli");
  ffi["sembol_var_mi"] = OrhunDegeri("__islev_ref__:ffi.sembol_var_mi");
  ffi["cagir_metin"] = OrhunDegeri("__islev_ref__:ffi.cagir_metin");
  ffi["bosalt"] = OrhunDegeri("__islev_ref__:ffi.bosalt");
  globalHafiza_["ffi"] = OrhunDegeri(std::move(ffi));
}

void Interpreter::calistir(const ASTNode *dugum) {
  if (dugum == nullptr) {
    hataFirlat(0, "Boş AST düğümü çalıştırılamaz.");
  }

  if (const auto *program = dynamic_cast<const ProgramNode *>(dugum)) {
    cagriYigini_.push_back({"<program>", program->satir()});
    try {
      for (const auto &komut : program->komutlar()) {
        calistir(komut.get());
      }
    } catch (...) {
      cagriYigini_.pop_back();
      throw;
    }
    cagriYigini_.pop_back();
    return;
  }

  if (const auto *block = dynamic_cast<const BlockNode *>(dugum)) {
    calistirBlock(block);
    return;
  }

  if (const auto *atama = dynamic_cast<const AtamaNode *>(dugum)) {
    calistirAtama(atama);
    return;
  }

  if (const auto *cokluAtama = dynamic_cast<const CokluAtamaNode *>(dugum)) {
    calistirCokluAtama(cokluAtama);
    return;
  }

  if (const auto *yazdir = dynamic_cast<const YazdirNode *>(dugum)) {
    calistirYazdir(yazdir);
    return;
  }

  if (const auto *eger = dynamic_cast<const EgerNode *>(dugum)) {
    calistirEger(eger);
    return;
  }

  if (const auto *tekrarla = dynamic_cast<const TekrarlaNode *>(dugum)) {
    calistirTekrarla(tekrarla);
    return;
  }

  if (const auto *surece = dynamic_cast<const SureceNode *>(dugum)) {
    calistirSurece(surece);
    return;
  }

  if (const auto *islev = dynamic_cast<const IslevTanimNode *>(dugum)) {
    calistirIslevTanim(islev);
    return;
  }

  if (const auto *disIslev = dynamic_cast<const DisIslevTanimNode *>(dugum)) {
    calistirDisIslevTanim(disIslev);
    return;
  }

  if (const auto *sinif = dynamic_cast<const SinifTanimNode *>(dugum)) {
    calistirSinifTanim(sinif);
    return;
  }

  if (const auto *denemeYakala =
          dynamic_cast<const DenemeYakalaNode *>(dugum)) {
    calistirDenemeYakala(denemeYakala);
    return;
  }

  if (const auto *kir = dynamic_cast<const KirNode *>(dugum)) {
    calistirKir(kir);
    return;
  }

  if (const auto *devam = dynamic_cast<const DevamNode *>(dugum)) {
    calistirDevam(devam);
    return;
  }

  if (const auto *dondur = dynamic_cast<const DondurNode *>(dugum)) {
    calistirDondur(dondur);
    return;
  }

  if (const auto *dahilEt = dynamic_cast<const DahilEtNode *>(dugum)) {
    calistirDahilEt(dahilEt);
    return;
  }

  if (const auto *ifadeKomut = dynamic_cast<const IfadeKomutNode *>(dugum)) {
    calistirIfadeKomut(ifadeKomut);
    return;
  }

  hataFirlat(dugum->satir(), "Bilinmeyen komut düğümü.");
}

void Interpreter::calistirBlock(const BlockNode *block) {
  for (const auto &komut : block->komutlar()) {
    calistir(komut.get());
  }
}

void Interpreter::calistirAtama(const AtamaNode *dugum) {
  const OrhunDegeri deger = ifadeHesapla(dugum->ifade());

  if (const auto *kimlik = dynamic_cast<const KimlikNode *>(dugum->hedef())) {
    atamaHedefiYaz(kimlik->ad(), deger, dugum->bildirimMi(), dugum->satir());
    return;
  }

  OrhunDegeri &hedef =
      atananHedefYazilabilir(dugum->hedef(), dugum->satir(), true);
  hedef = deger;
}

void Interpreter::calistirCokluAtama(const CokluAtamaNode *dugum) {
  const OrhunDegeri deger = ifadeHesapla(dugum->ifade());
  if (!std::holds_alternative<OrhunDegeri::ListeTipi>(deger.veri)) {
    hataFirlat(
        dugum->satir(),
        "Çoklu atamada sağ taraf liste olmalıdır (örn: [1, 2, 3]).");
  }

  const auto &listePtr = std::get<OrhunDegeri::ListeTipi>(deger.veri);
  if (!listePtr) {
    hataFirlat(dugum->satir(), "Çoklu atamada boş liste referansı kullanılamaz.");
  }
  if (listePtr->size() != dugum->hedefler().size()) {
    hataFirlat(dugum->satir(),
               "Çoklu atamada hedef sayısı ile liste boyutu eşleşmiyor.");
  }

  for (std::size_t i = 0; i < dugum->hedefler().size(); ++i) {
    atamaHedefiYaz(dugum->hedefler()[i], (*listePtr)[i],
                   dugum->bildirimMi(), dugum->satir());
  }
}

void Interpreter::calistirYazdir(const YazdirNode *dugum) {
  std::cout << metneCevir(ifadeHesapla(dugum->ifade())) << '\n';
}

void Interpreter::calistirEger(const EgerNode *dugum) {
  const OrhunDegeri kosul = ifadeHesapla(dugum->kosul());
  if (dogruMu(kosul)) {
    calistirBlock(dugum->dogruBlok());
  } else if (dugum->yanlisBlok() != nullptr) {
    calistirBlock(dugum->yanlisBlok());
  }
}

void Interpreter::calistirTekrarla(const TekrarlaNode *dugum) {
  const OrhunDegeri tekrarDegeri = ifadeHesapla(dugum->kacKezIfadesi());
  if (!tamSayiMi(tekrarDegeri)) {
    hataFirlat(dugum->satir(),
               "'tekrarla' ifadesinde tekrar sayısı tam sayı olmalıdır.");
  }

  const int kez = static_cast<int>(
      sayiDegeri(tekrarDegeri, dugum->satir(), "tekrar sayısı"));
  if (kez < 0) {
    hataFirlat(dugum->satir(), "'tekrarla' tekrar sayısı negatif olamaz.");
  }

  ++donguDerinligi_;
  try {
    for (int i = 0; i < kez; ++i) {
      yerelKapsamYigini_.push_back(std::make_shared<DegiskenTablosu>());
      try {
        calistirBlock(dugum->govde());
      } catch (const DevamSinyali &) {
        yerelKapsamYigini_.pop_back();
        continue;
      } catch (const KirSinyali &) {
        yerelKapsamYigini_.pop_back();
        break;
      } catch (...) {
        yerelKapsamYigini_.pop_back();
        throw;
      }
      yerelKapsamYigini_.pop_back();
    }
  } catch (...) {
    --donguDerinligi_;
    throw;
  }
  --donguDerinligi_;
}

void Interpreter::calistirSurece(const SureceNode *dugum) {
  ++donguDerinligi_;
  try {
    while (dogruMu(ifadeHesapla(dugum->kosul()))) {
      yerelKapsamYigini_.push_back(std::make_shared<DegiskenTablosu>());
      try {
        calistirBlock(dugum->govde());
      } catch (const DevamSinyali &) {
        yerelKapsamYigini_.pop_back();
        continue;
      } catch (const KirSinyali &) {
        yerelKapsamYigini_.pop_back();
        break;
      } catch (...) {
        yerelKapsamYigini_.pop_back();
        throw;
      }
      yerelKapsamYigini_.pop_back();
    }
  } catch (...) {
    --donguDerinligi_;
    throw;
  }
  --donguDerinligi_;
}

void Interpreter::calistirIslevTanim(const IslevTanimNode *dugum) {
  if (!yerelKapsamYigini_.empty()) {
    const std::string closureAdi =
        "__closure__" + std::to_string(++closureSayaci_);
    closureTablosu_[closureAdi] =
        ClosureKaydi{dugum, nullptr, yerelKapsamYigini_, dugum->ad()};
    aktifKapsam()[dugum->ad()] = OrhunDegeri("__islev_ref__:" + closureAdi);
    return;
  }

  islevTablosu_[dugum->ad()] = dugum;
  aktifKapsam()[dugum->ad()] = OrhunDegeri("__islev_ref__:" + dugum->ad());
}

void Interpreter::calistirDisIslevTanim(const DisIslevTanimNode *dugum) {
  if (gomuluIslevler_.find("ffi.yukle") == gomuluIslevler_.end() ||
      gomuluIslevler_.find("ffi.tanimla") == gomuluIslevler_.end() ||
      gomuluIslevler_.find("ffi.cagir_tanimli") == gomuluIslevler_.end()) {
    hataFirlat(dugum->satir(), "FFI altyapısı hazır değil "
                               "(ffi.yukle/tanimla/cagir_tanimli bulunamadı).");
  }

  OrhunDegeri kutuphaneKimligi;
  try {
    kutuphaneKimligi = gomuluIslevler_.at("ffi.yukle")(
        std::vector<OrhunDegeri>{OrhunDegeri(dugum->kutuphaneYolu())},
        dugum->satir());
  } catch (const std::exception &ex) {
    hataFirlat(dugum->satir(), "Dış işlev için kütüphane yüklenemedi: " +
                                   std::string(ex.what()));
  }

  OrhunDegeri::ListeVeri tipListesi;
  tipListesi.reserve(dugum->parametreTipleri().size());
  for (const std::string &tip : dugum->parametreTipleri()) {
    tipListesi.emplace_back(tip);
  }

  OrhunDegeri baglantiKimligi;
  try {
    baglantiKimligi = gomuluIslevler_.at("ffi.tanimla")(
        std::vector<OrhunDegeri>{kutuphaneKimligi, OrhunDegeri(dugum->ad()),
                                 OrhunDegeri(dugum->donusTipi()),
                                 OrhunDegeri(std::move(tipListesi))},
        dugum->satir());
  } catch (const std::exception &ex) {
    hataFirlat(dugum->satir(),
               "Dış işlev imzası oluşturulamadı: " + std::string(ex.what()));
  }

  int islevKimligi = 0;
  if (std::holds_alternative<int>(baglantiKimligi.veri)) {
    islevKimligi = std::get<int>(baglantiKimligi.veri);
  } else if (std::holds_alternative<double>(baglantiKimligi.veri)) {
    const double d = std::get<double>(baglantiKimligi.veri);
    if (!tamSayiMi(d)) {
      hataFirlat(dugum->satir(), "Dış işlev kaydı geçersiz kimlik döndürdü.");
    }
    islevKimligi = static_cast<int>(d);
  } else {
    hataFirlat(dugum->satir(), "Dış işlev kaydı geçersiz kimlik döndürdü.");
  }

  const std::string ad = dugum->ad();
  gomuluIslevler_[ad] =
      [this, islevKimligi](const std::vector<OrhunDegeri> &argumanlar,
                           std::size_t satir) -> OrhunDegeri {
    std::vector<OrhunDegeri> paketliArgumanlar;
    paketliArgumanlar.reserve(argumanlar.size() + 1);
    paketliArgumanlar.emplace_back(islevKimligi);
    paketliArgumanlar.insert(paketliArgumanlar.end(), argumanlar.begin(),
                             argumanlar.end());
    return gomuluIslevler_.at("ffi.cagir_tanimli")(paketliArgumanlar, satir);
  };

  // Aynı adda user-defined işlev varsa dış işlev tanımı geçerli olsun.
  islevTablosu_.erase(ad);
  aktifKapsam()[ad] = OrhunDegeri("__islev_ref__:" + ad);
}

void Interpreter::calistirSinifTanim(const SinifTanimNode *dugum) {
  sinifTablosu_[dugum->ad()] = dugum;
}

void Interpreter::calistirDenemeYakala(const DenemeYakalaNode *dugum) {
  try {
    calistirBlock(dugum->denemeBlogu());
  } catch (const OrhunHatasi &hata) {
    DegiskenTablosu yakalaKapsami;
    yakalaKapsami[dugum->hataDegiskeni()] =
        OrhunDegeri(yakalamaMesajiTemizle(std::string(hata.what())));
    yerelKapsamYigini_.push_back(
        std::make_shared<DegiskenTablosu>(std::move(yakalaKapsami)));
    try {
      calistirBlock(dugum->yakalaBlogu());
    } catch (...) {
      yerelKapsamYigini_.pop_back();
      throw;
    }
    yerelKapsamYigini_.pop_back();
  } catch (const std::exception &ex) {
    DegiskenTablosu yakalaKapsami;
    yakalaKapsami[dugum->hataDegiskeni()] =
        OrhunDegeri(yakalamaMesajiTemizle(std::string(ex.what())));
    yerelKapsamYigini_.push_back(
        std::make_shared<DegiskenTablosu>(std::move(yakalaKapsami)));
    try {
      calistirBlock(dugum->yakalaBlogu());
    } catch (...) {
      yerelKapsamYigini_.pop_back();
      throw;
    }
    yerelKapsamYigini_.pop_back();
  }
}

void Interpreter::calistirKir(const KirNode *dugum) {
  if (donguDerinligi_ <= 0) {
    hataFirlat(dugum->satir(), "'kır' yalnızca döngü içinde kullanılabilir.");
  }
  throw KirSinyali{};
}

void Interpreter::calistirDevam(const DevamNode *dugum) {
  if (donguDerinligi_ <= 0) {
    hataFirlat(dugum->satir(), "'devam' yalnızca döngü içinde kullanılabilir.");
  }
  throw DevamSinyali{};
}

void Interpreter::calistirDondur(const DondurNode *dugum) {
  if (yerelKapsamYigini_.empty()) {
    hataFirlat(dugum->satir(),
               "'döndür' yalnızca işlev içinde kullanılabilir.");
  }
  throw DondurSinyali(ifadeHesapla(dugum->ifade()));
}

void Interpreter::calistirDahilEt(const DahilEtNode *dugum) {
  static_cast<void>(dahilEtDegerlendir(dugum));
}

OrhunDegeri Interpreter::dahilEtDegerlendir(const DahilEtNode *dugum) {
  const std::string istenenYol = dugum->dosyaAdi();
  const auto cozulmusYol = orhunDahilYolunuCoz(istenenYol);
  if (!cozulmusYol.has_value()) {
    hataFirlat(dugum->satir(),
               "dahil_et: '" + istenenYol + "' dosyası açılamadı." +
                   orhunDahilAramaYollariMetni());
  }

  std::ifstream dosya(*cozulmusYol, std::ios::binary);
  if (!dosya.is_open()) {
    hataFirlat(dugum->satir(),
               "dahil_et: '" + istenenYol + "' dosyası açılamadı.");
  }

  std::ostringstream tampon;
  tampon << dosya.rdbuf();

  try {
    // Modül içeriğini parse et.
    Lexer lexer(tampon.str());
    std::vector<OrhunToken> tokenlar = lexer.tokenize();
    Parser parser(std::move(tokenlar));
    std::unique_ptr<ProgramNode> program = parser.parse();

    // Modül AST'sini yaşam döngüsü boyunca elde tut.
    ProgramNode *programHam = program.get();
    yukluModuller_.push_back(std::move(program));

    // Modül kapsamını mevcut global ortamdan izole etmek için anlık görüntü al.
    const DegiskenTablosu globalOncesi = globalHafiza_;
    const auto islevOncesi = islevTablosu_;

    calistir(programHam);

    // Modülde üretilen değerleri sözlükte topla.
    OrhunDegeri::SozlukVeri modulSozlugu;
    for (const auto &[ad, deger] : globalHafiza_) {
      const auto onceki = globalOncesi.find(ad);
      if (onceki == globalOncesi.end() || !(onceki->second == deger)) {
        modulSozlugu[ad] = deger;
      }
    }

    // Modül işlevlerini isim uzayına (module.func) bağlamak için takma ad üret.
    static std::size_t modulSayaci = 0;
    const std::string modulOnEki =
        "__modul__" + std::to_string(++modulSayaci) + "__";

    std::vector<std::pair<std::string, const IslevTanimNode *>> aliaslar;
    for (const auto &[islevAdi, islevDugumu] : islevTablosu_) {
      const auto onceki = islevOncesi.find(islevAdi);
      if (onceki != islevOncesi.end() && onceki->second == islevDugumu) {
        continue;
      }
      const std::string alias = modulOnEki + islevAdi;
      aliaslar.push_back({alias, islevDugumu});
      modulSozlugu[islevAdi] = OrhunDegeri("__islev_ref__:" + alias);
    }

    // Ortamı geri al ve yalnızca modül alias işlevleri sistemde bırak.
    globalHafiza_ = globalOncesi;
    islevTablosu_ = islevOncesi;
    for (const auto &[alias, islevDugumu] : aliaslar) {
      islevTablosu_[alias] = islevDugumu;
    }

    return OrhunDegeri(std::move(modulSozlugu));
  } catch (const std::exception &ex) {
    hataFirlat(dugum->satir(),
               "dahil_et içinde hata: " + std::string(ex.what()));
  }
}

void Interpreter::calistirIfadeKomut(const IfadeKomutNode *dugum) {
  if (const auto *cagri =
          dynamic_cast<const IslevCagriNode *>(dugum->ifade())) {
    static_cast<void>(islevCagir(cagri, false));
    return;
  }

  static_cast<void>(ifadeHesapla(dugum->ifade()));
}

OrhunDegeri Interpreter::ifadeHesapla(const ASTNode *dugum) {
  if (dugum == nullptr) {
    hataFirlat(0, "Boş ifade düğümü değerlendirilemez.");
  }

  if (const auto *sayi = dynamic_cast<const SayiNode *>(dugum)) {
    try {
      if (sayi->deger().find('.') != std::string::npos) {
        std::size_t okunan = 0;
        const double deger = std::stod(sayi->deger(), &okunan);
        if (okunan != sayi->deger().size()) {
          hataFirlat(sayi->satir(),
                     "'" + sayi->deger() + "' geçerli bir sayı değil.");
        }
        return OrhunDegeri(deger);
      }

      std::size_t okunan = 0;
      const int deger = std::stoi(sayi->deger(), &okunan, 10);
      if (okunan != sayi->deger().size()) {
        hataFirlat(sayi->satir(),
                   "'" + sayi->deger() + "' geçerli bir sayı değil.");
      }
      return OrhunDegeri(deger);
    } catch (const std::exception &) {
      hataFirlat(sayi->satir(),
                 "'" + sayi->deger() + "' geçerli bir sayı değil.");
    }
  }

  if (const auto *metin = dynamic_cast<const MetinNode *>(dugum)) {
    return OrhunDegeri(metinGomuleriCoz(metin->deger(), metin->satir()));
  }

  if (const auto *mantik = dynamic_cast<const MantikNode *>(dugum)) {
    return OrhunDegeri(mantik->deger() ? 1 : 0);
  }

  if (const auto *kimlik = dynamic_cast<const KimlikNode *>(dugum)) {
    return degiskenBul(kimlik->ad(), kimlik->satir());
  }

  if (const auto *tekli = dynamic_cast<const TekliIslemNode *>(dugum)) {
    return tekliIslemHesapla(tekli);
  }

  if (const auto *ikili = dynamic_cast<const IkiliIslemNode *>(dugum)) {
    return ikiliIslemHesapla(ikili);
  }

  if (const auto *sor = dynamic_cast<const SorNode *>(dugum)) {
    return sorCalistir(sor);
  }

  if (const auto *liste = dynamic_cast<const ListeNode *>(dugum)) {
    return listeOlustur(liste);
  }

  if (const auto *uretec = dynamic_cast<const ListeUretecNode *>(dugum)) {
    return listeUreteciOlustur(uretec);
  }

  if (const auto *sozluk = dynamic_cast<const SozlukNode *>(dugum)) {
    return sozlukOlustur(sozluk);
  }

  if (const auto *dilim = dynamic_cast<const DilimErisimNode *>(dugum)) {
    return dilimErisim(dilim);
  }

  if (const auto *indeks = dynamic_cast<const IndeksErisimNode *>(dugum)) {
    return indeksErisim(indeks);
  }

  if (const auto *paralel = dynamic_cast<const ParalelYapNode *>(dugum)) {
    hataFirlat(paralel->satir(),
               "'paralel yap' su an yalnizca VM modunda destekleniyor "
               "(ipucu: 'orhun vm dosya.oh').");
  }

  if (const auto *guvenliAlan =
          dynamic_cast<const GuvenliAlanErisimNode *>(dugum)) {
    return guvenliAlanErisim(guvenliAlan);
  }

  if (const auto *alan = dynamic_cast<const AlanErisimNode *>(dugum)) {
    return alanErisim(alan);
  }

  if (const auto *benim = dynamic_cast<const BenimErisimNode *>(dugum)) {
    return benimErisim(benim);
  }

  if (const auto *yeniNesne = dynamic_cast<const YeniNesneNode *>(dugum)) {
    return yeniNesneOlustur(yeniNesne);
  }

  if (const auto *cagri = dynamic_cast<const IslevCagriNode *>(dugum)) {
    return islevCagir(cagri);
  }

  if (const auto *anonim = dynamic_cast<const IsimsizIslevNode *>(dugum)) {
    return anonimIslevOlustur(anonim);
  }

  if (const auto *dahilEt = dynamic_cast<const DahilEtNode *>(dugum)) {
    return dahilEtDegerlendir(dahilEt);
  }

  hataFirlat(dugum->satir(),
             "İfade değerlendirme sırasında geçersiz düğüm türü alındı.");
}

OrhunDegeri Interpreter::tekliIslemHesapla(const TekliIslemNode *dugum) {
  const OrhunDegeri deger = ifadeHesapla(dugum->ifade());

  if (dugum->op() == "değil") {
    return OrhunDegeri(dogruMu(deger) ? 0 : 1);
  }

  if (dugum->op() == "-") {
    if (std::holds_alternative<int>(deger.veri)) {
      return OrhunDegeri(-std::get<int>(deger.veri));
    }
    if (std::holds_alternative<double>(deger.veri)) {
      return OrhunDegeri(-std::get<double>(deger.veri));
    }
    hataFirlat(dugum->satir(), "Tekli '-' yalnızca sayılarla kullanılabilir.");
  }

  hataFirlat(dugum->satir(), "Bilinmeyen tekli operatör: " + dugum->op());
}

OrhunDegeri Interpreter::ikiliIslemHesapla(const IkiliIslemNode *dugum) {
  const std::string &op = dugum->op();

  // ve/veya için kısa devre mantığı.
  if (op == "ve") {
    const OrhunDegeri sol = ifadeHesapla(dugum->sol());
    if (!dogruMu(sol)) {
      return OrhunDegeri(0);
    }
    return OrhunDegeri(dogruMu(ifadeHesapla(dugum->sag())) ? 1 : 0);
  }

  if (op == "veya") {
    const OrhunDegeri sol = ifadeHesapla(dugum->sol());
    if (dogruMu(sol)) {
      return OrhunDegeri(1);
    }
    return OrhunDegeri(dogruMu(ifadeHesapla(dugum->sag())) ? 1 : 0);
  }

  const OrhunDegeri sol = ifadeHesapla(dugum->sol());
  const OrhunDegeri sag = ifadeHesapla(dugum->sag());

  if (op == "eşit") {
    return OrhunDegeri(esittir(sol, sag) ? 1 : 0);
  }
  if (op == "eşit_değil") {
    return OrhunDegeri(esittir(sol, sag) ? 0 : 1);
  }
  if (op == "büyük" || op == "küçük") {
    const double a = sayiDegeri(sol, dugum->satir(), "karşılaştırma");
    const double b = sayiDegeri(sag, dugum->satir(), "karşılaştırma");
    if (op == "büyük") {
      return OrhunDegeri(a > b ? 1 : 0);
    }
    return OrhunDegeri(a < b ? 1 : 0);
  }

  if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
    return listeIslemi(sol, sag, op, dugum->satir());
  }

  hataFirlat(dugum->satir(), "Bilinmeyen ikili operatör: " + op);
}

OrhunDegeri Interpreter::listeIslemi(const OrhunDegeri &sol,
                                     const OrhunDegeri &sag,
                                     const std::string &op, std::size_t satir) {
  const bool solListe =
      std::holds_alternative<OrhunDegeri::ListeTipi>(sol.veri);
  const bool sagListe =
      std::holds_alternative<OrhunDegeri::ListeTipi>(sag.veri);

  // + operatöründe en az bir taraf metinse metin birleştirme yapılır.
  if (op == "+" && (std::holds_alternative<std::string>(sol.veri) ||
                    std::holds_alternative<std::string>(sag.veri))) {
    return OrhunDegeri(metneCevir(sol) + metneCevir(sag));
  }

  const bool solSayi = sayiMi(sol);
  const bool sagSayi = sayiMi(sag);

  if (solSayi && sagSayi) {
    const double a = sayiDegeri(sol, satir, "aritmetik işlem");
    const double b = sayiDegeri(sag, satir, "aritmetik işlem");

    if (op == "+") {
      if (std::holds_alternative<int>(sol.veri) &&
          std::holds_alternative<int>(sag.veri)) {
        return OrhunDegeri(static_cast<int>(a + b));
      }
      return OrhunDegeri(a + b);
    }
    if (op == "-") {
      if (std::holds_alternative<int>(sol.veri) &&
          std::holds_alternative<int>(sag.veri)) {
        return OrhunDegeri(static_cast<int>(a - b));
      }
      return OrhunDegeri(a - b);
    }
    if (op == "*") {
      if (std::holds_alternative<int>(sol.veri) &&
          std::holds_alternative<int>(sag.veri)) {
        return OrhunDegeri(static_cast<int>(a * b));
      }
      return OrhunDegeri(a * b);
    }
    if (op == "/") {
      if (std::fabs(b) < 1e-12) {
        hataFirlat(satir, "Sıfıra bölme yapılamaz.");
      }
      return OrhunDegeri(a / b);
    }
    if (op == "%") {
      if (std::fabs(b) < 1e-12) {
        hataFirlat(satir, "Sıfıra göre mod alınamaz.");
      }
      if (std::holds_alternative<int>(sol.veri) &&
          std::holds_alternative<int>(sag.veri)) {
        return OrhunDegeri(std::get<int>(sol.veri) % std::get<int>(sag.veri));
      }
      return OrhunDegeri(std::fmod(a, b));
    }

    hataFirlat(satir, "Bilinmeyen aritmetik operatör: " + op);
  }

  if (solListe && sagListe) {
    const auto &aPtr = std::get<OrhunDegeri::ListeTipi>(sol.veri);
    const auto &bPtr = std::get<OrhunDegeri::ListeTipi>(sag.veri);
    if (!aPtr || !bPtr) {
      hataFirlat(satir, "Liste işlemi için geçersiz boş liste referansı.");
    }
    const auto &a = *aPtr;
    const auto &b = *bPtr;

    if (a.size() != b.size()) {
      hataFirlat(satir, "Matris boyutları eşleşmiyor");
    }

    OrhunDegeri::ListeVeri sonuc;
    sonuc.reserve(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
      sonuc.push_back(listeIslemi(a[i], b[i], op, satir));
    }
    return OrhunDegeri(std::move(sonuc));
  }

  if (solListe && sagSayi) {
    const auto &aPtr = std::get<OrhunDegeri::ListeTipi>(sol.veri);
    if (!aPtr) {
      hataFirlat(satir, "Liste işlemi için geçersiz boş liste referansı.");
    }
    const auto &a = *aPtr;
    OrhunDegeri::ListeVeri sonuc;
    sonuc.reserve(a.size());
    for (const auto &oge : a) {
      sonuc.push_back(listeIslemi(oge, sag, op, satir));
    }
    return OrhunDegeri(std::move(sonuc));
  }

  if (solSayi && sagListe) {
    const auto &bPtr = std::get<OrhunDegeri::ListeTipi>(sag.veri);
    if (!bPtr) {
      hataFirlat(satir, "Liste işlemi için geçersiz boş liste referansı.");
    }
    const auto &b = *bPtr;
    OrhunDegeri::ListeVeri sonuc;
    sonuc.reserve(b.size());
    for (const auto &oge : b) {
      sonuc.push_back(listeIslemi(sol, oge, op, satir));
    }
    return OrhunDegeri(std::move(sonuc));
  }

  hataFirlat(satir,
             "Tip uyuşmazlığı: '" + op + "' işlemi bu tiplerle kullanılamaz.");
}

OrhunDegeri Interpreter::sorCalistir(const SorNode *dugum) {
  const OrhunDegeri soru = ifadeHesapla(dugum->soruIfadesi());
  std::cout << metneCevir(soru);
  std::cout.flush();

  std::string giris;
  std::getline(std::cin, giris);
  if (!std::cin && giris.empty()) {
    hataFirlat(dugum->satir(), "Kullanıcı girdisi okunamadı.");
  }

  return OrhunDegeri(std::move(giris));
}

OrhunDegeri Interpreter::listeOlustur(const ListeNode *dugum) {
  OrhunDegeri::ListeVeri liste;
  liste.reserve(dugum->ogeler().size());

  for (const auto &oge : dugum->ogeler()) {
    liste.push_back(ifadeHesapla(oge.get()));
  }

  return OrhunDegeri(std::move(liste));
}

OrhunDegeri Interpreter::listeUreteciOlustur(const ListeUretecNode *dugum) {
  const OrhunDegeri kaynak = ifadeHesapla(dugum->kaynakListe());
  if (!std::holds_alternative<OrhunDegeri::ListeTipi>(kaynak.veri)) {
    hataFirlat(dugum->satir(),
               "Liste üreteci için 'içinde' kaynağı liste olmalıdır.");
  }

  const auto &kaynakListe = std::get<OrhunDegeri::ListeTipi>(kaynak.veri);
  if (!kaynakListe) {
    return OrhunDegeri(OrhunDegeri::ListeVeri{});
  }

  OrhunDegeri::ListeVeri sonuc;
  sonuc.reserve(kaynakListe->size());

  for (const auto &oge : *kaynakListe) {
    DegiskenTablosu uretecKapsami;
    uretecKapsami[dugum->donguDegiskeni()] = oge;
    yerelKapsamYigini_.push_back(
        std::make_shared<DegiskenTablosu>(std::move(uretecKapsami)));
    try {
      bool ekle = true;
      if (dugum->kosul() != nullptr) {
        ekle = dogruMu(ifadeHesapla(dugum->kosul()));
      }
      if (ekle) {
        sonuc.push_back(ifadeHesapla(dugum->ifade()));
      }
    } catch (...) {
      yerelKapsamYigini_.pop_back();
      throw;
    }
    yerelKapsamYigini_.pop_back();
  }

  return OrhunDegeri(std::move(sonuc));
}

OrhunDegeri Interpreter::sozlukOlustur(const SozlukNode *dugum) {
  OrhunDegeri::SozlukVeri sozluk;
  for (const auto &oge : dugum->ogeler()) {
    sozluk[oge.first] = ifadeHesapla(oge.second.get());
  }
  return OrhunDegeri(std::move(sozluk));
}

OrhunDegeri Interpreter::dilimErisim(const DilimErisimNode *dugum) {
  const OrhunDegeri hedef = ifadeHesapla(dugum->hedef());

  if (std::holds_alternative<OrhunDegeri::ListeTipi>(hedef.veri)) {
    const auto &listePtr = std::get<OrhunDegeri::ListeTipi>(hedef.veri);
    if (!listePtr) {
      hataFirlat(dugum->satir(),
                 "Dilim erişiminde geçersiz boş liste referansı.");
    }

    const auto &liste = *listePtr;
    const long long uzunluk = static_cast<long long>(liste.size());
    long long baslangic =
        dilimSiniriCevir(dugum->baslangic(), 0, uzunluk, dugum->satir(),
                         "liste dilim başlangıcı");
    long long bitis = dilimSiniriCevir(dugum->bitis(), uzunluk, uzunluk,
                                       dugum->satir(), "liste dilim bitişi");
    if (bitis < baslangic) {
      bitis = baslangic;
    }

    OrhunDegeri::ListeVeri sonuc;
    sonuc.reserve(static_cast<std::size_t>(bitis - baslangic));
    for (long long i = baslangic; i < bitis; ++i) {
      sonuc.push_back(liste[static_cast<std::size_t>(i)]);
    }
    return OrhunDegeri(std::move(sonuc));
  }

  if (std::holds_alternative<std::string>(hedef.veri)) {
    const std::string &metin = std::get<std::string>(hedef.veri);
    const long long uzunluk = static_cast<long long>(metin.size());
    long long baslangic =
        dilimSiniriCevir(dugum->baslangic(), 0, uzunluk, dugum->satir(),
                         "metin dilim başlangıcı");
    long long bitis = dilimSiniriCevir(dugum->bitis(), uzunluk, uzunluk,
                                       dugum->satir(), "metin dilim bitişi");
    if (bitis < baslangic) {
      bitis = baslangic;
    }

    return OrhunDegeri(
        metin.substr(static_cast<std::size_t>(baslangic),
                     static_cast<std::size_t>(bitis - baslangic)));
  }

  hataFirlat(
      dugum->satir(),
      "Dilim erişimi yalnızca liste veya metin üzerinde kullanılabilir.");
}

OrhunDegeri Interpreter::indeksErisim(const IndeksErisimNode *dugum) {
  const OrhunDegeri hedef = ifadeHesapla(dugum->hedef());
  const OrhunDegeri indeks = ifadeHesapla(dugum->indeks());

  if (std::holds_alternative<OrhunDegeri::ListeTipi>(hedef.veri)) {
    const auto &listePtr = std::get<OrhunDegeri::ListeTipi>(hedef.veri);
    if (!listePtr) {
      hataFirlat(dugum->satir(),
                 "Liste erişiminde geçersiz boş liste referansı.");
    }
    const auto &liste = *listePtr;
    const std::size_t idx =
        listeIndeksiCevir(indeks, dugum->satir(), "liste indeksi");
    if (idx >= liste.size()) {
      hataFirlat(dugum->satir(), "Liste indeksi sınır dışında.");
    }
    return liste[idx];
  }

  if (std::holds_alternative<OrhunDegeri::SozlukTipi>(hedef.veri)) {
    if (!std::holds_alternative<std::string>(indeks.veri)) {
      hataFirlat(dugum->satir(), "Sözlük anahtarı metin olmalıdır.");
    }

    const std::string &anahtar = std::get<std::string>(indeks.veri);
    const auto &sozlukPtr = std::get<OrhunDegeri::SozlukTipi>(hedef.veri);
    if (!sozlukPtr) {
      hataFirlat(dugum->satir(),
                 "Sözlük erişiminde geçersiz boş sözlük referansı.");
    }
    const auto &sozluk = *sozlukPtr;
    const auto bulunan = sozluk.find(anahtar);
    if (bulunan == sozluk.end()) {
      std::vector<std::string> adaylar;
      adaylar.reserve(sozluk.size());
      for (const auto &[anahtarAday, _] : sozluk) {
        adaylar.push_back(anahtarAday);
      }
      hataFirlat(dugum->satir(),
                 oneriliMesaj("'" + anahtar + "' anahtarı sözlükte bulunamadı!",
                              anahtar, adaylar));
    }
    return bulunan->second;
  }

  if (std::holds_alternative<OrhunDegeri::NesneTipi>(hedef.veri)) {
    if (!std::holds_alternative<std::string>(indeks.veri)) {
      hataFirlat(dugum->satir(),
                 "Nesne alan erişiminde anahtar metin olmalıdır.");
    }
    const auto &nesne = std::get<OrhunDegeri::NesneTipi>(hedef.veri);
    if (!nesne || !nesne->alanlar) {
      hataFirlat(dugum->satir(), "Nesne erişiminde geçersiz nesne.");
    }
    const std::string &anahtar = std::get<std::string>(indeks.veri);
    const auto bulunan = nesne->alanlar->find(anahtar);
    if (bulunan == nesne->alanlar->end()) {
      std::vector<std::string> adaylar;
      std::unordered_set<std::string> gorulen;
      for (const auto &[alan, _] : *nesne->alanlar) {
        adayEkleTekil(adaylar, gorulen, alan);
      }
      for (const auto &[metod, _] : nesne->metodlar) {
        adayEkleTekil(adaylar, gorulen, metod);
      }
      hataFirlat(dugum->satir(),
                 oneriliMesaj("'" + anahtar + "' alanı nesnede bulunamadı.",
                              anahtar, adaylar));
    }
    return bulunan->second;
  }

  hataFirlat(dugum->satir(), "İndeks erişimi yalnızca liste, sözlük veya nesne "
                             "üzerinde kullanılabilir.");
}

OrhunDegeri Interpreter::guvenliAlanErisim(
    const GuvenliAlanErisimNode *dugum) {
  const OrhunDegeri hedef = ifadeHesapla(dugum->hedef());

  // Interpreter'da bos deger 0 ile temsil ediliyor; bu nedenle 0'u bos kabul
  // edip guvenli erisimde dogrudan bos donuyoruz.
  if (std::holds_alternative<int>(hedef.veri) && std::get<int>(hedef.veri) == 0) {
    return OrhunDegeri(0);
  }

  if (std::holds_alternative<OrhunDegeri::SozlukTipi>(hedef.veri)) {
    const auto &sozlukPtr = std::get<OrhunDegeri::SozlukTipi>(hedef.veri);
    if (!sozlukPtr) {
      return OrhunDegeri(0);
    }
    const auto bulunan = sozlukPtr->find(dugum->alanAdi());
    if (bulunan == sozlukPtr->end()) {
      std::vector<std::string> adaylar;
      adaylar.reserve(sozlukPtr->size());
      for (const auto &[anahtar, _] : *sozlukPtr) {
        adaylar.push_back(anahtar);
      }
      hataFirlat(dugum->satir(),
                 oneriliMesaj("'" + dugum->alanAdi() +
                                  "' anahtarı sözlükte bulunamadı!",
                              dugum->alanAdi(), adaylar));
    }
    return bulunan->second;
  }

  if (std::holds_alternative<OrhunDegeri::NesneTipi>(hedef.veri)) {
    const auto &nesne = std::get<OrhunDegeri::NesneTipi>(hedef.veri);
    if (!nesne || !nesne->alanlar) {
      return OrhunDegeri(0);
    }
    const auto alanBul = nesne->alanlar->find(dugum->alanAdi());
    if (alanBul != nesne->alanlar->end()) {
      return alanBul->second;
    }

    if (nesne->metodlar.find(dugum->alanAdi()) != nesne->metodlar.end()) {
      return OrhunDegeri("__islev_ref__:" + nesne->sinifAdi + "." +
                         dugum->alanAdi());
    }

    std::vector<std::string> adaylar;
    std::unordered_set<std::string> gorulen;
    for (const auto &[alan, _] : *nesne->alanlar) {
      adayEkleTekil(adaylar, gorulen, alan);
    }
    for (const auto &[metod, _] : nesne->metodlar) {
      adayEkleTekil(adaylar, gorulen, metod);
    }
    hataFirlat(dugum->satir(), oneriliMesaj("'" + dugum->alanAdi() +
                                                "' alanı nesnede bulunamadı!",
                                            dugum->alanAdi(), adaylar));
  }

  hataFirlat(
      dugum->satir(),
      "Nokta erişimi yalnızca sözlük veya nesne üzerinde kullanılabilir.");
}

OrhunDegeri Interpreter::alanErisim(const AlanErisimNode *dugum) {
  const OrhunDegeri hedef = ifadeHesapla(dugum->hedef());

  if (std::holds_alternative<OrhunDegeri::SozlukTipi>(hedef.veri)) {
    const auto &sozlukPtr = std::get<OrhunDegeri::SozlukTipi>(hedef.veri);
    if (!sozlukPtr) {
      hataFirlat(dugum->satir(),
                 "Nokta erişiminde geçersiz boş sözlük referansı.");
    }
    const auto bulunan = sozlukPtr->find(dugum->alanAdi());
    if (bulunan == sozlukPtr->end()) {
      std::vector<std::string> adaylar;
      adaylar.reserve(sozlukPtr->size());
      for (const auto &[anahtar, _] : *sozlukPtr) {
        adaylar.push_back(anahtar);
      }
      hataFirlat(dugum->satir(),
                 oneriliMesaj("'" + dugum->alanAdi() +
                                  "' anahtarı sözlükte bulunamadı!",
                              dugum->alanAdi(), adaylar));
    }
    return bulunan->second;
  }

  if (std::holds_alternative<OrhunDegeri::NesneTipi>(hedef.veri)) {
    const auto &nesne = std::get<OrhunDegeri::NesneTipi>(hedef.veri);
    if (!nesne || !nesne->alanlar) {
      hataFirlat(dugum->satir(), "Nokta erişiminde geçersiz nesne.");
    }
    const auto alanBul = nesne->alanlar->find(dugum->alanAdi());
    if (alanBul != nesne->alanlar->end()) {
      return alanBul->second;
    }

    if (nesne->metodlar.find(dugum->alanAdi()) != nesne->metodlar.end()) {
      return OrhunDegeri("__islev_ref__:" + nesne->sinifAdi + "." +
                         dugum->alanAdi());
    }

    std::vector<std::string> adaylar;
    std::unordered_set<std::string> gorulen;
    for (const auto &[alan, _] : *nesne->alanlar) {
      adayEkleTekil(adaylar, gorulen, alan);
    }
    for (const auto &[metod, _] : nesne->metodlar) {
      adayEkleTekil(adaylar, gorulen, metod);
    }
    hataFirlat(dugum->satir(), oneriliMesaj("'" + dugum->alanAdi() +
                                                "' alanı nesnede bulunamadı!",
                                            dugum->alanAdi(), adaylar));
  }

  hataFirlat(
      dugum->satir(),
      "Nokta erişimi yalnızca sözlük veya nesne üzerinde kullanılabilir.");
}

OrhunDegeri Interpreter::benimErisim(const BenimErisimNode *dugum) {
  const OrhunDegeri &benimDegeri = degiskenBul("benim", dugum->satir());
  if (!std::holds_alternative<OrhunDegeri::NesneTipi>(benimDegeri.veri)) {
    hataFirlat(dugum->satir(),
               "'benim' yalnızca nesne metodu içinde kullanılabilir.");
  }

  const auto &nesne = std::get<OrhunDegeri::NesneTipi>(benimDegeri.veri);
  if (!nesne || !nesne->alanlar) {
    hataFirlat(dugum->satir(), "'benim' geçersiz nesneyi gösteriyor.");
  }

  const auto alan = nesne->alanlar->find(dugum->alanAdi());
  if (alan == nesne->alanlar->end()) {
    std::vector<std::string> adaylar;
    adaylar.reserve(nesne->alanlar->size());
    for (const auto &[alanAday, _] : *nesne->alanlar) {
      adaylar.push_back(alanAday);
    }
    hataFirlat(dugum->satir(), oneriliMesaj("'" + dugum->alanAdi() +
                                                "' alanı nesnede bulunamadı.",
                                            dugum->alanAdi(), adaylar));
  }
  return alan->second;
}

OrhunDegeri Interpreter::yeniNesneOlustur(const YeniNesneNode *dugum) {
  const auto sinifBul = sinifTablosu_.find(dugum->sinifAdi());
  if (sinifBul == sinifTablosu_.end()) {
    std::vector<std::string> adaylar;
    std::unordered_set<std::string> gorulen;
    for (const auto &[ad, _] : sinifTablosu_) {
      adayEkleTekil(adaylar, gorulen, ad);
    }
    hataFirlat(dugum->satir(), oneriliMesaj("'" + dugum->sinifAdi() +
                                                "' adlı sınıf bulunamadı.",
                                            dugum->sinifAdi(), adaylar));
  }

  const SinifTanimNode *sinif = sinifBul->second;

  auto nesne = std::make_shared<OrhunNesne>();
  nesne->sinifAdi = sinif->ad();
  OrhunDegeri nesneDegeri(nesne);

  // Miras dahil, alan ve metodları ebeveynden çocuğa doğru yükle.
  std::unordered_set<std::string> ziyaretEdilenler;
  const auto sinifUyeleriniYukle =
      [&](const auto &self, const SinifTanimNode *aktifSinif) -> void {
    if (aktifSinif == nullptr) {
      return;
    }

    if (!ziyaretEdilenler.insert(aktifSinif->ad()).second) {
      hataFirlat(dugum->satir(), "Sınıf mirasında döngü tespit edildi: '" +
                                     aktifSinif->ad() + "'");
    }

    if (!aktifSinif->ebeveynAdi().empty()) {
      const auto ebeveynBul = sinifTablosu_.find(aktifSinif->ebeveynAdi());
      if (ebeveynBul == sinifTablosu_.end()) {
        hataFirlat(dugum->satir(), "'" + aktifSinif->ebeveynAdi() +
                                       "' adlı ebeveyn sınıf bulunamadı.");
      }
      self(self, ebeveynBul->second);
    }

    for (const auto &komut : aktifSinif->govde()->komutlar()) {
      if (const auto *atama = dynamic_cast<const AtamaNode *>(komut.get())) {
        const auto *kimlik = dynamic_cast<const KimlikNode *>(atama->hedef());
        if (kimlik == nullptr) {
          continue;
        }
        // Çocuğun alanı aynı ada sahipse ebeveyni override eder.
        nesne->alanlar->insert_or_assign(kimlik->ad(),
                                         ifadeHesapla(atama->ifade()));
        continue;
      }

      if (const auto *metod =
              dynamic_cast<const IslevTanimNode *>(komut.get())) {
        // Çocuğun metodu aynı ada sahipse ebeveyni override eder.
        nesne->metodlar[metod->ad()] =
            NesneMetodBilgisi{metod, aktifSinif->ad()};
        continue;
      }
    }
  };
  sinifUyeleriniYukle(sinifUyeleriniYukle, sinif);

  // Kurucu varsa otomatik çağır.
  const auto kur = nesne->metodlar.find("kur");
  if (kur != nesne->metodlar.end()) {
    std::vector<OrhunDegeri> argumanlarDeger;
    argumanlarDeger.reserve(dugum->argumanlar().size());
    for (const auto &arg : dugum->argumanlar()) {
      argumanlarDeger.push_back(ifadeHesapla(arg.get()));
    }

    static_cast<void>(kullaniciIslevCalistir(
        kur->second.dugum, argumanlarDeger, dugum->satir(), &nesneDegeri,
        &kur->second.tanimlayanSinif, false));
  } else if (!dugum->argumanlar().empty()) {
    hataFirlat(dugum->satir(),
               "'" + dugum->sinifAdi() +
                   "' sınıfında 'kur' metodu yok; kurucu argümanı verilemez.");
  }

  return nesneDegeri;
}

OrhunDegeri Interpreter::anonimIslevOlustur(const IsimsizIslevNode *dugum) {
  const std::string ad = "__anonim__" + std::to_string(++anonimIslevSayaci_);
  closureTablosu_[ad] =
      ClosureKaydi{nullptr, dugum, yerelKapsamYigini_, "<anonim>"};
  return OrhunDegeri("__islev_ref__:" + ad);
}

OrhunDegeri Interpreter::islevCagir(const IslevCagriNode *dugum,
                                    bool dondurZorunlu) {
  std::vector<OrhunDegeri> argumanDegerleri;
  argumanDegerleri.reserve(dugum->argumanlar().size());
  for (const auto &arg : dugum->argumanlar()) {
    argumanDegerleri.push_back(ifadeHesapla(arg.get()));
  }

  const std::string &cagriAdi = dugum->ad();
  if (cagriAdi.rfind("ust.", 0) == 0) {
    return ustIslevCagir(cagriAdi.substr(4), argumanDegerleri, dugum->satir());
  }

  if (islevTablosu_.find(cagriAdi) != islevTablosu_.end() ||
      anonimIslevTablosu_.find(cagriAdi) != anonimIslevTablosu_.end() ||
      gomuluIslevler_.find(cagriAdi) != gomuluIslevler_.end()) {
    return islevCagirAdaGore(cagriAdi, argumanDegerleri, dugum->satir(),
                             dondurZorunlu);
  }

  bool isimliDegiskenVar = false;
  std::string degiskenIslevAdi;
  try {
    const OrhunDegeri &aday = degiskenBul(cagriAdi, dugum->satir());
    isimliDegiskenVar = true;
    static_cast<void>(islevReferansiCoz(aday, degiskenIslevAdi));
  } catch (const std::exception &) {
    // Degisken yoksa normal çözümleme akışıyla devam et.
  }
  if (!degiskenIslevAdi.empty()) {
    return islevCagirAdaGore(degiskenIslevAdi, argumanDegerleri,
                             dugum->satir(), dondurZorunlu);
  }

  const std::size_t nokta = cagriAdi.rfind('.');
  if (nokta == std::string::npos || nokta == 0 ||
      nokta + 1 >= cagriAdi.size()) {
    if (isimliDegiskenVar) {
      hataFirlat(dugum->satir(),
                 "'" + cagriAdi + "' çağrılabilir bir işlev değil.");
    }
    std::vector<std::string> adaylar;
    std::unordered_set<std::string> gorulen;
    for (const auto &[ad, _] : islevTablosu_) {
      adayEkleTekil(adaylar, gorulen, ad);
    }
    for (const auto &[ad, _] : anonimIslevTablosu_) {
      adayEkleTekil(adaylar, gorulen, ad);
    }
    for (const auto &[ad, _] : gomuluIslevler_) {
      adayEkleTekil(adaylar, gorulen, ad);
    }
    hataFirlat(dugum->satir(),
               oneriliMesaj("'" + cagriAdi + "' adlı işlev bulunamadı.",
                            cagriAdi, adaylar));
  }

  const std::string hedefYolu = cagriAdi.substr(0, nokta);
  const std::string metodAdi = cagriAdi.substr(nokta + 1);
  const OrhunDegeri hedef = noktaYoluDegeri(hedefYolu, dugum->satir());
  return nesneMetoduCagir(hedef, metodAdi, argumanDegerleri, dugum->satir());
}

OrhunDegeri
Interpreter::ustIslevCagir(const std::string &metodAdi,
                           const std::vector<OrhunDegeri> &argumanlar,
                           std::size_t satir) {
  const OrhunDegeri &benimDegeri = degiskenBul("benim", satir);
  if (!std::holds_alternative<OrhunDegeri::NesneTipi>(benimDegeri.veri)) {
    hataFirlat(satir, "'ust' yalnızca nesne metodu içinde kullanılabilir.");
  }

  const OrhunDegeri &sinifDegeri = degiskenBul("__sinif__", satir);
  if (!std::holds_alternative<std::string>(sinifDegeri.veri)) {
    hataFirlat(satir, "'ust' bağlamı çözümlenemedi (sınıf bilgisi eksik).");
  }
  const std::string aktifSinif = std::get<std::string>(sinifDegeri.veri);

  const auto sinifBul = sinifTablosu_.find(aktifSinif);
  if (sinifBul == sinifTablosu_.end()) {
    std::vector<std::string> adaylar;
    adaylar.reserve(sinifTablosu_.size());
    for (const auto &[sinifAdi, _] : sinifTablosu_) {
      adaylar.push_back(sinifAdi);
    }
    hataFirlat(satir, oneriliMesaj("'" + aktifSinif + "' sınıfı kayıtlı değil.",
                                   aktifSinif, adaylar));
  }

  std::string ebeveynAdi = sinifBul->second->ebeveynAdi();
  if (ebeveynAdi.empty()) {
    hataFirlat(satir, "'" + aktifSinif + "' sınıfının üst sınıfı yok.");
  }

  std::vector<std::string> ebeveynMetodAdaylari;
  std::unordered_set<std::string> gorulenMetodlar;
  while (!ebeveynAdi.empty()) {
    const auto ebeveynBul = sinifTablosu_.find(ebeveynAdi);
    if (ebeveynBul == sinifTablosu_.end()) {
      std::vector<std::string> adaylar;
      adaylar.reserve(sinifTablosu_.size());
      for (const auto &[sinifAdi, _] : sinifTablosu_) {
        adaylar.push_back(sinifAdi);
      }
      hataFirlat(satir,
                 oneriliMesaj("'" + ebeveynAdi + "' adlı üst sınıf bulunamadı.",
                              ebeveynAdi, adaylar));
    }

    const SinifTanimNode *ebeveyn = ebeveynBul->second;
    const IslevTanimNode *bulunanMetod = nullptr;
    for (const auto &komut : ebeveyn->govde()->komutlar()) {
      const auto *metod = dynamic_cast<const IslevTanimNode *>(komut.get());
      if (metod != nullptr && metod->ad() == metodAdi) {
        bulunanMetod = metod;
        break;
      }
      if (metod != nullptr) {
        adayEkleTekil(ebeveynMetodAdaylari, gorulenMetodlar, metod->ad());
      }
    }

    if (bulunanMetod != nullptr) {
      return kullaniciIslevCalistir(bulunanMetod, argumanlar, satir,
                                    &benimDegeri, &ebeveynAdi, false);
    }

    ebeveynAdi = ebeveyn->ebeveynAdi();
  }

  hataFirlat(satir, oneriliMesaj("'" + metodAdi +
                                     "' metodu üst sınıflarda bulunamadı.",
                                 metodAdi, ebeveynMetodAdaylari));
}

OrhunDegeri
Interpreter::islevCagirAdaGore(const std::string &ad,
                               const std::vector<OrhunDegeri> &argumanlar,
                               std::size_t satir, bool dondurZorunlu) {
  const auto closure = closureTablosu_.find(ad);
  if (closure != closureTablosu_.end()) {
    const ClosureKaydi &kayit = closure->second;
    if (kayit.islev != nullptr) {
      return kullaniciIslevCalistir(kayit.islev, argumanlar, satir, nullptr,
                                    nullptr, dondurZorunlu,
                                    &kayit.yakalananKapsamlar);
    }
    if (kayit.anonimIslev != nullptr) {
      return anonimIslevCalistir(kayit.anonimIslev, argumanlar, satir,
                                 nullptr, nullptr, dondurZorunlu,
                                 &kayit.yakalananKapsamlar);
    }
  }

  const auto yerelIslev = islevTablosu_.find(ad);
  if (yerelIslev != islevTablosu_.end()) {
    return kullaniciIslevCalistir(yerelIslev->second, argumanlar, satir,
                                  nullptr, nullptr, dondurZorunlu);
  }

  const auto anonimIslev = anonimIslevTablosu_.find(ad);
  if (anonimIslev != anonimIslevTablosu_.end()) {
    return anonimIslevCalistir(anonimIslev->second, argumanlar, satir, nullptr,
                               nullptr, dondurZorunlu);
  }

  const auto gomulu = gomuluIslevler_.find(ad);
  if (gomulu != gomuluIslevler_.end()) {
    return gomulu->second(argumanlar, satir);
  }

  std::vector<std::string> adaylar;
  std::unordered_set<std::string> gorulen;
  for (const auto &[isim, _] : islevTablosu_) {
    adayEkleTekil(adaylar, gorulen, isim);
  }
  for (const auto &[isim, _] : anonimIslevTablosu_) {
    adayEkleTekil(adaylar, gorulen, isim);
  }
  for (const auto &[isim, _] : gomuluIslevler_) {
    adayEkleTekil(adaylar, gorulen, isim);
  }
  hataFirlat(satir,
             oneriliMesaj("'" + ad + "' adlı işlev bulunamadı.", ad, adaylar));
}

OrhunDegeri Interpreter::kullaniciIslevCalistir(
    const IslevTanimNode *islev, const std::vector<OrhunDegeri> &argumanlar,
    std::size_t satir, const OrhunDegeri *benimDegeri,
    const std::string *etkinSinifAdi, bool dondurZorunlu,
    const std::vector<KapsamPtr> *yakalananKapsamlar) {
  cagriYigini_.push_back({islev->ad(), satir});

  const std::size_t minArg = islev->minArgumanSayisi();
  const std::size_t maxArg = islev->parametreler().size();
  if (argumanlar.size() < minArg || argumanlar.size() > maxArg) {
    try {
      hataFirlat(satir, "'" + islev->ad() +
                            "' için argüman sayısı uyuşmuyor (beklenen " +
                            std::to_string(minArg) + "-" +
                            std::to_string(maxArg) + ").");
    } catch (...) {
      cagriYigini_.pop_back();
      throw;
    }
  }

  const std::size_t kapsamBaslangici = yerelKapsamYigini_.size();
  if (yakalananKapsamlar != nullptr) {
    yerelKapsamYigini_.insert(yerelKapsamYigini_.end(),
                              yakalananKapsamlar->begin(),
                              yakalananKapsamlar->end());
  }

  DegiskenTablosu yeniKapsam;
  if (benimDegeri != nullptr) {
    yeniKapsam["benim"] = *benimDegeri;
  }
  if (etkinSinifAdi != nullptr) {
    yeniKapsam["__sinif__"] = OrhunDegeri(*etkinSinifAdi);
  }

  yerelKapsamYigini_.push_back(
      std::make_shared<DegiskenTablosu>(std::move(yeniKapsam)));
  try {
    auto &kapsam = *yerelKapsamYigini_.back();
    for (std::size_t i = 0; i < islev->parametreler().size(); ++i) {
      if (i < argumanlar.size()) {
        kapsam[islev->parametreler()[i]] = argumanlar[i];
        continue;
      }
      const auto &varsayilanlar = islev->varsayilanlar();
      if (i >= varsayilanlar.size() || varsayilanlar[i] == nullptr) {
        hataFirlat(satir, "İç hata: eksik varsayılan parametre.");
      }
      kapsam[islev->parametreler()[i]] =
          ifadeHesapla(varsayilanlar[i].get());
    }

    calistirBlock(islev->govde());
  } catch (const DondurSinyali &sinyal) {
    yerelKapsamYigini_.resize(kapsamBaslangici);
    cagriYigini_.pop_back();
    return sinyal.deger;
  } catch (...) {
    yerelKapsamYigini_.resize(kapsamBaslangici);
    cagriYigini_.pop_back();
    throw;
  }

  yerelKapsamYigini_.resize(kapsamBaslangici);
  if (dondurZorunlu) {
    try {
      hataFirlat(satir, "'" + islev->ad() + "' işlevi bir değer döndürmedi.");
    } catch (...) {
      cagriYigini_.pop_back();
      throw;
    }
  }
  cagriYigini_.pop_back();
  return OrhunDegeri(0);
}

OrhunDegeri Interpreter::anonimIslevCalistir(
    const IsimsizIslevNode *islev, const std::vector<OrhunDegeri> &argumanlar,
    std::size_t satir, const OrhunDegeri *benimDegeri,
    const std::string *etkinSinifAdi, bool dondurZorunlu,
    const std::vector<KapsamPtr> *yakalananKapsamlar) {
  cagriYigini_.push_back({"<anonim>", satir});

  const std::size_t minArg = islev->minArgumanSayisi();
  const std::size_t maxArg = islev->parametreler().size();
  if (argumanlar.size() < minArg || argumanlar.size() > maxArg) {
    try {
      hataFirlat(satir,
                 "Anonim işlev için argüman sayısı uyuşmuyor (beklenen " +
                     std::to_string(minArg) + "-" +
                     std::to_string(maxArg) + ").");
    } catch (...) {
      cagriYigini_.pop_back();
      throw;
    }
  }

  const std::size_t kapsamBaslangici = yerelKapsamYigini_.size();
  if (yakalananKapsamlar != nullptr) {
    yerelKapsamYigini_.insert(yerelKapsamYigini_.end(),
                              yakalananKapsamlar->begin(),
                              yakalananKapsamlar->end());
  }

  DegiskenTablosu yeniKapsam;
  if (benimDegeri != nullptr) {
    yeniKapsam["benim"] = *benimDegeri;
  }
  if (etkinSinifAdi != nullptr) {
    yeniKapsam["__sinif__"] = OrhunDegeri(*etkinSinifAdi);
  }

  yerelKapsamYigini_.push_back(
      std::make_shared<DegiskenTablosu>(std::move(yeniKapsam)));
  try {
    auto &kapsam = *yerelKapsamYigini_.back();
    for (std::size_t i = 0; i < islev->parametreler().size(); ++i) {
      if (i < argumanlar.size()) {
        kapsam[islev->parametreler()[i]] = argumanlar[i];
        continue;
      }
      const auto &varsayilanlar = islev->varsayilanlar();
      if (i >= varsayilanlar.size() || varsayilanlar[i] == nullptr) {
        hataFirlat(satir, "İç hata: eksik varsayılan anonim parametre.");
      }
      kapsam[islev->parametreler()[i]] =
          ifadeHesapla(varsayilanlar[i].get());
    }

    calistirBlock(islev->govde());
  } catch (const DondurSinyali &sinyal) {
    yerelKapsamYigini_.resize(kapsamBaslangici);
    cagriYigini_.pop_back();
    return sinyal.deger;
  } catch (...) {
    yerelKapsamYigini_.resize(kapsamBaslangici);
    cagriYigini_.pop_back();
    throw;
  }

  yerelKapsamYigini_.resize(kapsamBaslangici);
  if (dondurZorunlu) {
    try {
      hataFirlat(satir, "Anonim işlev bir değer döndürmedi.");
    } catch (...) {
      cagriYigini_.pop_back();
      throw;
    }
  }
  cagriYigini_.pop_back();
  return OrhunDegeri(0);
}

OrhunDegeri Interpreter::noktaYoluDegeri(const std::string &yol,
                                         std::size_t satir) const {
  const std::vector<std::string> parcalar = noktaIleBol(yol);
  if (parcalar.empty() || parcalar.front().empty()) {
    throw OrhunHatasi("Satır " + std::to_string(satir) +
                      ": Geçersiz nokta erişim yolu: '" + yol + "'");
  }

  const OrhunDegeri *aktif = &degiskenBul(parcalar.front(), satir);
  for (std::size_t i = 1; i < parcalar.size(); ++i) {
    if (std::holds_alternative<OrhunDegeri::SozlukTipi>(aktif->veri)) {
      const auto &sozlukPtr = std::get<OrhunDegeri::SozlukTipi>(aktif->veri);
      if (!sozlukPtr) {
        throw OrhunHatasi("Satır " + std::to_string(satir) +
                          ": Boş sözlük üzerinden nokta erişimi yapılamaz.");
      }

      const auto bulunan = sozlukPtr->find(parcalar[i]);
      if (bulunan == sozlukPtr->end()) {
        std::vector<std::string> adaylar;
        adaylar.reserve(sozlukPtr->size());
        for (const auto &[anahtar, _] : *sozlukPtr) {
          adaylar.push_back(anahtar);
        }
        throw OrhunHatasi(
            "Satır " + std::to_string(satir) + ": " +
            oneriliMesaj("'" + parcalar[i] + "' anahtarı bulunamadı.",
                         parcalar[i], adaylar));
      }
      aktif = &bulunan->second;
      continue;
    }

    if (std::holds_alternative<OrhunDegeri::NesneTipi>(aktif->veri)) {
      const auto &nesne = std::get<OrhunDegeri::NesneTipi>(aktif->veri);
      if (!nesne || !nesne->alanlar) {
        throw OrhunHatasi(
            "Satır " + std::to_string(satir) +
            ": Geçersiz nesne üzerinden nokta erişimi yapılamaz.");
      }
      const auto alan = nesne->alanlar->find(parcalar[i]);
      if (alan == nesne->alanlar->end()) {
        std::vector<std::string> adaylar;
        std::unordered_set<std::string> gorulen;
        for (const auto &[alanAday, _] : *nesne->alanlar) {
          adayEkleTekil(adaylar, gorulen, alanAday);
        }
        for (const auto &[metodAday, _] : nesne->metodlar) {
          adayEkleTekil(adaylar, gorulen, metodAday);
        }
        throw OrhunHatasi(
            "Satır " + std::to_string(satir) + ": " +
            oneriliMesaj("'" + parcalar[i] + "' alanı bulunamadı.", parcalar[i],
                         adaylar));
      }
      aktif = &alan->second;
      continue;
    }

    throw OrhunHatasi("Satır " + std::to_string(satir) + ": '" +
                      parcalar[i - 1] + "' üzerinde nokta erişimi yapılamaz.");
  }

  return *aktif;
}

bool Interpreter::islevReferansiCoz(const OrhunDegeri &deger,
                                    std::string &gercekAd) const {
  if (!std::holds_alternative<std::string>(deger.veri)) {
    return false;
  }
  const std::string &metin = std::get<std::string>(deger.veri);
  const std::string onEk = "__islev_ref__:";
  if (metin.rfind(onEk, 0) != 0) {
    return false;
  }
  gercekAd = metin.substr(onEk.size());
  return !gercekAd.empty();
}

OrhunDegeri Interpreter::nesneMetoduCagir(
    const OrhunDegeri &hedef, const std::string &metodAdi,
    const std::vector<OrhunDegeri> &argumanlar, std::size_t satir) {
  // Liste metodları: ekle, sil, uzunluk
  if (std::holds_alternative<OrhunDegeri::ListeTipi>(hedef.veri)) {
    const auto &listePtr = std::get<OrhunDegeri::ListeTipi>(hedef.veri);
    if (!listePtr) {
      hataFirlat(satir, "Liste metodu boş liste üzerinde çağrılamaz.");
    }

    if (metodAdi == "ekle") {
      if (argumanlar.size() != 1) {
        hataFirlat(satir, "liste.ekle(eleman) bir argüman alır.");
      }
      listePtr->push_back(argumanlar[0]);
      return hedef;
    }

    if (metodAdi == "sil") {
      if (argumanlar.size() != 1) {
        hataFirlat(satir, "liste.sil(indeks) bir argüman alır.");
      }
      const std::size_t idx =
          listeIndeksiCevir(argumanlar[0], satir, "liste.sil indeksi");
      if (idx >= listePtr->size()) {
        hataFirlat(satir, "liste.sil indeksi sınır dışında.");
      }
      listePtr->erase(listePtr->begin() + static_cast<std::ptrdiff_t>(idx));
      return hedef;
    }

    if (metodAdi == "uzunluk") {
      if (!argumanlar.empty()) {
        hataFirlat(satir, "liste.uzunluk() argüman almaz.");
      }
      return OrhunDegeri(static_cast<int>(listePtr->size()));
    }

    hataFirlat(satir,
               oneriliMesaj("Liste için bilinmeyen metod: '" + metodAdi + "'",
                            metodAdi, sabitListeMetodAdaylari()));
  }

  // Sözlük metodları: anahtarlar, degerler, sil (+ çağrılabilir alanlar)
  if (std::holds_alternative<OrhunDegeri::SozlukTipi>(hedef.veri)) {
    const auto &sozlukPtr = std::get<OrhunDegeri::SozlukTipi>(hedef.veri);
    if (!sozlukPtr) {
      hataFirlat(satir, "Sözlük metodu boş sözlük üzerinde çağrılamaz.");
    }

    const auto uye = sozlukPtr->find(metodAdi);
    if (uye != sozlukPtr->end()) {
      std::string gercekAd;
      if (islevReferansiCoz(uye->second, gercekAd)) {
        // Modül işlevleri modül içi sabitlere erişebilsin diye sözlüğü scope'a
        // koy.
        DegiskenTablosu modulKapsami;
        for (const auto &[anahtar, deger] : *sozlukPtr) {
          std::string dummy;
          if (!islevReferansiCoz(deger, dummy)) {
            modulKapsami[anahtar] = deger;
          }
        }

        yerelKapsamYigini_.push_back(
            std::make_shared<DegiskenTablosu>(std::move(modulKapsami)));
        try {
          OrhunDegeri sonuc = islevCagirAdaGore(gercekAd, argumanlar, satir);
          yerelKapsamYigini_.pop_back();
          return sonuc;
        } catch (...) {
          yerelKapsamYigini_.pop_back();
          throw;
        }
      }
    }

    if (metodAdi == "anahtarlar") {
      if (!argumanlar.empty()) {
        hataFirlat(satir, "sozluk.anahtarlar() argüman almaz.");
      }
      OrhunDegeri::ListeVeri sonuc;
      sonuc.reserve(sozlukPtr->size());
      for (const auto &kv : *sozlukPtr) {
        sonuc.emplace_back(kv.first);
      }
      return OrhunDegeri(std::move(sonuc));
    }

    if (metodAdi == "degerler") {
      if (!argumanlar.empty()) {
        hataFirlat(satir, "sozluk.degerler() argüman almaz.");
      }
      OrhunDegeri::ListeVeri sonuc;
      sonuc.reserve(sozlukPtr->size());
      for (const auto &kv : *sozlukPtr) {
        sonuc.push_back(kv.second);
      }
      return OrhunDegeri(std::move(sonuc));
    }

    if (metodAdi == "sil") {
      if (argumanlar.size() != 1 ||
          !std::holds_alternative<std::string>(argumanlar[0].veri)) {
        hataFirlat(satir, "sozluk.sil(anahtar) için metin anahtar bekleniyor.");
      }
      const std::string &anahtar = std::get<std::string>(argumanlar[0].veri);
      const auto silinen = sozlukPtr->erase(anahtar);
      if (silinen == 0) {
        hataFirlat(satir, "'" + anahtar + "' anahtarı sözlükte bulunamadı!");
      }
      return hedef;
    }

    if (metodAdi == "uzunluk") {
      if (!argumanlar.empty()) {
        hataFirlat(satir, "sozluk.uzunluk() argüman almaz.");
      }
      return OrhunDegeri(static_cast<int>(sozlukPtr->size()));
    }

    std::vector<std::string> adaylar = sabitSozlukMetodAdaylari();
    for (const auto &[anahtar, deger] : *sozlukPtr) {
      std::string gercekAd;
      if (islevReferansiCoz(deger, gercekAd) ||
          std::holds_alternative<std::string>(deger.veri)) {
        adaylar.push_back(anahtar);
      }
    }
    hataFirlat(satir,
               oneriliMesaj("Sözlük için bilinmeyen metod: '" + metodAdi + "'",
                            metodAdi, adaylar));
  }

  // Metin metodları: buyuk, kucuk, parcala, uzunluk
  if (std::holds_alternative<OrhunDegeri::NesneTipi>(hedef.veri)) {
    const auto &nesne = std::get<OrhunDegeri::NesneTipi>(hedef.veri);
    if (!nesne) {
      hataFirlat(satir, "Geçersiz nesne üzerinde metod çağrısı yapılamaz.");
    }

    const auto metod = nesne->metodlar.find(metodAdi);
    if (metod == nesne->metodlar.end()) {
      std::vector<std::string> adaylar;
      adaylar.reserve(nesne->metodlar.size());
      for (const auto &[metodAday, _] : nesne->metodlar) {
        adaylar.push_back(metodAday);
      }
      hataFirlat(satir, oneriliMesaj("'" + nesne->sinifAdi + "' nesnesinde '" +
                                         metodAdi + "' adlı metod bulunamadı.",
                                     metodAdi, adaylar));
    }

    return kullaniciIslevCalistir(metod->second.dugum, argumanlar, satir,
                                  &hedef, &metod->second.tanimlayanSinif,
                                  false);
  }

  // Metin metodları: buyuk, kucuk, parcala, uzunluk
  if (std::holds_alternative<std::string>(hedef.veri)) {
    const std::string metin = std::get<std::string>(hedef.veri);

    if (metodAdi == "buyuk") {
      if (!argumanlar.empty()) {
        hataFirlat(satir, "metin.buyuk() argüman almaz.");
      }
      std::string sonuc = metin;
      std::transform(
          sonuc.begin(), sonuc.end(), sonuc.begin(),
          [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
      return OrhunDegeri(std::move(sonuc));
    }

    if (metodAdi == "kucuk") {
      if (!argumanlar.empty()) {
        hataFirlat(satir, "metin.kucuk() argüman almaz.");
      }
      std::string sonuc = metin;
      std::transform(
          sonuc.begin(), sonuc.end(), sonuc.begin(),
          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return OrhunDegeri(std::move(sonuc));
    }

    if (metodAdi == "parcala") {
      if (argumanlar.size() != 1 ||
          !std::holds_alternative<std::string>(argumanlar[0].veri)) {
        hataFirlat(satir,
                   "metin.parcala(ayirici) için metin ayırıcı bekleniyor.");
      }
      const std::string ayirici = std::get<std::string>(argumanlar[0].veri);
      OrhunDegeri::ListeVeri sonuc;

      if (ayirici.empty()) {
        sonuc.reserve(metin.size());
        for (char c : metin) {
          sonuc.emplace_back(std::string(1, c));
        }
        return OrhunDegeri(std::move(sonuc));
      }

      std::size_t bas = 0;
      while (true) {
        const std::size_t konum = metin.find(ayirici, bas);
        if (konum == std::string::npos) {
          sonuc.emplace_back(metin.substr(bas));
          break;
        }
        sonuc.emplace_back(metin.substr(bas, konum - bas));
        bas = konum + ayirici.size();
      }
      return OrhunDegeri(std::move(sonuc));
    }

    if (metodAdi == "uzunluk") {
      if (!argumanlar.empty()) {
        hataFirlat(satir, "metin.uzunluk() argüman almaz.");
      }
      return OrhunDegeri(static_cast<int>(metin.size()));
    }

    hataFirlat(satir,
               oneriliMesaj("Metin için bilinmeyen metod: '" + metodAdi + "'",
                            metodAdi, sabitMetinMetodAdaylari()));
  }

  hataFirlat(satir, "Bu tipte nesne metodu çağrılamaz: '" + metodAdi + "'");
}

Interpreter::DegiskenTablosu &Interpreter::aktifKapsam() {
  if (!yerelKapsamYigini_.empty()) {
    return *yerelKapsamYigini_.back();
  }
  return globalHafiza_;
}

void Interpreter::atamaHedefiYaz(const std::string &ad,
                                 const OrhunDegeri &deger, bool bildirimMi,
                                 std::size_t satir) {
  (void)satir;
  if (bildirimMi || yerelKapsamYigini_.empty()) {
    aktifKapsam()[ad] = deger;
    return;
  }

  for (auto it = yerelKapsamYigini_.rbegin(); it != yerelKapsamYigini_.rend();
       ++it) {
    if (!*it) {
      continue;
    }
    const auto bulunan = (*it)->find(ad);
    if (bulunan != (*it)->end()) {
      bulunan->second = deger;
      return;
    }
  }

  globalHafiza_[ad] = deger;
}

OrhunDegeri &Interpreter::degiskenBulYazilabilir(const std::string &ad,
                                                 std::size_t satir) {
  for (auto it = yerelKapsamYigini_.rbegin(); it != yerelKapsamYigini_.rend();
       ++it) {
    if (!*it) {
      continue;
    }
    const auto bulunan = (*it)->find(ad);
    if (bulunan != (*it)->end()) {
      return bulunan->second;
    }
  }

  const auto global = globalHafiza_.find(ad);
  if (global != globalHafiza_.end()) {
    return global->second;
  }

  std::vector<std::string> adaylar;
  std::unordered_set<std::string> gorulen;
  for (const auto &kapsam : yerelKapsamYigini_) {
    if (!kapsam) {
      continue;
    }
    for (const auto &[isim, _] : *kapsam) {
      adayEkleTekil(adaylar, gorulen, isim);
    }
  }
  for (const auto &[isim, _] : globalHafiza_) {
    adayEkleTekil(adaylar, gorulen, isim);
  }
  for (const auto &[isim, _] : islevTablosu_) {
    adayEkleTekil(adaylar, gorulen, isim);
  }
  for (const auto &[isim, _] : gomuluIslevler_) {
    adayEkleTekil(adaylar, gorulen, isim);
  }
  hataFirlat(satir,
             oneriliMesaj("'" + ad + "' değişkeni bulunamadı.", ad, adaylar));
}

OrhunDegeri &Interpreter::atananHedefYazilabilir(const ASTNode *hedef,
                                                 std::size_t satir,
                                                 bool sonHedef) {
  if (const auto *kimlik = dynamic_cast<const KimlikNode *>(hedef)) {
    if (sonHedef) {
      return aktifKapsam()[kimlik->ad()];
    }
    return degiskenBulYazilabilir(kimlik->ad(), satir);
  }

  if (const auto *benim = dynamic_cast<const BenimErisimNode *>(hedef)) {
    OrhunDegeri &benimDegeri = degiskenBulYazilabilir("benim", satir);
    if (!std::holds_alternative<OrhunDegeri::NesneTipi>(benimDegeri.veri)) {
      hataFirlat(satir, "'benim' yalnızca nesne metodu içinde kullanılabilir.");
    }
    const auto &nesne = std::get<OrhunDegeri::NesneTipi>(benimDegeri.veri);
    if (!nesne || !nesne->alanlar) {
      hataFirlat(satir, "Geçersiz nesne üzerinde alan ataması yapılamaz.");
    }
    return (*nesne->alanlar)[benim->alanAdi()];
  }

  if (const auto *alan = dynamic_cast<const AlanErisimNode *>(hedef)) {
    OrhunDegeri &taban = atananHedefYazilabilir(alan->hedef(), satir, false);

    if (std::holds_alternative<OrhunDegeri::SozlukTipi>(taban.veri)) {
      const auto &sozluk = std::get<OrhunDegeri::SozlukTipi>(taban.veri);
      if (!sozluk) {
        hataFirlat(satir, "Boş sözlük üzerinde alan ataması yapılamaz.");
      }
      return (*sozluk)[alan->alanAdi()];
    }

    if (std::holds_alternative<OrhunDegeri::NesneTipi>(taban.veri)) {
      const auto &nesne = std::get<OrhunDegeri::NesneTipi>(taban.veri);
      if (!nesne || !nesne->alanlar) {
        hataFirlat(satir, "Geçersiz nesne üzerinde alan ataması yapılamaz.");
      }
      return (*nesne->alanlar)[alan->alanAdi()];
    }

    hataFirlat(
        satir,
        "Nokta ataması yalnızca sözlük veya nesne üzerinde yapılabilir.");
  }

  if (const auto *indeks = dynamic_cast<const IndeksErisimNode *>(hedef)) {
    OrhunDegeri &taban = atananHedefYazilabilir(indeks->hedef(), satir, false);
    const OrhunDegeri anahtar = ifadeHesapla(indeks->indeks());

    if (std::holds_alternative<OrhunDegeri::ListeTipi>(taban.veri)) {
      const auto &liste = std::get<OrhunDegeri::ListeTipi>(taban.veri);
      if (!liste) {
        hataFirlat(satir, "Boş liste üzerinde indeks ataması yapılamaz.");
      }
      const std::size_t idx =
          listeIndeksiCevir(anahtar, satir, "liste indeksi");
      if (idx >= liste->size()) {
        hataFirlat(satir, "Liste indeksi sınır dışında.");
      }
      return (*liste)[idx];
    }

    if (std::holds_alternative<OrhunDegeri::SozlukTipi>(taban.veri)) {
      if (!std::holds_alternative<std::string>(anahtar.veri)) {
        hataFirlat(satir, "Sözlük anahtarı metin olmalıdır.");
      }
      const auto &sozluk = std::get<OrhunDegeri::SozlukTipi>(taban.veri);
      if (!sozluk) {
        hataFirlat(satir, "Boş sözlük üzerinde indeks ataması yapılamaz.");
      }
      return (*sozluk)[std::get<std::string>(anahtar.veri)];
    }

    if (std::holds_alternative<OrhunDegeri::NesneTipi>(taban.veri)) {
      if (!std::holds_alternative<std::string>(anahtar.veri)) {
        hataFirlat(satir, "Nesne alan anahtarı metin olmalıdır.");
      }
      const auto &nesne = std::get<OrhunDegeri::NesneTipi>(taban.veri);
      if (!nesne || !nesne->alanlar) {
        hataFirlat(satir, "Geçersiz nesne üzerinde indeks ataması yapılamaz.");
      }
      return (*nesne->alanlar)[std::get<std::string>(anahtar.veri)];
    }

    hataFirlat(satir, "İndeks ataması yalnızca liste, sözlük veya nesne "
                      "üzerinde yapılabilir.");
  }

  hataFirlat(satir, "Geçersiz atama hedefi.");
}

const OrhunDegeri &Interpreter::degiskenBul(const std::string &ad,
                                            std::size_t satir) const {
  for (auto it = yerelKapsamYigini_.rbegin(); it != yerelKapsamYigini_.rend();
       ++it) {
    if (!*it) {
      continue;
    }
    const auto bulunan = (*it)->find(ad);
    if (bulunan != (*it)->end()) {
      return bulunan->second;
    }
  }

  const auto global = globalHafiza_.find(ad);
  if (global != globalHafiza_.end()) {
    return global->second;
  }

  std::vector<std::string> adaylar;
  std::unordered_set<std::string> gorulen;
  for (const auto &kapsam : yerelKapsamYigini_) {
    if (!kapsam) {
      continue;
    }
    for (const auto &[isim, _] : *kapsam) {
      adayEkleTekil(adaylar, gorulen, isim);
    }
  }
  for (const auto &[isim, _] : globalHafiza_) {
    adayEkleTekil(adaylar, gorulen, isim);
  }
  for (const auto &[isim, _] : islevTablosu_) {
    adayEkleTekil(adaylar, gorulen, isim);
  }
  for (const auto &[isim, _] : gomuluIslevler_) {
    adayEkleTekil(adaylar, gorulen, isim);
  }

  throw OrhunHatasi(
      "Satır " + std::to_string(satir) + ": " +
      oneriliMesaj("'" + ad + "' değişkeni bulunamadı.", ad, adaylar));
}

bool Interpreter::dogruMu(const OrhunDegeri &deger) const {
  if (std::holds_alternative<int>(deger.veri)) {
    return std::get<int>(deger.veri) != 0;
  }
  if (std::holds_alternative<double>(deger.veri)) {
    return std::fabs(std::get<double>(deger.veri)) > 1e-12;
  }
  if (std::holds_alternative<std::string>(deger.veri)) {
    return !std::get<std::string>(deger.veri).empty();
  }
  if (std::holds_alternative<OrhunDegeri::ListeTipi>(deger.veri)) {
    const auto &liste = std::get<OrhunDegeri::ListeTipi>(deger.veri);
    return liste && !liste->empty();
  }
  if (std::holds_alternative<OrhunDegeri::SozlukTipi>(deger.veri)) {
    const auto &sozluk = std::get<OrhunDegeri::SozlukTipi>(deger.veri);
    return sozluk && !sozluk->empty();
  }
  const auto &nesne = std::get<OrhunDegeri::NesneTipi>(deger.veri);
  return nesne != nullptr;
}

bool Interpreter::esittir(const OrhunDegeri &sol,
                          const OrhunDegeri &sag) const {
  if (sayiMi(sol) && sayiMi(sag)) {
    return std::fabs(sayiDegeri(sol, 0, "eşitlik") -
                     sayiDegeri(sag, 0, "eşitlik")) < 1e-12;
  }
  return sol == sag;
}

std::string Interpreter::metneCevir(const OrhunDegeri &deger) const {
  if (std::holds_alternative<int>(deger.veri)) {
    return std::to_string(std::get<int>(deger.veri));
  }

  if (std::holds_alternative<double>(deger.veri)) {
    std::ostringstream os;
    os << std::get<double>(deger.veri);
    std::string sonuc = os.str();

    // İnsan okunabilirlik için gereksiz sıfırları temizle.
    if (sonuc.find('.') != std::string::npos) {
      while (!sonuc.empty() && sonuc.back() == '0') {
        sonuc.pop_back();
      }
      if (!sonuc.empty() && sonuc.back() == '.') {
        sonuc.pop_back();
      }
      if (sonuc.empty()) {
        sonuc = "0";
      }
    }
    return sonuc;
  }

  if (std::holds_alternative<std::string>(deger.veri)) {
    return std::get<std::string>(deger.veri);
  }

  if (std::holds_alternative<OrhunDegeri::ListeTipi>(deger.veri)) {
    const auto &listePtr = std::get<OrhunDegeri::ListeTipi>(deger.veri);
    const OrhunDegeri::ListeVeri bosListe;
    const auto &liste = listePtr ? *listePtr : bosListe;
    std::string sonuc = "[";
    for (std::size_t i = 0; i < liste.size(); ++i) {
      if (i > 0) {
        sonuc += ", ";
      }
      sonuc += metneCevir(liste[i]);
    }
    sonuc += "]";
    return sonuc;
  }

  if (std::holds_alternative<OrhunDegeri::SozlukTipi>(deger.veri)) {
    const auto &sozlukPtr = std::get<OrhunDegeri::SozlukTipi>(deger.veri);
    const OrhunDegeri::SozlukVeri bosSozluk;
    const auto &sozluk = sozlukPtr ? *sozlukPtr : bosSozluk;
    std::string sonuc = "{";
    bool ilk = true;
    for (const auto &[anahtar, degerIc] : sozluk) {
      if (!ilk) {
        sonuc += ", ";
      }
      ilk = false;
      sonuc += anahtar + ": " + metneCevir(degerIc);
    }
    sonuc += "}";
    return sonuc;
  }

  const auto &nesne = std::get<OrhunDegeri::NesneTipi>(deger.veri);
  if (!nesne) {
    return "<bos_nesne>";
  }
  std::string sonuc = "<" + nesne->sinifAdi + " ";
  if (nesne->alanlar) {
    bool ilk = true;
    for (const auto &[anahtar, alanDegeri] : *nesne->alanlar) {
      if (!ilk) {
        sonuc += ", ";
      }
      ilk = false;
      sonuc += anahtar + "=" + metneCevir(alanDegeri);
    }
  }
  sonuc += ">";
  return sonuc;
}

std::string Interpreter::metinGomuleriCoz(const std::string &metin,
                                          std::size_t satir) const {
  std::string sonuc;
  sonuc.reserve(metin.size() + 16);

  std::size_t i = 0;
  while (i < metin.size()) {
    const char c = metin[i];
    if (c == '{') {
      if (i + 1 < metin.size() && metin[i + 1] == '{') {
        sonuc.push_back('{');
        i += 2;
        continue;
      }

      const std::size_t kapanis = metin.find('}', i + 1);
      if (kapanis == std::string::npos) {
        sonuc.push_back('{');
        ++i;
        continue;
      }

      const std::string hamYerTutucu = metin.substr(i + 1, kapanis - (i + 1));
      const std::string yerTutucu = kirpilmisKopya(hamYerTutucu);

      if (!yerTutucuYoluGecerliMi(yerTutucu)) {
        sonuc.push_back('{');
        ++i;
        continue;
      }

      if (yerTutucu.find('.') == std::string::npos) {
        sonuc += metneCevir(degiskenBul(yerTutucu, satir));
      } else {
        sonuc += metneCevir(noktaYoluDegeri(yerTutucu, satir));
      }
      i = kapanis + 1;
      continue;
    }

    if (c == '}') {
      if (i + 1 < metin.size() && metin[i + 1] == '}') {
        sonuc.push_back('}');
        i += 2;
        continue;
      }
      sonuc.push_back('}');
      ++i;
      continue;
    }

    sonuc.push_back(c);
    ++i;
  }

  return sonuc;
}

bool Interpreter::sayiMi(const OrhunDegeri &deger) const {
  return std::holds_alternative<int>(deger.veri) ||
         std::holds_alternative<double>(deger.veri);
}

double Interpreter::sayiDegeri(const OrhunDegeri &deger, std::size_t satir,
                               const std::string &baglam) const {
  if (std::holds_alternative<int>(deger.veri)) {
    return static_cast<double>(std::get<int>(deger.veri));
  }
  if (std::holds_alternative<double>(deger.veri)) {
    return std::get<double>(deger.veri);
  }

  if (satir == 0) {
    throw OrhunHatasi("Hata: " + baglam + " için sayı bekleniyordu.");
  }
  throw OrhunHatasi("Satır " + std::to_string(satir) + ": " + baglam +
                    " için sayı bekleniyordu.");
}

bool Interpreter::tamSayiMi(const OrhunDegeri &deger) const {
  if (std::holds_alternative<int>(deger.veri)) {
    return true;
  }
  if (std::holds_alternative<double>(deger.veri)) {
    return tamSayiMi(std::get<double>(deger.veri));
  }
  return false;
}

bool Interpreter::tamSayiMi(double deger) const {
  return std::fabs(deger - std::round(deger)) < 1e-12;
}

long long Interpreter::dilimSiniriCevir(const ASTNode *sinirIfadesi,
                                        long long varsayilan, long long uzunluk,
                                        std::size_t satir,
                                        const std::string &baglam) {
  if (sinirIfadesi == nullptr) {
    return varsayilan;
  }

  const OrhunDegeri deger = ifadeHesapla(sinirIfadesi);
  if (!sayiMi(deger)) {
    hataFirlat(satir, baglam + " sayı olmalıdır.");
  }

  const double ham = sayiDegeri(deger, satir, baglam);
  if (!tamSayiMi(ham)) {
    hataFirlat(satir, baglam + " tam sayı olmalıdır.");
  }

  long long sonuc = static_cast<long long>(ham);
  if (sonuc < 0) {
    sonuc = uzunluk + sonuc;
  }
  if (sonuc < 0) {
    sonuc = 0;
  }
  if (sonuc > uzunluk) {
    sonuc = uzunluk;
  }
  return sonuc;
}

std::size_t Interpreter::listeIndeksiCevir(const OrhunDegeri &deger,
                                           std::size_t satir,
                                           const std::string &baglam) const {
  double hamDeger = 0.0;
  if (std::holds_alternative<int>(deger.veri)) {
    hamDeger = static_cast<double>(std::get<int>(deger.veri));
  } else if (std::holds_alternative<double>(deger.veri)) {
    hamDeger = std::get<double>(deger.veri);
  } else {
    hataFirlat(satir, baglam + " tam sayı olmalıdır.");
  }

  if (!tamSayiMi(hamDeger)) {
    hataFirlat(satir, baglam + " tam sayı olmalıdır.");
  }
  if (hamDeger < 0.0) {
    hataFirlat(satir, baglam + " negatif olamaz.");
  }

  return static_cast<std::size_t>(hamDeger);
}

std::string Interpreter::stackTraceOlustur() const {
  if (cagriYigini_.empty()) {
    return "";
  }

  std::ostringstream ss;
  ss << "Stack Trace:\n";
  for (auto it = cagriYigini_.rbegin(); it != cagriYigini_.rend(); ++it) {
    ss << "  [satir " << it->satir << "] " << it->ad;
    if (std::next(it) != cagriYigini_.rend()) {
      ss << '\n';
    }
  }
  return ss.str();
}

[[noreturn]] void Interpreter::hataFirlat(std::size_t satir,
                                          const std::string &mesaj) const {
  std::string tamMesaj;
  if (satir == 0) {
    tamMesaj = "Hata: " + mesaj;
  } else {
    tamMesaj = "Satır " + std::to_string(satir) + ": " + mesaj;
  }

  if (mesaj.find("Stack Trace:") == std::string::npos) {
    const std::string trace = stackTraceOlustur();
    if (!trace.empty()) {
      tamMesaj += "\n\n" + trace;
    }
  }

  throw OrhunHatasi(tamMesaj);
}
