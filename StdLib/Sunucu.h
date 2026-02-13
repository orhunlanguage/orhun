#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

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
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

// Bu dosya yerlesik stdlib icindeki HTTP sunucu altyapisini tasir.
// Not: namespace yerlesik icinde include edilmek uzere yazilmistir.

#ifdef _WIN32
inline std::string sunucuWindowsHataMesaji(DWORD kod) {
  LPSTR tampon = nullptr;
  const DWORD uzunluk = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, kod, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPSTR>(&tampon), 0, nullptr);

  if (uzunluk == 0 || tampon == nullptr) {
    return "Windows hata kodu: " + std::to_string(kod);
  }

  std::string mesaj(tampon, uzunluk);
  LocalFree(tampon);
  while (!mesaj.empty() &&
         (mesaj.back() == '\r' || mesaj.back() == '\n' || mesaj.back() == ' ')) {
    mesaj.pop_back();
  }
  return mesaj;
}

struct WinSockApi {
  using WSAStartupFn = int(WSAAPI*)(WORD, LPWSADATA);
  using WSACleanupFn = int(WSAAPI*)(void);
  using SocketFn = SOCKET(WSAAPI*)(int, int, int);
  using BindFn = int(WSAAPI*)(SOCKET, const sockaddr*, int);
  using ListenFn = int(WSAAPI*)(SOCKET, int);
  using AcceptFn = SOCKET(WSAAPI*)(SOCKET, sockaddr*, int*);
  using RecvFn = int(WSAAPI*)(SOCKET, char*, int, int);
  using SendFn = int(WSAAPI*)(SOCKET, const char*, int, int);
  using CloseSocketFn = int(WSAAPI*)(SOCKET);
  using SetSockOptFn = int(WSAAPI*)(SOCKET, int, int, const char*, int);

  HMODULE kutuphane = nullptr;
  WSAStartupFn wsaStartup = nullptr;
  WSACleanupFn wsaCleanup = nullptr;
  SocketFn socketFn = nullptr;
  BindFn bindFn = nullptr;
  ListenFn listenFn = nullptr;
  AcceptFn acceptFn = nullptr;
  RecvFn recvFn = nullptr;
  SendFn sendFn = nullptr;
  CloseSocketFn closeSocketFn = nullptr;
  SetSockOptFn setSockOptFn = nullptr;
  bool hazir = false;

  WinSockApi() {
    kutuphane = LoadLibraryW(L"ws2_32.dll");
    if (!kutuphane) {
      throw std::runtime_error("ws2_32.dll yüklenemedi: " +
                               sunucuWindowsHataMesaji(GetLastError()));
    }
    try {
      yukle(wsaStartup, "WSAStartup");
      yukle(wsaCleanup, "WSACleanup");
      yukle(socketFn, "socket");
      yukle(bindFn, "bind");
      yukle(listenFn, "listen");
      yukle(acceptFn, "accept");
      yukle(recvFn, "recv");
      yukle(sendFn, "send");
      yukle(closeSocketFn, "closesocket");
      yukle(setSockOptFn, "setsockopt");

      WSADATA veri;
      const int sonuc = wsaStartup(MAKEWORD(2, 2), &veri);
      if (sonuc != 0) {
        throw std::runtime_error("WSAStartup başarısız (kod: " +
                                 std::to_string(sonuc) + ").");
      }
      hazir = true;
    } catch (...) {
      if (kutuphane != nullptr) {
        FreeLibrary(kutuphane);
        kutuphane = nullptr;
      }
      throw;
    }
  }

  ~WinSockApi() {
    if (hazir && wsaCleanup != nullptr) {
      wsaCleanup();
    }
    hazir = false;
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
      throw std::runtime_error(std::string("WinSock sembolü yüklenemedi: ") + ad);
    }
    static_assert(sizeof(hedef) == sizeof(ham),
                  "Fonksiyon imza boyutu FARPROC ile uyumsuz.");
    std::memcpy(&hedef, &ham, sizeof(hedef));
  }
};

inline WinSockApi& winSockApi() {
  static WinSockApi api;
  return api;
}
#endif

class BasitHttpSunucu {
public:
  BasitHttpSunucu() = default;
  ~BasitHttpSunucu() { durdur(); }

  BasitHttpSunucu(const BasitHttpSunucu&) = delete;
  BasitHttpSunucu& operator=(const BasitHttpSunucu&) = delete;

