#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
#include <winsock2.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace yerlesik {

struct JsonDeger {
  using Liste = std::vector<JsonDeger>;
  using Sozluk = std::map<std::string, JsonDeger>;
  using ListePtr = std::shared_ptr<Liste>;
  using SozlukPtr = std::shared_ptr<Sozluk>;

  std::variant<std::nullptr_t, bool, double, std::string, ListePtr, SozlukPtr>
      veri;

  JsonDeger() : veri(nullptr) {}
  JsonDeger(std::nullptr_t) : veri(nullptr) {}
  explicit JsonDeger(bool v) : veri(v) {}
  explicit JsonDeger(double v) : veri(v) {}
  explicit JsonDeger(std::string v) : veri(std::move(v)) {}
  explicit JsonDeger(const char* v) : veri(std::string(v)) {}
  explicit JsonDeger(Liste v) : veri(std::make_shared<Liste>(std::move(v))) {}
  explicit JsonDeger(Sozluk v) : veri(std::make_shared<Sozluk>(std::move(v))) {}
};

inline std::string kodNoktasiUtf8(char32_t cp) {
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

class JsonCozucu {
public:
  explicit JsonCozucu(std::string metin) : metin_(std::move(metin)) {}

  JsonDeger coz() {
    bosluklariAtla();
    JsonDeger sonuc = degerCoz();
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

  [[noreturn]] void hata(const std::string& mesaj) const {
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

  bool literalEslesir(const char* literal) {
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

  JsonDeger sayiCoz() {
    const std::size_t baslangic = konum_;
    if (eslesir('-')) {
      // opsiyonel işaret
    }

    if (eslesir('0')) {
      // 0 ile başlayan sayı
    } else {
      if (!(bak() >= '1' && bak() <= '9')) {
        hata("Geçersiz sayı");
      }
      while (bak() >= '0' && bak() <= '9') {
        ++konum_;
      }
    }

    if (eslesir('.')) {
      if (!(bak() >= '0' && bak() <= '9')) {
        hata("Ondalık noktadan sonra en az bir rakam bekleniyordu");
      }
      while (bak() >= '0' && bak() <= '9') {
        ++konum_;
      }
    }

    if (bak() == 'e' || bak() == 'E') {
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
    char* bitis = nullptr;
    const double d = std::strtod(parca.c_str(), &bitis);
    if (bitis == nullptr || *bitis != '\0') {
      hata("Sayı çözümlenemedi");
    }
    return JsonDeger(d);
  }

  JsonDeger listeCoz() {
    if (!eslesir('[')) {
      hata("Liste için '[' bekleniyordu");
    }

    JsonDeger::Liste liste;
    bosluklariAtla();
    if (eslesir(']')) {
      return JsonDeger(std::move(liste));
    }

    while (true) {
      liste.push_back(degerCoz());
      bosluklariAtla();
      if (eslesir(']')) {
        return JsonDeger(std::move(liste));
      }
      if (!eslesir(',')) {
        hata("Listede ',' veya ']' bekleniyordu");
      }
      bosluklariAtla();
    }
  }

  JsonDeger sozlukCoz() {
    if (!eslesir('{')) {
      hata("Sözlük için '{' bekleniyordu");
    }

    JsonDeger::Sozluk sozluk;
    bosluklariAtla();
    if (eslesir('}')) {
      return JsonDeger(std::move(sozluk));
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
      sozluk[anahtar] = degerCoz();
      bosluklariAtla();
      if (eslesir('}')) {
        return JsonDeger(std::move(sozluk));
      }
      if (!eslesir(',')) {
        hata("Sözlükte ',' veya '}' bekleniyordu");
      }
      bosluklariAtla();
    }
  }

  JsonDeger degerCoz() {
    bosluklariAtla();
    const char c = bak();
    if (c == '"') {
      return JsonDeger(metinCoz());
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
      return JsonDeger(true);
    }
    if (literalEslesir("false")) {
      return JsonDeger(false);
    }
    if (literalEslesir("null")) {
      return JsonDeger(nullptr);
    }
    hata("Geçersiz JSON değer başlangıcı");
  }
};

inline JsonDeger jsonCoz(const std::string& metin) {
  JsonCozucu cozucu(metin);
  return cozucu.coz();
}

inline std::string jsonMetniKacisla(const std::string& metin) {
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
        const char* hex = "0123456789ABCDEF";
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

inline std::string jsonGirintiUret(int seviye, int adim) {
  if (seviye <= 0 || adim <= 0) {
    return std::string();
  }
  return std::string(static_cast<std::size_t>(seviye * adim), ' ');
}

inline std::string jsonYazIc(const JsonDeger& deger, bool guzel, int adim,
                             int seviye) {
  if (std::holds_alternative<std::nullptr_t>(deger.veri)) {
    return "null";
  }
  if (const auto* m = std::get_if<bool>(&deger.veri)) {
    return *m ? "true" : "false";
  }
  if (const auto* s = std::get_if<double>(&deger.veri)) {
    std::ostringstream oss;
    oss << *s;
    return oss.str();
  }
  if (const auto* metin = std::get_if<std::string>(&deger.veri)) {
    return "\"" + jsonMetniKacisla(*metin) + "\"";
  }
  if (const auto* listePtr = std::get_if<JsonDeger::ListePtr>(&deger.veri)) {
    const JsonDeger::Liste bosListe;
    const auto& liste = (*listePtr) ? *(*listePtr) : bosListe;
    if (liste.empty()) {
      return "[]";
    }

    if (!guzel) {
      std::string json = "[";
      for (std::size_t i = 0; i < liste.size(); ++i) {
        if (i > 0) {
          json += ",";
        }
        json += jsonYazIc(liste[i], false, adim, seviye + 1);
      }
      json += "]";
      return json;
    }

    std::string json = "[\n";
    for (std::size_t i = 0; i < liste.size(); ++i) {
      json += jsonGirintiUret(seviye + 1, adim);
      json += jsonYazIc(liste[i], true, adim, seviye + 1);
      if (i + 1 < liste.size()) {
        json += ",";
      }
      json += "\n";
    }
    json += jsonGirintiUret(seviye, adim) + "]";
    return json;
  }

  const auto* sozlukPtr = std::get_if<JsonDeger::SozlukPtr>(&deger.veri);
  const JsonDeger::Sozluk bosSozluk;
  const auto& sozluk = (*sozlukPtr) ? *(*sozlukPtr) : bosSozluk;
  if (sozluk.empty()) {
    return "{}";
  }

  if (!guzel) {
    std::string json = "{";
    bool ilk = true;
    for (const auto& [anahtar, altDeger] : sozluk) {
      if (!ilk) {
        json += ",";
      }
      ilk = false;
      json += "\"" + jsonMetniKacisla(anahtar) + "\":";
      json += jsonYazIc(altDeger, false, adim, seviye + 1);
    }
    json += "}";
    return json;
  }

  std::string json = "{\n";
  std::size_t i = 0;
  for (const auto& [anahtar, altDeger] : sozluk) {
    json += jsonGirintiUret(seviye + 1, adim);
    json += "\"" + jsonMetniKacisla(anahtar) + "\": ";
    json += jsonYazIc(altDeger, true, adim, seviye + 1);
    if (i + 1 < sozluk.size()) {
      json += ",";
    }
    json += "\n";
    ++i;
  }
  json += jsonGirintiUret(seviye, adim) + "}";
  return json;
}

inline std::string jsonYaz(const JsonDeger& deger, bool guzel = false,
                           int adim = 2) {
  if (adim < 0) {
    adim = 0;
  }
  if (adim > 16) {
    adim = 16;
  }
  return jsonYazIc(deger, guzel, adim, 0);
}

#ifdef _WIN32
inline std::wstring utf8denWstringe(const std::string& metin) {
  if (metin.empty()) {
    return std::wstring();
  }
  const int gerekli = MultiByteToWideChar(
      CP_UTF8, 0, metin.c_str(), static_cast<int>(metin.size()), nullptr, 0);
  if (gerekli <= 0) {
    throw std::runtime_error("UTF-8 -> UTF-16 dönüşümü başarısız.");
  }
  std::wstring sonuc(static_cast<std::size_t>(gerekli), L'\0');
  const int yazilan =
      MultiByteToWideChar(CP_UTF8, 0, metin.c_str(),
                          static_cast<int>(metin.size()), sonuc.data(), gerekli);
  if (yazilan <= 0) {
    throw std::runtime_error("UTF-8 -> UTF-16 dönüşümü başarısız.");
  }
  return sonuc;
}

inline std::string windowsHataMesaji(DWORD kod) {
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
  while (!mesaj.empty() &&
         (mesaj.back() == '\n' || mesaj.back() == '\r' ||
          mesaj.back() == ' ' || mesaj.back() == '\t')) {
    mesaj.pop_back();
  }
  return mesaj;
}

struct WinHttpApi {
  using CrackUrlFn = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPURL_COMPONENTS);
  using OpenFn = HINTERNET(WINAPI*)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
  using ConnectFn = HINTERNET(WINAPI*)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
  using OpenRequestFn = HINTERNET(WINAPI*)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR,
                                           LPCWSTR, LPCWSTR const*, DWORD);
  using SetTimeoutsFn = BOOL(WINAPI*)(HINTERNET, int, int, int, int);
  using SendRequestFn = BOOL(WINAPI*)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD,
                                      DWORD, DWORD_PTR);
  using ReceiveResponseFn = BOOL(WINAPI*)(HINTERNET, LPVOID);
  using QueryHeadersFn =
      BOOL(WINAPI*)(HINTERNET, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD);
  using QueryDataAvailableFn = BOOL(WINAPI*)(HINTERNET, LPDWORD);
  using ReadDataFn = BOOL(WINAPI*)(HINTERNET, LPVOID, DWORD, LPDWORD);
  using CloseHandleFn = BOOL(WINAPI*)(HINTERNET);

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
  template <typename T>
  void yukle(T& hedef, const char* ad) {
    FARPROC ham = GetProcAddress(kutuphane, ad);
    if (!ham) {
      throw std::runtime_error(std::string("WinHTTP sembolü yüklenemedi: ") + ad);
    }
    static_assert(sizeof(hedef) == sizeof(ham),
                  "Fonksiyon imza boyutu FARPROC ile uyumsuz.");
    std::memcpy(&hedef, &ham, sizeof(hedef));
  }
};

inline WinHttpApi& winHttpApi() {
  static WinHttpApi api;
  return api;
}

inline std::string internetIcerigiGetir(const std::string& url) {
  WinHttpApi& api = winHttpApi();
  const std::wstring urlW = utf8denWstringe(url);

  URL_COMPONENTS parcalar;
  std::memset(&parcalar, 0, sizeof(parcalar));
  parcalar.dwStructSize = sizeof(parcalar);
  parcalar.dwSchemeLength = static_cast<DWORD>(-1);
  parcalar.dwHostNameLength = static_cast<DWORD>(-1);
  parcalar.dwUrlPathLength = static_cast<DWORD>(-1);
  parcalar.dwExtraInfoLength = static_cast<DWORD>(-1);

  if (!api.crackUrl(urlW.c_str(), static_cast<DWORD>(urlW.size()), 0, &parcalar)) {
    throw std::runtime_error("URL çözümlenemedi: " +
                             windowsHataMesaji(GetLastError()));
  }

  const std::wstring host(parcalar.lpszHostName, parcalar.dwHostNameLength);
  std::wstring yol = parcalar.dwUrlPathLength > 0
                         ? std::wstring(parcalar.lpszUrlPath, parcalar.dwUrlPathLength)
                         : L"/";
  if (parcalar.dwExtraInfoLength > 0) {
    yol.append(parcalar.lpszExtraInfo, parcalar.dwExtraInfoLength);
  }

  const DWORD istekBayraklari =
      parcalar.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;

  HINTERNET oturum = api.open(L"Orhun/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
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

  HINTERNET istek =
      api.openRequest(baglanti, L"GET", yol.c_str(), nullptr, WINHTTP_NO_REFERER,
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
  if (api.queryHeaders(istek, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
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
inline std::string shellTekTirnakKacis(const std::string& metin) {
  std::string sonuc;
  sonuc.reserve(metin.size() + 8);
  for (const char c : metin) {
    if (c == '\'') {
      sonuc += "'\\''";
    } else {
      sonuc.push_back(c);
    }
  }
  return sonuc;
}

inline std::string komutCalistirVeOku(const std::string& komut, int& cikisKodu) {
  FILE* boru = popen(komut.c_str(), "r");
  if (boru == nullptr) {
    throw std::runtime_error("Alt komut çalıştırılamadı.");
  }
  std::string cikti;
  char tampon[4096];
  while (std::fgets(tampon, static_cast<int>(sizeof(tampon)), boru) != nullptr) {
    cikti += tampon;
  }
  cikisKodu = pclose(boru);
  return cikti;
}

inline std::string internetIcerigiGetir(const std::string& url) {
  const std::string komut = "curl -L -s --fail '" + shellTekTirnakKacis(url) + "'";
  int kod = 0;
  std::string icerik = komutCalistirVeOku(komut, kod);
  if (kod != 0) {
    throw std::runtime_error("HTTP isteği başarısız oldu (çıkış kodu: " +
                             std::to_string(kod) + ").");
  }
  return icerik;
}
#endif

#include "StdLib/Sunucu.h"

} // namespace yerlesik