  bool baslat(int port, const std::string& kokKlasor, std::string* hataMesaji) {
    std::lock_guard<std::mutex> kilit(mutex_);
    if (calisiyor_.load()) {
      return true;
    }
    if (port <= 0 || port > 65535) {
      if (hataMesaji) {
        *hataMesaji = "Port aralığı 1..65535 olmalıdır.";
      }
      return false;
    }

    std::error_code ec;
    std::filesystem::path kok =
        kokKlasor.empty() ? std::filesystem::current_path(ec)
                          : std::filesystem::path(kokKlasor);
    if (ec) {
      if (hataMesaji) {
        *hataMesaji = "Kök klasör çözümlenemedi: " + ec.message();
      }
      return false;
    }
    if (!std::filesystem::exists(kok, ec) || ec ||
        !std::filesystem::is_directory(kok, ec) || ec) {
      if (hataMesaji) {
        *hataMesaji = "Kök klasör bulunamadı veya dizin değil.";
      }
      return false;
    }

    kok_ = std::filesystem::weakly_canonical(kok, ec);
    if (ec) {
      kok_ = std::filesystem::absolute(kok, ec);
      if (ec) {
        if (hataMesaji) {
          *hataMesaji = "Kök klasör mutlak yola çevrilemedi: " + ec.message();
        }
        return false;
      }
    }
    kok_ = kok_.lexically_normal();
    port_ = port;

    SocketTutamac dinleme = soketAc();
    if (!soketGecerli(dinleme)) {
      if (hataMesaji) {
        *hataMesaji = "Dinleme soketi açılamadı.";
      }
      return false;
    }

    int reuse = 1;
    if (!soketTekrarKullan(dinleme, &reuse, sizeof(reuse))) {
      soketKapat(dinleme);
      if (hataMesaji) {
        *hataMesaji = "Soket seçenekleri ayarlanamadı.";
      }
      return false;
    }

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = agSirasi32(INADDR_ANY);
    addr.sin_port = agSirasi16(static_cast<std::uint16_t>(port_));

    if (!soketBind(dinleme, reinterpret_cast<const sockaddr*>(&addr),
                   static_cast<int>(sizeof(addr)))) {
      soketKapat(dinleme);
      if (hataMesaji) {
        *hataMesaji = "Port dinlenemedi (kullanımda olabilir).";
      }
      return false;
    }
    if (!soketDinle(dinleme, 16)) {
      soketKapat(dinleme);
      if (hataMesaji) {
        *hataMesaji = "Soket dinlemeye alınamadı.";
      }
      return false;
    }

    durdurIstegi_.store(false);
    dinlemeSoketi_ = dinleme;
    calisiyor_.store(true);
    isParcacigi_ = std::thread([this]() { calistirDongu(); });
    return true;
  }

  void durdur() {
    SocketTutamac kapatilacak = kGecersizSocket;
    {
      std::lock_guard<std::mutex> kilit(mutex_);
      if (!calisiyor_.load() && !soketGecerli(dinlemeSoketi_)) {
        return;
      }
      durdurIstegi_.store(true);
      kapatilacak = dinlemeSoketi_;
      dinlemeSoketi_ = kGecersizSocket;
    }

    if (soketGecerli(kapatilacak)) {
      soketKapat(kapatilacak);
    }
    if (isParcacigi_.joinable()) {
      isParcacigi_.join();
    }
    calisiyor_.store(false);
  }

  bool calisiyorMu() const { return calisiyor_.load(); }

  int port() const { return port_; }

private:
#ifdef _WIN32
  using SocketTutamac = SOCKET;
  static constexpr SocketTutamac kGecersizSocket = INVALID_SOCKET;
#else
  using SocketTutamac = int;
  static constexpr SocketTutamac kGecersizSocket = -1;
#endif

  static std::uint16_t agSirasi16(std::uint16_t v) {
    return static_cast<std::uint16_t>((v >> 8) | (v << 8));
  }

  static std::uint32_t agSirasi32(std::uint32_t v) {
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
  }

  mutable std::mutex mutex_;
  std::atomic<bool> calisiyor_{false};
  std::atomic<bool> durdurIstegi_{false};
  std::thread isParcacigi_;
  std::filesystem::path kok_;
  int port_ = 0;
  SocketTutamac dinlemeSoketi_ = kGecersizSocket;

  static bool soketGecerli(SocketTutamac s) { return s != kGecersizSocket; }

  static SocketTutamac soketAc() {
#ifdef _WIN32
    return winSockApi().socketFn(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    return ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif
  }

  static bool soketTekrarKullan(SocketTutamac s, const void* deger, int boyut) {
#ifdef _WIN32
    return winSockApi().setSockOptFn(s, SOL_SOCKET, SO_REUSEADDR,
                                     static_cast<const char*>(deger),
                                     boyut) == 0;
#else
    return ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, deger, boyut) == 0;
#endif
  }

  static bool soketBind(SocketTutamac s, const sockaddr* addr, int addrBoyut) {
#ifdef _WIN32
    return winSockApi().bindFn(s, addr, addrBoyut) == 0;
#else
    return ::bind(s, addr, static_cast<socklen_t>(addrBoyut)) == 0;
#endif
  }

  static bool soketDinle(SocketTutamac s, int backlog) {
#ifdef _WIN32
    return winSockApi().listenFn(s, backlog) == 0;
#else
    return ::listen(s, backlog) == 0;
#endif
  }

  static SocketTutamac soketKabulEt(SocketTutamac s) {
    sockaddr_in istemci;
#ifdef _WIN32
    int boyut = static_cast<int>(sizeof(istemci));
    return winSockApi().acceptFn(s, reinterpret_cast<sockaddr*>(&istemci),
                                 &boyut);
#else
    socklen_t boyut = static_cast<socklen_t>(sizeof(istemci));
    return ::accept(s, reinterpret_cast<sockaddr*>(&istemci), &boyut);
#endif
  }

  static int soketOku(SocketTutamac s, char* tampon, int boyut) {
#ifdef _WIN32
    return winSockApi().recvFn(s, tampon, boyut, 0);
#else
    return ::recv(s, tampon, static_cast<std::size_t>(boyut), 0);
#endif
  }

  static int soketYaz(SocketTutamac s, const char* veri, int boyut) {
#ifdef _WIN32
    return winSockApi().sendFn(s, veri, boyut, 0);
#else
    return ::send(s, veri, static_cast<std::size_t>(boyut), 0);
#endif
  }

  static void soketKapat(SocketTutamac s) {
    if (!soketGecerli(s)) {
      return;
    }
#ifdef _WIN32
    winSockApi().closeSocketFn(s);
#else
    ::close(s);
#endif
  }

  static bool tumunuYaz(SocketTutamac soket, const char* veri,
                        std::size_t uzunluk) {
    std::size_t toplam = 0;
    while (toplam < uzunluk) {
      const std::size_t kalan = uzunluk - toplam;
      const int parcaboyut =
          static_cast<int>(kalan > 32768 ? 32768 : kalan);
      const int yazilan = soketYaz(soket, veri + toplam, parcaboyut);
      if (yazilan <= 0) {
        return false;
      }
      toplam += static_cast<std::size_t>(yazilan);
    }
    return true;
  }

  static std::string mimeTipi(const std::filesystem::path& dosya) {
    const std::string ext = dosya.extension().string();
    if (ext == ".html" || ext == ".htm") {
      return "text/html; charset=utf-8";
    }
    if (ext == ".css") {
      return "text/css; charset=utf-8";
    }
    if (ext == ".js") {
      return "application/javascript; charset=utf-8";
    }
    if (ext == ".json") {
      return "application/json; charset=utf-8";
    }
    if (ext == ".txt" || ext == ".oh") {
      return "text/plain; charset=utf-8";
    }
    if (ext == ".png") {
      return "image/png";
    }
    if (ext == ".jpg" || ext == ".jpeg") {
      return "image/jpeg";
    }
    if (ext == ".gif") {
      return "image/gif";
    }
    if (ext == ".svg") {
      return "image/svg+xml";
    }
    return "application/octet-stream";
  }

  static int hexDegeri(char c) {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
      return 10 + (c - 'A');
    }
    return -1;
  }

  static std::string urlCoz(const std::string& yol) {
    std::string sonuc;
    sonuc.reserve(yol.size());
    for (std::size_t i = 0; i < yol.size(); ++i) {
      const char c = yol[i];
      if (c == '%' && i + 2 < yol.size()) {
        const int hi = hexDegeri(yol[i + 1]);
        const int lo = hexDegeri(yol[i + 2]);
        if (hi >= 0 && lo >= 0) {
          sonuc.push_back(static_cast<char>((hi << 4) | lo));
          i += 2;
          continue;
        }
      }
      if (c == '+') {
        sonuc.push_back(' ');
        continue;
      }
      sonuc.push_back(c);
    }
    return sonuc;
  }

  bool yoluGuvenliCoz(const std::string& hamYol, std::filesystem::path& hedef,
                      int& httpKod) const {
    httpKod = 0;
    std::string yol = hamYol;
    const std::size_t soru = yol.find('?');
    if (soru != std::string::npos) {
      yol = yol.substr(0, soru);
    }
    const std::size_t diyez = yol.find('#');
    if (diyez != std::string::npos) {
      yol = yol.substr(0, diyez);
    }

    yol = urlCoz(yol);
    std::replace(yol.begin(), yol.end(), '\\', '/');
    if (!yol.empty() && yol.front() == '/') {
      yol.erase(yol.begin());
    }

    std::filesystem::path goreli;
    std::size_t bas = 0;
    while (bas <= yol.size()) {
      const std::size_t pos = yol.find('/', bas);
      const std::string parcasi =
          (pos == std::string::npos) ? yol.substr(bas) : yol.substr(bas, pos - bas);

      if (!parcasi.empty() && parcasi != ".") {
        if (parcasi == ".." || parcasi.find(':') != std::string::npos) {
          httpKod = 403;
          return false;
        }
        goreli /= parcasi;
      }

      if (pos == std::string::npos) {
        break;
      }
      bas = pos + 1;
    }

    if (goreli.empty()) {
      goreli = "index.html";
    }

    hedef = (kok_ / goreli).lexically_normal();
    if (std::filesystem::is_directory(hedef)) {
      hedef /= "index.html";
    }
    return true;
  }

  void cevapYaz(SocketTutamac istemci, int kod, const std::string& metin,
                const std::string& govde) {
    std::ostringstream baslik;
    baslik << "HTTP/1.1 " << kod << " " << metin << "\r\n"
           << "Content-Type: text/plain; charset=utf-8\r\n"
           << "Content-Length: " << govde.size() << "\r\n"
           << "Connection: close\r\n\r\n";
    const std::string baslikMetni = baslik.str();
    (void)tumunuYaz(istemci, baslikMetni.data(), baslikMetni.size());
    (void)tumunuYaz(istemci, govde.data(), govde.size());
  }

  void istekIsle(SocketTutamac istemci) {
    std::string istek;
    char tampon[4096];
    while (istek.find("\r\n\r\n") == std::string::npos &&
           istek.size() < 64 * 1024) {
      const int okunan = soketOku(istemci, tampon, static_cast<int>(sizeof(tampon)));
      if (okunan <= 0) {
        return;
      }
      istek.append(tampon, static_cast<std::size_t>(okunan));
    }

    const std::size_t satirSonu = istek.find("\r\n");
    if (satirSonu == std::string::npos) {
      cevapYaz(istemci, 400, "Bad Request", "Geçersiz HTTP isteği.");
      return;
    }
    const std::string ilkSatir = istek.substr(0, satirSonu);
    std::istringstream iss(ilkSatir);
    std::string metod;
    std::string yol;
    std::string surum;
    iss >> metod >> yol >> surum;
    if (metod != "GET") {
      cevapYaz(istemci, 405, "Method Not Allowed",
               "Sadece GET destekleniyor.");
      return;
    }

    std::filesystem::path hedef;
    int engelKodu = 0;
    if (!yoluGuvenliCoz(yol, hedef, engelKodu)) {
      cevapYaz(istemci, engelKodu == 0 ? 403 : engelKodu, "Forbidden",
               "Bu yola erişim engellendi.");
      return;
    }

    std::error_code ec;
    if (!std::filesystem::exists(hedef, ec) || ec ||
        !std::filesystem::is_regular_file(hedef, ec) || ec) {
      cevapYaz(istemci, 404, "Not Found", "Dosya bulunamadı.");
      return;
    }

    std::ifstream dosya(hedef, std::ios::binary);
    if (!dosya.is_open()) {
      cevapYaz(istemci, 500, "Internal Server Error",
               "Dosya okunamadı.");
      return;
    }
    std::string govde((std::istreambuf_iterator<char>(dosya)),
                      std::istreambuf_iterator<char>());

    std::ostringstream baslik;
    baslik << "HTTP/1.1 200 OK\r\n"
           << "Content-Type: " << mimeTipi(hedef) << "\r\n"
           << "Content-Length: " << govde.size() << "\r\n"
           << "Connection: close\r\n\r\n";
    const std::string baslikMetni = baslik.str();
    if (!tumunuYaz(istemci, baslikMetni.data(), baslikMetni.size())) {
      return;
    }
    (void)tumunuYaz(istemci, govde.data(), govde.size());
  }

  void calistirDongu() {
    while (!durdurIstegi_.load()) {
      SocketTutamac istemci = soketKabulEt(dinlemeSoketi_);
      if (!soketGecerli(istemci)) {
        if (durdurIstegi_.load()) {
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        continue;
      }

      istekIsle(istemci);
      soketKapat(istemci);
    }

    SocketTutamac kalan = kGecersizSocket;
    {
      std::lock_guard<std::mutex> kilit(mutex_);
      kalan = dinlemeSoketi_;
      dinlemeSoketi_ = kGecersizSocket;
    }
    if (soketGecerli(kalan)) {
      soketKapat(kalan);
    }
    calisiyor_.store(false);
  }
};

inline BasitHttpSunucu& paylasimliHttpSunucu() {
  static BasitHttpSunucu sunucu;
  return sunucu;
}


