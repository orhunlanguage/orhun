
#include "VM.h"
#include "Compiler.h"
#include "DynamicLibrary.h"
#include "Lexer.h"
#include "Parser.h"
#include "Yardimci.h"
#include "Yerlesik.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <utility>

namespace {

std::string sayiyiMetinlestir(double sayi) {
  std::ostringstream ss;
  ss << std::setprecision(15) << sayi;
  std::string metin = ss.str();
  if (metin.find('.') != std::string::npos) {
    while (!metin.empty() && metin.back() == '0') {
      metin.pop_back();
    }
    if (!metin.empty() && metin.back() == '.') {
      metin.pop_back();
    }
  }
  return metin.empty() ? "0" : metin;
}

std::string asciiBuyut(std::string metin) {
  std::transform(
      metin.begin(), metin.end(), metin.begin(),
      [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return metin;
}

std::string asciiKucult(std::string metin) {
  std::transform(
      metin.begin(), metin.end(), metin.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return metin;
}

std::string solaSagaKirp(std::string metin) {
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

bool tamSayiMi(double d) { return std::isfinite(d) && std::floor(d) == d; }

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
    const std::string politika = asciiKucult(solaSagaKirp(env));
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
      const std::string trim = asciiKucult(solaSagaKirp(parca));
      if (!trim.empty()) {
        izinliler.insert(trim);
      }
    }
  }
  return izinliler;
}

bool ffiYolAllowlistteMi(const std::string &yol,
                         const std::unordered_set<std::string> &izinliler) {
  const std::string norm = asciiKucult(solaSagaKirp(yol));
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

void ffiKullaniminiDogrula(const std::string &baglam) {
  if (ffiPolitikasiBul() != FFIPolitikasi::Off) {
    return;
  }
  throw std::runtime_error(baglam +
                           ": FFI politikasi kapali "
                           "(ORHUN_FFI_POLICY=off).");
}

void ffiKutuphaneErisiminiDogrula(const std::string &yol,
                                  const std::string &baglam) {
  ffiKullaniminiDogrula(baglam);
  if (ffiPolitikasiBul() == FFIPolitikasi::Full) {
    return;
  }
  const auto izinliler = ffiAllowlistiniBul();
  if (ffiYolAllowlistteMi(yol, izinliler)) {
    return;
  }
  throw std::runtime_error(
      baglam +
      ": FFI allowlist disi kutuphane. ORHUN_FFI_ALLOWLIST veya "
      "ORHUN_FFI_POLICY=full kullanin.");
}

std::string zamanBicimlendir(const char *bicim, std::time_t zaman) {
  std::tm tmDegeri{};
#ifdef _WIN32
  localtime_s(&tmDegeri, &zaman);
#else
  localtime_r(&zaman, &tmDegeri);
#endif
  std::ostringstream ss;
  ss << std::put_time(&tmDegeri, bicim);
  return ss.str();
}

std::intptr_t ffiHamCagir(std::uintptr_t fonksiyon,
                          const std::vector<std::intptr_t> &argumanlar) {
  switch (argumanlar.size()) {
  case 0:
    return reinterpret_cast<std::intptr_t (*)()>(fonksiyon)();
  case 1:
    return reinterpret_cast<std::intptr_t (*)(std::intptr_t)>(fonksiyon)(
        argumanlar[0]);
  case 2:
    return reinterpret_cast<std::intptr_t (*)(std::intptr_t, std::intptr_t)>(
        fonksiyon)(argumanlar[0], argumanlar[1]);
  case 3:
    return reinterpret_cast<std::intptr_t (*)(std::intptr_t, std::intptr_t,
                                              std::intptr_t)>(fonksiyon)(
        argumanlar[0], argumanlar[1], argumanlar[2]);
  case 4:
    return reinterpret_cast<std::intptr_t (*)(std::intptr_t, std::intptr_t,
                                              std::intptr_t, std::intptr_t)>(
        fonksiyon)(argumanlar[0], argumanlar[1], argumanlar[2], argumanlar[3]);
  case 5:
    return reinterpret_cast<std::intptr_t (*)(
        std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t,
        std::intptr_t)>(fonksiyon)(argumanlar[0], argumanlar[1], argumanlar[2],
                                   argumanlar[3], argumanlar[4]);
  case 6:
    return reinterpret_cast<std::intptr_t (*)(std::intptr_t, std::intptr_t,
                                              std::intptr_t, std::intptr_t,
                                              std::intptr_t, std::intptr_t)>(
        fonksiyon)(argumanlar[0], argumanlar[1], argumanlar[2], argumanlar[3],
                   argumanlar[4], argumanlar[5]);
  case 7:
    return reinterpret_cast<std::intptr_t (*)(
        std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t,
        std::intptr_t, std::intptr_t, std::intptr_t)>(fonksiyon)(
        argumanlar[0], argumanlar[1], argumanlar[2], argumanlar[3],
        argumanlar[4], argumanlar[5], argumanlar[6]);
  case 8:
    return reinterpret_cast<std::intptr_t (*)(
        std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t,
        std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t)>(fonksiyon)(
        argumanlar[0], argumanlar[1], argumanlar[2], argumanlar[3],
        argumanlar[4], argumanlar[5], argumanlar[6], argumanlar[7]);
  default:
    throw std::runtime_error(
        "ffi.cagir simdilik en fazla 8 arguman destekliyor.");
  }
}

double ffiCiftCagir(std::uintptr_t fonksiyon,
                    const std::vector<double> &argumanlar) {
  switch (argumanlar.size()) {
  case 0:
    return reinterpret_cast<double (*)()>(fonksiyon)();
  case 1:
    return reinterpret_cast<double (*)(double)>(fonksiyon)(argumanlar[0]);
  case 2:
    return reinterpret_cast<double (*)(double, double)>(fonksiyon)(
        argumanlar[0], argumanlar[1]);
  case 3:
    return reinterpret_cast<double (*)(double, double, double)>(fonksiyon)(
        argumanlar[0], argumanlar[1], argumanlar[2]);
  case 4:
    return reinterpret_cast<double (*)(double, double, double, double)>(
        fonksiyon)(argumanlar[0], argumanlar[1], argumanlar[2], argumanlar[3]);
  case 5:
    return reinterpret_cast<double (*)(double, double, double, double, double)>(
        fonksiyon)(argumanlar[0], argumanlar[1], argumanlar[2], argumanlar[3],
                   argumanlar[4]);
  case 6:
    return reinterpret_cast<double (*)(double, double, double, double, double,
                                       double)>(fonksiyon)(
        argumanlar[0], argumanlar[1], argumanlar[2], argumanlar[3],
        argumanlar[4], argumanlar[5]);
  case 7:
    return reinterpret_cast<double (*)(double, double, double, double, double,
                                       double, double)>(fonksiyon)(
        argumanlar[0], argumanlar[1], argumanlar[2], argumanlar[3],
        argumanlar[4], argumanlar[5], argumanlar[6]);
  case 8:
    return reinterpret_cast<double (*)(double, double, double, double, double,
                                       double, double, double)>(fonksiyon)(
        argumanlar[0], argumanlar[1], argumanlar[2], argumanlar[3],
        argumanlar[4], argumanlar[5], argumanlar[6], argumanlar[7]);
  default:
    throw std::runtime_error(
        "ffi.cagir_tanimli simdilik en fazla 8 arguman destekliyor.");
  }
}

yerlesik::JsonDeger vmDegerindenJsona(const Value &deger) {
  if (deger.bosMu()) {
    return yerlesik::JsonDeger(nullptr);
  }
  if (deger.mantikMi()) {
    return yerlesik::JsonDeger(deger.as.mantik);
  }
  if (deger.sayiMi()) {
    return yerlesik::JsonDeger(deger.as.sayi);
  }
  if (!deger.nesneMi() || deger.as.nesne == nullptr) {
    return yerlesik::JsonDeger(nullptr);
  }

  Obj *obj = deger.as.nesne;
  switch (obj->type) {
  case ObjType::STRING:
    return yerlesik::JsonDeger(static_cast<ObjString *>(obj)->deger);
  case ObjType::LIST: {
    yerlesik::JsonDeger::Liste liste;
    for (const Value &oge : static_cast<ObjList *>(obj)->ogeler) {
      liste.push_back(vmDegerindenJsona(oge));
    }
    return yerlesik::JsonDeger(std::move(liste));
  }
  case ObjType::DICT: {
    yerlesik::JsonDeger::Sozluk sozluk;
    for (const auto &[anahtar, alt] : static_cast<ObjDict *>(obj)->alanlar) {
      sozluk[anahtar] = vmDegerindenJsona(alt);
    }
    return yerlesik::JsonDeger(std::move(sozluk));
  }
  case ObjType::INSTANCE: {
    auto *nesne = static_cast<ObjInstance *>(obj);
    yerlesik::JsonDeger::Sozluk sozluk;
    if (nesne->sinif != nullptr) {
      sozluk["__sinif"] = yerlesik::JsonDeger(nesne->sinif->ad);
    }
    for (const auto &[anahtar, alt] : nesne->alanlar) {
      sozluk[anahtar] = vmDegerindenJsona(alt);
    }
    return yerlesik::JsonDeger(std::move(sozluk));
  }
  default:
    return yerlesik::JsonDeger(nullptr);
  }
}

Value jsondanVmDegerine(VM &vm, const yerlesik::JsonDeger &deger) {
  if (std::holds_alternative<std::nullptr_t>(deger.veri)) {
    return Value::bos();
  }
  if (const auto *m = std::get_if<bool>(&deger.veri)) {
    return Value::mantik(*m);
  }
  if (const auto *s = std::get_if<double>(&deger.veri)) {
    return Value::sayi(*s);
  }
  if (const auto *metin = std::get_if<std::string>(&deger.veri)) {
    return vm.yeniString(*metin);
  }
  if (const auto *listePtr =
          std::get_if<yerlesik::JsonDeger::ListePtr>(&deger.veri)) {
    std::vector<Value> liste;
    if (*listePtr) {
      liste.reserve((*listePtr)->size());
      for (const auto &oge : *(*listePtr)) {
        liste.push_back(jsondanVmDegerine(vm, oge));
      }
    }
    return vm.yeniListe(std::move(liste));
  }

  const auto *sozlukPtr =
      std::get_if<yerlesik::JsonDeger::SozlukPtr>(&deger.veri);
  std::unordered_map<std::string, Value> sozluk;
  if (sozlukPtr && *sozlukPtr) {
    for (const auto &[anahtar, alt] : *(*sozlukPtr)) {
      sozluk[anahtar] = jsondanVmDegerine(vm, alt);
    }
  }
  return vm.yeniSozluk(std::move(sozluk));
}

} // namespace

VM::VM() { sifirla(); }

void VM::sifirla() {
  chunk_ = nullptr;
  frameYigini_.clear();
  bekleyenKurucular_.clear();
  tryYigini_.clear();
  yigin_.clear();
  globaller_.clear();
  globalInlineCache_.clear();
  runtimeSabitCache_.clear();
  gorevler_.clear();
  gorevSonrakiKimlik_ = 1;
  ffiKutuphaneleri_.clear();
  ffiKutuphaneKimlikleri_.clear();
  ffiIslevBaglantilari_.clear();
  ffiSonrakiKimlik_ = 1;
  ffiSonrakiIslevKimlik_ = 1;
  modulChunklari_.clear();
  gcEsigi_ = 1024;
  gcErtelemeDerinligi_ = 0;
  yerlesikNativesYukle();
}

Value VM::yeniString(const std::string &metin) {
  return Value::nesne(memory_.allocate<ObjString>(metin));
}

Value VM::yeniListe(std::vector<Value> ogeler) {
  return Value::nesne(memory_.allocate<ObjList>(std::move(ogeler)));
}

Value VM::yeniSozluk(std::unordered_map<std::string, Value> alanlar) {
  return Value::nesne(memory_.allocate<ObjDict>(std::move(alanlar)));
}

Value VM::nativeOlustur(const std::string &ad, int arity,
                        ObjNative::NativeFn fn) {
  return Value::nesne(memory_.allocate<ObjNative>(ad, arity, std::move(fn)));
}

void VM::yerlesikNativesYukle() {
  auto ekleNative = [&](const std::string &ad, int arity,
                        ObjNative::NativeFn fn) {
    globaller_[ad] = nativeOlustur(ad, arity, std::move(fn));
  };

  ekleNative("yazdir", -1, [](VM &vm, const std::vector<Value> &args) -> Value {
    for (std::size_t i = 0; i < args.size(); ++i) {
      if (i > 0) {
        std::cout << ' ';
      }
      std::cout << vm.metneCevir(args[i]);
    }
    std::cout << '\n';
    return Value::bos();
  });
  globaller_["yaz"] = globaller_["yazdir"];

  ekleNative("sor", 1, [](VM &vm, const std::vector<Value> &args) -> Value {
    std::cout << vm.metneCevir(args[0]);
    std::cout.flush();
    std::string giris;
    std::getline(std::cin, giris);
    return vm.yeniString(giris);
  });
  globaller_["oku"] = globaller_["sor"];

  ekleNative("karekok", 1, [](VM &vm, const std::vector<Value> &args) -> Value {
    return Value::sayi(std::sqrt(vm.sayiyaCevir(args[0], "karekok")));
  });
  ekleNative("us", 2, [](VM &vm, const std::vector<Value> &args) -> Value {
    return Value::sayi(
        std::pow(vm.sayiyaCevir(args[0], "us"), vm.sayiyaCevir(args[1], "us")));
  });
  ekleNative("tam", 1, [](VM &vm, const std::vector<Value> &args) -> Value {
    return Value::sayi(std::trunc(vm.sayiyaCevir(args[0], "tam")));
  });
  ekleNative("taban", 1, [](VM &vm, const std::vector<Value> &args) -> Value {
    return Value::sayi(std::floor(vm.sayiyaCevir(args[0], "taban")));
  });
  ekleNative("tam", 1, [](VM &vm, const std::vector<Value> &args) -> Value {
    return Value::sayi(std::trunc(vm.sayiyaCevir(args[0], "tam")));
  });
  ekleNative("taban", 1, [](VM &vm, const std::vector<Value> &args) -> Value {
    return Value::sayi(std::floor(vm.sayiyaCevir(args[0], "taban")));
  });

  static thread_local std::mt19937 rng{std::random_device{}()};
  ekleNative(
      "rastgele", 2, [](VM &vm, const std::vector<Value> &args) -> Value {
        const int minV =
            static_cast<int>(std::llround(vm.sayiyaCevir(args[0], "rastgele")));
        const int maxV =
            static_cast<int>(std::llround(vm.sayiyaCevir(args[1], "rastgele")));
        if (minV > maxV) {
          throw std::runtime_error("rastgele(min,max) araligi gecersiz.");
        }
        std::uniform_int_distribution<int> dist(minV, maxV);
        return Value::sayi(static_cast<double>(dist(rng)));
      });

  ekleNative("zaman", 0, [](VM &, const std::vector<Value> &) -> Value {
    const auto s = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
    return Value::sayi(static_cast<double>(s));
  });

  ekleNative("bekle", 1, [](VM &vm, const std::vector<Value> &args) -> Value {
    const double s = vm.sayiyaCevir(args[0], "bekle");
    if (s > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(
          static_cast<int>(std::llround(s * 1000.0))));
    }
    return Value::bos();
  });

  ekleNative("buyuk_harf", 1,
             [](VM &vm, const std::vector<Value> &args) -> Value {
               return vm.yeniString(asciiBuyut(vm.metneCevir(args[0])));
             });
  ekleNative("kucuk_harf", 1,
             [](VM &vm, const std::vector<Value> &args) -> Value {
               return vm.yeniString(asciiKucult(vm.metneCevir(args[0])));
             });

  ekleNative("metne_cevir", 1,
             [](VM &vm, const std::vector<Value> &args) -> Value {
               return vm.yeniString(vm.metneCevir(args[0]));
             });
  ekleNative("sayiya_cevir", 1,
             [](VM &vm, const std::vector<Value> &args) -> Value {
               try {
                 return Value::sayi(std::stod(vm.metneCevir(args[0])));
               } catch (...) {
                 throw std::runtime_error("sayiya_cevir: gecersiz sayi.");
               }
             });
  ekleNative("icerir", 2, [](VM &vm, const std::vector<Value> &args) -> Value {
    const std::string kaynak = vm.metneCevir(args[0]);
    const std::string aranan = vm.metneCevir(args[1]);
    return Value::mantik(kaynak.find(aranan) != std::string::npos);
  });

  ekleNative("uzunluk", 1, [](VM &, const std::vector<Value> &args) -> Value {
    const Value &d = args[0];
    if (d.nesneMi() && d.as.nesne != nullptr) {
      if (d.as.nesne->type == ObjType::STRING) {
        return Value::sayi(static_cast<double>(
            static_cast<ObjString *>(d.as.nesne)->deger.size()));
      }
      if (d.as.nesne->type == ObjType::LIST) {
        return Value::sayi(static_cast<double>(
            static_cast<ObjList *>(d.as.nesne)->ogeler.size()));
      }
      if (d.as.nesne->type == ObjType::DICT) {
        return Value::sayi(static_cast<double>(
            static_cast<ObjDict *>(d.as.nesne)->alanlar.size()));
      }
    }
    throw std::runtime_error("uzunluk: metin/liste/sozluk bekleniyor.");
  });

  ekleNative("aralik", -1, [](VM &vm, const std::vector<Value> &args) -> Value {
    if (args.empty() || args.size() > 3) {
      throw std::runtime_error(
          "aralik([baslangic], bitis, [adim]) bir, iki veya uc arguman alir.");
    }

    const long long baslangic =
        args.size() == 1
            ? 0
            : static_cast<long long>(
                  std::llround(vm.sayiyaCevir(args[0], "aralik")));
    const long long bitis = static_cast<long long>(std::llround(
        vm.sayiyaCevir(args.size() == 1 ? args[0] : args[1], "aralik")));
    const long long adim =
        args.size() < 3
            ? 1
            : static_cast<long long>(
                  std::llround(vm.sayiyaCevir(args[2], "aralik")));

    std::vector<Value> sonuc;
    if (adim == 0) {
      return vm.yeniListe(std::move(sonuc));
    }
    for (long long i = baslangic;
         adim > 0 ? i < bitis : i > bitis; i += adim) {
      sonuc.push_back(Value::sayi(static_cast<double>(i)));
    }
    return vm.yeniListe(std::move(sonuc));
  });
  globaller_["aralık"] = globaller_["aralik"];

  auto koleksiyonUzunluguBul = [](const Value &deger,
                                  const std::string &baglam) -> std::size_t {
    if (deger.nesneMi() && deger.as.nesne != nullptr) {
      if (deger.as.nesne->type == ObjType::STRING) {
        return static_cast<ObjString *>(deger.as.nesne)->deger.size();
      }
      if (deger.as.nesne->type == ObjType::LIST) {
        return static_cast<ObjList *>(deger.as.nesne)->ogeler.size();
      }
      if (deger.as.nesne->type == ObjType::DICT) {
        return static_cast<ObjDict *>(deger.as.nesne)->alanlar.size();
      }
    }
    throw std::runtime_error(baglam + ": metin, liste veya sozluk bekleniyor.");
  };

  ekleNative("bos_mu", 1,
             [koleksiyonUzunluguBul](VM &, const std::vector<Value> &args)
                 -> Value {
               return Value::mantik(
                   koleksiyonUzunluguBul(args[0], "bos_mu") == 0);
             });
  globaller_["boş_mu"] = globaller_["bos_mu"];

  ekleNative("dolu_mu", 1,
             [koleksiyonUzunluguBul](VM &, const std::vector<Value> &args)
                 -> Value {
               return Value::mantik(
                   koleksiyonUzunluguBul(args[0], "dolu_mu") > 0);
             });

  ekleNative("ilk", -1, [](VM &, const std::vector<Value> &args) -> Value {
    if (args.empty() || args.size() > 2) {
      throw std::runtime_error("ilk(liste, [yedek]) bir veya iki arguman alir.");
    }
    const Value &hedef = args[0];
    if (!hedef.nesneMi() || hedef.as.nesne == nullptr ||
        hedef.as.nesne->type != ObjType::LIST) {
      throw std::runtime_error("ilk icin ilk arguman liste olmalidir.");
    }
    const auto *liste = static_cast<ObjList *>(hedef.as.nesne);
    if (!liste->ogeler.empty()) {
      return liste->ogeler.front();
    }
    if (args.size() == 2) {
      return args[1];
    }
    throw std::runtime_error("ilk bos liste icin yedek arguman ister.");
  });

  ekleNative("son", -1, [](VM &, const std::vector<Value> &args) -> Value {
    if (args.empty() || args.size() > 2) {
      throw std::runtime_error("son(liste, [yedek]) bir veya iki arguman alir.");
    }
    const Value &hedef = args[0];
    if (!hedef.nesneMi() || hedef.as.nesne == nullptr ||
        hedef.as.nesne->type != ObjType::LIST) {
      throw std::runtime_error("son icin ilk arguman liste olmalidir.");
    }
    const auto *liste = static_cast<ObjList *>(hedef.as.nesne);
    if (!liste->ogeler.empty()) {
      return liste->ogeler.back();
    }
    if (args.size() == 2) {
      return args[1];
    }
    throw std::runtime_error("son bos liste icin yedek arguman ister.");
  });

  ekleNative(
      "listeye_ekle", 2, [](VM &, const std::vector<Value> &args) -> Value {
        const Value &hedef = args[0];
        if (!hedef.nesneMi() || hedef.as.nesne == nullptr ||
            hedef.as.nesne->type != ObjType::LIST) {
          throw std::runtime_error("listeye_ekle: ilk arguman liste olmali.");
        }
        static_cast<ObjList *>(hedef.as.nesne)->ogeler.push_back(args[1]);
        return Value::bos();
      });

  ekleNative(
      "dilim_al", 3, [](VM &vm, const std::vector<Value> &args) -> Value {
        const Value &hedef = args[0];
        const Value &basDeger = args[1];
        const Value &bitDeger = args[2];

        auto sinirCevir = [&vm](const Value &deger, long long varsayilan,
                                long long uzunluk,
                                const std::string &baglam) -> long long {
          if (deger.bosMu()) {
            return varsayilan;
          }
          const double d = vm.sayiyaCevir(deger, baglam);
          long long idx = static_cast<long long>(std::llround(d));
          if (idx < 0) {
            idx = uzunluk + idx;
          }
          if (idx < 0) {
            idx = 0;
          }
          if (idx > uzunluk) {
            idx = uzunluk;
          }
          return idx;
        };

        if (!hedef.nesneMi() || hedef.as.nesne == nullptr) {
          throw std::runtime_error("dilim_al: hedef liste veya metin olmali.");
        }

        if (hedef.as.nesne->type == ObjType::LIST) {
          auto *liste = static_cast<ObjList *>(hedef.as.nesne);
          const long long uzunluk =
              static_cast<long long>(liste->ogeler.size());
          long long bas =
              sinirCevir(basDeger, 0, uzunluk, "liste dilim baslangici");
          long long bit =
              sinirCevir(bitDeger, uzunluk, uzunluk, "liste dilim bitisi");
          if (bit < bas) {
            bit = bas;
          }

          std::vector<Value> sonuc;
          sonuc.reserve(static_cast<std::size_t>(bit - bas));
          for (long long i = bas; i < bit; ++i) {
            sonuc.push_back(liste->ogeler[static_cast<std::size_t>(i)]);
          }
          return vm.yeniListe(std::move(sonuc));
        }

        if (hedef.as.nesne->type == ObjType::STRING) {
          const std::string &metin =
              static_cast<ObjString *>(hedef.as.nesne)->deger;
          const long long uzunluk = static_cast<long long>(metin.size());
          long long bas =
              sinirCevir(basDeger, 0, uzunluk, "metin dilim baslangici");
          long long bit =
              sinirCevir(bitDeger, uzunluk, uzunluk, "metin dilim bitisi");
          if (bit < bas) {
            bit = bas;
          }

          return vm.yeniString(
              metin.substr(static_cast<std::size_t>(bas),
                           static_cast<std::size_t>(bit - bas)));
        }

        throw std::runtime_error("dilim_al: hedef liste veya metin olmali.");
      });

  ekleNative("parcala", 2, [](VM &vm, const std::vector<Value> &args) -> Value {
    const std::string metin = vm.metneCevir(args[0]);
    const std::string ayirici = vm.metneCevir(args[1]);
    std::vector<Value> parcalar;
    if (ayirici.empty()) {
      for (char c : metin) {
        parcalar.push_back(vm.yeniString(std::string(1, c)));
      }
      return vm.yeniListe(std::move(parcalar));
    }
    std::size_t bas = 0;
    while (true) {
      const std::size_t pos = metin.find(ayirici, bas);
      if (pos == std::string::npos) {
        parcalar.push_back(vm.yeniString(metin.substr(bas)));
        break;
      }
      parcalar.push_back(vm.yeniString(metin.substr(bas, pos - bas)));
      bas = pos + ayirici.size();
    }
    return vm.yeniListe(std::move(parcalar));
  });

  auto *matematik = memory_.allocate<ObjDict>();
  matematik->alanlar["pi"] = Value::sayi(3.14159265358979323846);
  matematik->alanlar["karekok"] = globaller_["karekok"];
  matematik->alanlar["us"] = globaller_["us"];
  matematik->alanlar["rastgele"] = globaller_["rastgele"];
  globaller_["matematik"] = Value::nesne(matematik);

  auto *dosya = memory_.allocate<ObjDict>();
  dosya->alanlar["oku"] = nativeOlustur(
      "dosya.oku", 1, [](VM &vm, const std::vector<Value> &a) -> Value {
        const std::string yol = vm.metneCevir(a[0]);
        std::ifstream in(yol, std::ios::binary);
        if (!in.is_open()) {
          throw std::runtime_error("dosya.oku: acilamadi: " + yol);
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        return vm.yeniString(ss.str());
      });
  dosya->alanlar["yaz"] = nativeOlustur(
      "dosya.yaz", 2, [](VM &vm, const std::vector<Value> &a) -> Value {
        const std::string yol = vm.metneCevir(a[0]);
        const std::string icerik = vm.metneCevir(a[1]);
        std::ofstream out(yol, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
          throw std::runtime_error("dosya.yaz: acilamadi: " + yol);
        }
        out << icerik;
        return Value::bos();
      });
  dosya->alanlar["var_mi"] = nativeOlustur(
      "dosya.var_mi", 1, [](VM &vm, const std::vector<Value> &a) -> Value {
        const std::string yol = vm.metneCevir(a[0]);
        std::error_code ec;
        const bool var = std::filesystem::exists(yol, ec);
        if (ec) {
          throw std::runtime_error("dosya.var_mi: " + ec.message());
        }
        return Value::mantik(var);
      });
  dosya->alanlar["sil"] = nativeOlustur(
      "dosya.sil", 1, [](VM &vm, const std::vector<Value> &a) -> Value {
        const std::string yol = vm.metneCevir(a[0]);
        std::error_code ec;
        const bool silindi = std::filesystem::remove(yol, ec);
        if (ec) {
          throw std::runtime_error("dosya.sil: " + ec.message());
        }
        return Value::mantik(silindi);
      });
  dosya->alanlar["ekle_satir"] = nativeOlustur(
      "dosya.ekle_satir", 2, [](VM &vm, const std::vector<Value> &a) -> Value {
        const std::string yol = vm.metneCevir(a[0]);
        const std::string icerik = vm.metneCevir(a[1]);
        std::ofstream out(yol, std::ios::binary | std::ios::app);
        if (!out.is_open()) {
          throw std::runtime_error("dosya.ekle_satir: acilamadi: " + yol);
        }
        out << icerik << '\n';
        return Value::bos();
      });
  dosya->alanlar["listele"] = nativeOlustur(
      "dosya.listele", 1, [](VM &vm, const std::vector<Value> &a) -> Value {
        const std::string yol = vm.metneCevir(a[0]);
        std::error_code ec;
        if (!std::filesystem::exists(yol, ec)) {
          if (ec) {
            throw std::runtime_error("dosya.listele: " + ec.message());
          }
          throw std::runtime_error("dosya.listele: klasor bulunamadi: " + yol);
        }
        std::vector<std::string> adlar;
        for (const auto &giris : std::filesystem::directory_iterator(yol, ec)) {
          if (ec) {
            throw std::runtime_error("dosya.listele: " + ec.message());
          }
          adlar.push_back(giris.path().filename().u8string());
        }
        std::sort(adlar.begin(), adlar.end());
        std::vector<Value> liste;
        liste.reserve(adlar.size());
        for (const auto &ad : adlar) {
          liste.push_back(vm.yeniString(ad));
        }
        return vm.yeniListe(std::move(liste));
      });
  dosya->alanlar["klasor_olustur"] = nativeOlustur(
      "dosya.klasor_olustur", 1,
      [](VM &vm, const std::vector<Value> &a) -> Value {
        const std::string yol = vm.metneCevir(a[0]);
        std::error_code ec;
        const bool olustu = std::filesystem::create_directories(yol, ec);
        if (ec) {
          throw std::runtime_error("dosya.klasor_olustur: " + ec.message());
        }
        return Value::mantik(olustu || std::filesystem::exists(yol));
      });
  globaller_["dosya"] = Value::nesne(dosya);

  auto *internet = memory_.allocate<ObjDict>();
  internet->alanlar["getir"] = nativeOlustur(
      "internet.getir", 1, [](VM &vm, const std::vector<Value> &a) -> Value {
        const std::string url = vm.metneCevir(a[0]);
        return vm.yeniString(yerlesik::internetIcerigiGetir(url));
      });
  internet->alanlar["indir"] = nativeOlustur(
      "internet.indir", 2, [](VM &vm, const std::vector<Value> &a) -> Value {
        const std::string url = vm.metneCevir(a[0]);
        const std::string yol = vm.metneCevir(a[1]);
        const std::string icerik = yerlesik::internetIcerigiGetir(url);
        std::ofstream dosya(yol, std::ios::binary | std::ios::trunc);
        if (!dosya.is_open()) {
          throw std::runtime_error("internet.indir: dosya acilamadi: " + yol);
        }
        dosya.write(icerik.data(), static_cast<std::streamsize>(icerik.size()));
        return Value::sayi(static_cast<double>(icerik.size()));
      });
  globaller_["internet"] = Value::nesne(internet);

  auto *json = memory_.allocate<ObjDict>();
  json->alanlar["coz"] = nativeOlustur(
      "json.coz", 1, [](VM &vm, const std::vector<Value> &a) -> Value {
        try {
          const std::string metin = vm.metneCevir(a[0]);
          const yerlesik::JsonDeger json = yerlesik::jsonCoz(metin);
          return jsondanVmDegerine(vm, json);
        } catch (const std::exception &ex) {
          throw std::runtime_error("json.coz hatasi: " +
                                   std::string(ex.what()));
        }
      });
  json->alanlar["yaz"] = nativeOlustur(
      "json.yaz", 1, [](VM &vm, const std::vector<Value> &a) -> Value {
        try {
          const yerlesik::JsonDeger json = vmDegerindenJsona(a[0]);
          return vm.yeniString(yerlesik::jsonYaz(json, false, 2));
        } catch (const std::exception &ex) {
          throw std::runtime_error("json.yaz hatasi: " +
                                   std::string(ex.what()));
        }
      });
  json->alanlar["guzel_yaz"] = nativeOlustur(
      "json.guzel_yaz", -1, [](VM &vm, const std::vector<Value> &a) -> Value {
        if (a.empty() || a.size() > 2) {
          throw std::runtime_error(
              "json.guzel_yaz(deger, [girinti]) bir veya iki arguman alir.");
        }
        int girinti = 2;
        if (a.size() == 2) {
          const double d = vm.sayiyaCevir(a[1], "json.guzel_yaz");
          if (!tamSayiMi(d)) {
            throw std::runtime_error("json.guzel_yaz girinti tam sayi olmali.");
          }
          girinti = static_cast<int>(d);
          if (girinti < 0 || girinti > 16) {
            throw std::runtime_error("json.guzel_yaz girinti araligi 0..16.");
          }
        }
        try {
          const yerlesik::JsonDeger json = vmDegerindenJsona(a[0]);
          return vm.yeniString(yerlesik::jsonYaz(json, true, girinti));
        } catch (const std::exception &ex) {
          throw std::runtime_error("json.guzel_yaz hatasi: " +
                                   std::string(ex.what()));
        }
      });
  globaller_["json"] = Value::nesne(json);

  auto *metin = memory_.allocate<ObjDict>();
  metin->alanlar["buyuk"] = nativeOlustur(
      "metin.buyuk", 1, [](VM &vm, const std::vector<Value> &a) -> Value {
        return vm.yeniString(asciiBuyut(vm.metneCevir(a[0])));
      });
  metin->alanlar["kucuk"] = nativeOlustur(
      "metin.kucuk", 1, [](VM &vm, const std::vector<Value> &a) -> Value {
        return vm.yeniString(asciiKucult(vm.metneCevir(a[0])));
      });
  metin->alanlar["uzunluk"] = nativeOlustur(
      "metin.uzunluk", 1, [](VM &vm, const std::vector<Value> &a) -> Value {
        return Value::sayi(static_cast<double>(vm.metneCevir(a[0]).size()));
      });
  metin->alanlar["icerir"] = nativeOlustur(
      "metin.icerir", 2, [](VM &vm, const std::vector<Value> &a) -> Value {
        const std::string kaynak = vm.metneCevir(a[0]);
        const std::string aranan = vm.metneCevir(a[1]);
        return Value::mantik(kaynak.find(aranan) != std::string::npos);
      });
  metin->alanlar["parcala"] = globaller_["parcala"];
  metin->alanlar["birlestir"] = nativeOlustur(
      "metin.birlestir", 2, [](VM &vm, const std::vector<Value> &a) -> Value {
        if (!a[0].nesneMi() || a[0].as.nesne == nullptr ||
            a[0].as.nesne->type != ObjType::LIST) {
          throw std::runtime_error(
              "metin.birlestir ilk argumanda liste bekler.");
        }
        const std::string ayirici = vm.metneCevir(a[1]);
        const auto *liste = static_cast<ObjList *>(a[0].as.nesne);
        std::string sonuc;
        for (std::size_t i = 0; i < liste->ogeler.size(); ++i) {
          if (i > 0) {
            sonuc += ayirici;
          }
          sonuc += vm.metneCevir(liste->ogeler[i]);
        }
        return vm.yeniString(sonuc);
      });
  globaller_["metin"] = Value::nesne(metin);

  auto *regex = memory_.allocate<ObjDict>();
  regex->alanlar["eslesir"] = nativeOlustur(
      "regex.eslesir", 2, [](VM &vm, const std::vector<Value> &a) -> Value {
        try {
          const std::regex desen(vm.metneCevir(a[1]));
          return Value::mantik(std::regex_search(vm.metneCevir(a[0]), desen));
        } catch (const std::regex_error &ex) {
          throw std::runtime_error("regex.eslesir deseni gecersiz: " +
                                   std::string(ex.what()));
        }
      });
  regex->alanlar["ilk"] = nativeOlustur(
      "regex.ilk", 2, [](VM &vm, const std::vector<Value> &a) -> Value {
        try {
          const std::regex desen(vm.metneCevir(a[1]));
          std::smatch sonuc;
          const std::string kaynak = vm.metneCevir(a[0]);
          if (!std::regex_search(kaynak, sonuc, desen)) {
            return vm.yeniString("");
          }
          return vm.yeniString(sonuc.str(0));
        } catch (const std::regex_error &ex) {
          throw std::runtime_error("regex.ilk deseni gecersiz: " +
                                   std::string(ex.what()));
        }
      });
  regex->alanlar["tum"] = nativeOlustur(
      "regex.tum", 2, [](VM &vm, const std::vector<Value> &a) -> Value {
        try {
          const std::regex desen(vm.metneCevir(a[1]));
          const std::string kaynak = vm.metneCevir(a[0]);
          std::vector<Value> liste;
          for (std::sregex_iterator it(kaynak.begin(), kaynak.end(), desen),
               son;
               it != son; ++it) {
            liste.push_back(vm.yeniString((*it).str(0)));
          }
          return vm.yeniListe(std::move(liste));
        } catch (const std::regex_error &ex) {
          throw std::runtime_error("regex.tum deseni gecersiz: " +
                                   std::string(ex.what()));
        }
      });
  regex->alanlar["degistir"] = nativeOlustur(
      "regex.degistir", 3, [](VM &vm, const std::vector<Value> &a) -> Value {
        try {
          const std::regex desen(vm.metneCevir(a[1]));
          return vm.yeniString(std::regex_replace(vm.metneCevir(a[0]), desen,
                                                  vm.metneCevir(a[2])));
        } catch (const std::regex_error &ex) {
          throw std::runtime_error("regex.degistir deseni gecersiz: " +
                                   std::string(ex.what()));
        }
      });
  globaller_["regex"] = Value::nesne(regex);

  auto *tarih = memory_.allocate<ObjDict>();
  tarih->alanlar["simdi"] = nativeOlustur(
      "tarih.simdi", 0, [](VM &vm, const std::vector<Value> &) -> Value {
        const std::time_t t = std::time(nullptr);
        return vm.yeniString(zamanBicimlendir("%Y-%m-%d %H:%M:%S", t));
      });
  tarih->alanlar["bugun"] = nativeOlustur(
      "tarih.bugun", 0, [](VM &vm, const std::vector<Value> &) -> Value {
        const std::time_t t = std::time(nullptr);
        return vm.yeniString(zamanBicimlendir("%Y-%m-%d", t));
      });
  tarih->alanlar["unix"] = nativeOlustur(
      "tarih.unix", 0, [](VM &, const std::vector<Value> &) -> Value {
        return Value::sayi(static_cast<double>(std::time(nullptr)));
      });
  globaller_["tarih"] = Value::nesne(tarih);

  auto *veritabani = memory_.allocate<ObjDict>();
  veritabani->alanlar["kaydet"] = nativeOlustur(
      "veritabani.kaydet", -1,
      [](VM &vm, const std::vector<Value> &a) -> Value {
        if (a.size() != 2 && a.size() != 3) {
          throw std::runtime_error("veritabani.kaydet(anahtar, deger, [dosya]) "
                                   "iki veya uc arguman alir.");
        }
        const std::string anahtar = vm.metneCevir(a[0]);
        const std::string yol =
            (a.size() == 3) ? vm.metneCevir(a[2]) : "_orhun_veritabani.json";

        std::unordered_map<std::string, Value> tablo;
        {
          std::ifstream in(yol, std::ios::binary);
          if (in.is_open()) {
            std::ostringstream ss;
            ss << in.rdbuf();
            const std::string icerik = ss.str();
            if (!icerik.empty()) {
              const yerlesik::JsonDeger kokJson = yerlesik::jsonCoz(icerik);
              Value kok = jsondanVmDegerine(vm, kokJson);
              if (kok.nesneMi() && kok.as.nesne != nullptr &&
                  kok.as.nesne->type == ObjType::DICT) {
                tablo = static_cast<ObjDict *>(kok.as.nesne)->alanlar;
              }
            }
          }
        }

        tablo[anahtar] = a[1];
        yerlesik::JsonDeger::Sozluk jsonSozluk;
        for (const auto &[k, v] : tablo) {
          jsonSozluk[k] = vmDegerindenJsona(v);
        }
        const std::string jsonMetin = yerlesik::jsonYaz(
            yerlesik::JsonDeger(std::move(jsonSozluk)), true, 2);
        std::ofstream out(yol, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
          throw std::runtime_error("veritabani.kaydet: yazilamadi: " + yol);
        }
        out << jsonMetin;
        return Value::mantik(true);
      });
  veritabani->alanlar["oku"] = nativeOlustur(
      "veritabani.oku", -1, [](VM &vm, const std::vector<Value> &a) -> Value {
        if (a.size() != 1 && a.size() != 2) {
          throw std::runtime_error(
              "veritabani.oku(anahtar, [dosya]) bir veya iki arguman alir.");
        }
        const std::string anahtar = vm.metneCevir(a[0]);
        const std::string yol =
            (a.size() == 2) ? vm.metneCevir(a[1]) : "_orhun_veritabani.json";

        std::ifstream in(yol, std::ios::binary);
        if (!in.is_open()) {
          throw std::runtime_error("veritabani.oku: dosya acilamadi: " + yol);
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        Value kok = jsondanVmDegerine(vm, yerlesik::jsonCoz(ss.str()));
        if (!kok.nesneMi() || kok.as.nesne == nullptr ||
            kok.as.nesne->type != ObjType::DICT) {
          throw std::runtime_error(
              "veritabani.oku: gecersiz veritabani formati.");
        }
        auto *sozluk = static_cast<ObjDict *>(kok.as.nesne);
        const auto it = sozluk->alanlar.find(anahtar);
        if (it == sozluk->alanlar.end()) {
          throw std::runtime_error("veritabani.oku: anahtar bulunamadi: " +
                                   anahtar);
        }
        return it->second;
      });
  veritabani->alanlar["listele"] = nativeOlustur(
      "veritabani.listele", -1,
      [](VM &vm, const std::vector<Value> &a) -> Value {
        if (a.size() > 1) {
          throw std::runtime_error(
              "veritabani.listele([dosya]) sifir veya bir arguman alir.");
        }
        const std::string yol =
            (a.size() == 1) ? vm.metneCevir(a[0]) : "_orhun_veritabani.json";
        std::ifstream in(yol, std::ios::binary);
        if (!in.is_open()) {
          return vm.yeniListe({});
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        Value kok = jsondanVmDegerine(vm, yerlesik::jsonCoz(ss.str()));
        if (!kok.nesneMi() || kok.as.nesne == nullptr ||
            kok.as.nesne->type != ObjType::DICT) {
          throw std::runtime_error(
              "veritabani.listele: gecersiz veritabani formati.");
        }
        auto *sozluk = static_cast<ObjDict *>(kok.as.nesne);
        std::vector<Value> anahtarlar;
        anahtarlar.reserve(sozluk->alanlar.size());
        for (const auto &[k, _] : sozluk->alanlar) {
          anahtarlar.push_back(vm.yeniString(k));
        }
        return vm.yeniListe(std::move(anahtarlar));
      });
  veritabani->alanlar["sil"] = nativeOlustur(
      "veritabani.sil", -1, [](VM &vm, const std::vector<Value> &a) -> Value {
        if (a.size() != 1 && a.size() != 2) {
          throw std::runtime_error(
              "veritabani.sil(anahtar, [dosya]) bir veya iki arguman alir.");
        }
        const std::string anahtar = vm.metneCevir(a[0]);
        const std::string yol =
            (a.size() == 2) ? vm.metneCevir(a[1]) : "_orhun_veritabani.json";

        std::unordered_map<std::string, Value> tablo;
        {
          std::ifstream in(yol, std::ios::binary);
          if (!in.is_open()) {
            return Value::mantik(false);
          }
          std::ostringstream ss;
          ss << in.rdbuf();
          Value kok = jsondanVmDegerine(vm, yerlesik::jsonCoz(ss.str()));
          if (kok.nesneMi() && kok.as.nesne != nullptr &&
              kok.as.nesne->type == ObjType::DICT) {
            tablo = static_cast<ObjDict *>(kok.as.nesne)->alanlar;
          }
        }

        const bool silindi = tablo.erase(anahtar) > 0;
        yerlesik::JsonDeger::Sozluk jsonSozluk;
        for (const auto &[k, v] : tablo) {
          jsonSozluk[k] = vmDegerindenJsona(v);
        }
        std::ofstream out(yol, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
          throw std::runtime_error("veritabani.sil: yazilamadi: " + yol);
        }
        out << yerlesik::jsonYaz(yerlesik::JsonDeger(std::move(jsonSozluk)),
                                 true, 2);
        return Value::mantik(silindi);
      });
  globaller_["veritabani"] = Value::nesne(veritabani);

  auto *sunucu = memory_.allocate<ObjDict>();
  sunucu->alanlar["baslat"] = nativeOlustur(
      "sunucu.baslat", -1, [](VM &vm, const std::vector<Value> &a) -> Value {
        if (a.empty() || a.size() > 2) {
          throw std::runtime_error(
              "sunucu.baslat(port, [\"klasor\"]) bir veya iki arguman alir.");
        }
        const double portDegeri = vm.sayiyaCevir(a[0], "sunucu.baslat");
        if (!tamSayiMi(portDegeri)) {
          throw std::runtime_error(
              "sunucu.baslat icin port tam sayi olmalidir.");
        }
        const int port = static_cast<int>(std::llround(portDegeri));
        std::string klasor = ".";
        if (a.size() == 2) {
          klasor = vm.metneCevir(a[1]);
        }
        std::string hata;
        if (!yerlesik::paylasimliHttpSunucu().baslat(port, klasor, &hata)) {
          throw std::runtime_error("sunucu.baslat basarisiz: " + hata);
        }
        return Value::mantik(true);
      });
  sunucu->alanlar["durdur"] = nativeOlustur(
      "sunucu.durdur", 0, [](VM &, const std::vector<Value> &) -> Value {
        yerlesik::paylasimliHttpSunucu().durdur();
        return Value::mantik(true);
      });
  sunucu->alanlar["calisiyor_mu"] = nativeOlustur(
      "sunucu.calisiyor_mu", 0, [](VM &, const std::vector<Value> &) -> Value {
        return Value::mantik(yerlesik::paylasimliHttpSunucu().calisiyorMu());
      });
  globaller_["sunucu"] = Value::nesne(sunucu);

  auto *sistem = memory_.allocate<ObjDict>();
  sistem->alanlar["komut"] = nativeOlustur(
      "sistem.komut", 1, [](VM &vm, const std::vector<Value> &a) -> Value {
        const std::string komut = vm.metneCevir(a[0]);
        if (!sistemKomutuKisitliModDisi() && !sistemKomutuGuvenliMi(komut)) {
          throw std::runtime_error(
              "sistem.komut kisitli modda tehlikeli karakter iceremez. "
              "Gerekiyorsa ORHUN_UNSAFE=1 ile acin.");
        }
        const int cikis = yerlesik::komutCalistirGuvenli(komut);
        if (cikis == -1) {
          throw std::runtime_error("sistem.komut calistirilamadi.");
        }
        return Value::sayi(static_cast<double>(cikis));
      });
  globaller_["sistem"] = Value::nesne(sistem);

  auto *sonuc = memory_.allocate<ObjDict>();
  sonuc->alanlar["ok"] =
      nativeOlustur("sonuc.ok", 1,
                    [](VM &vm, const std::vector<Value> &a) -> Value {
                      auto *kayit = vm.memory_.allocate<ObjDict>();
                      kayit->alanlar["ok"] = Value::mantik(true);
                      kayit->alanlar["deger"] = a[0];
                      kayit->alanlar["hata"] = Value::bos();
                      return Value::nesne(kayit);
                    });
  sonuc->alanlar["hata"] =
      nativeOlustur("sonuc.hata", 1,
                    [](VM &vm, const std::vector<Value> &a) -> Value {
                      auto *kayit = vm.memory_.allocate<ObjDict>();
                      kayit->alanlar["ok"] = Value::mantik(false);
                      kayit->alanlar["deger"] = Value::bos();
                      kayit->alanlar["hata"] = a[0];
                      return Value::nesne(kayit);
                    });
  globaller_["sonuc"] = Value::nesne(sonuc);

  auto *gorev = memory_.allocate<ObjDict>();
  gorev->alanlar["baslat_bekle"] = nativeOlustur(
      "gorev.baslat_bekle", 1,
      [](VM &vm, const std::vector<Value> &a) -> Value {
        const double saniye = vm.sayiyaCevir(a[0], "gorev.baslat_bekle");
        if (!std::isfinite(saniye) || saniye < 0.0) {
          throw std::runtime_error(
              "gorev.baslat_bekle icin sifirdan buyuk sayi beklenir.");
        }
        const int kimlik = vm.gorevSonrakiKimlik_++;
        VM::GorevKaydi kayit{
            std::async(std::launch::async, [saniye]() -> double {
              const auto ms = std::chrono::milliseconds(
                  static_cast<int>(std::llround(saniye * 1000.0)));
              if (ms.count() > 0) {
                std::this_thread::sleep_for(ms);
              }
              return 1.0;
            })};
        vm.gorevler_.emplace(kimlik, std::move(kayit));
        return Value::sayi(static_cast<double>(kimlik));
      });
  gorev->alanlar["baslat_komut"] = nativeOlustur(
      "gorev.baslat_komut", 1,
      [](VM &vm, const std::vector<Value> &a) -> Value {
        const std::string komut = vm.metneCevir(a[0]);
        if (!sistemKomutuKisitliModDisi() && !sistemKomutuGuvenliMi(komut)) {
          throw std::runtime_error(
              "gorev.baslat_komut kisitli modda tehlikeli karakter iceremez. "
              "Gerekiyorsa ORHUN_UNSAFE=1 ile acin.");
        }
        const int kimlik = vm.gorevSonrakiKimlik_++;
        VM::GorevKaydi kayit{
            std::async(std::launch::async, [komut]() -> double {
              const int cikis = yerlesik::komutCalistirGuvenli(komut);
              return static_cast<double>(cikis);
            })};
        vm.gorevler_.emplace(kimlik, std::move(kayit));
        return Value::sayi(static_cast<double>(kimlik));
      });
  gorev->alanlar["baslat_plan"] = nativeOlustur(
      "gorev.baslat_plan", 1,
      [](VM &vm, const std::vector<Value> &a) -> Value {
        if (!a[0].nesneMi() || a[0].as.nesne == nullptr ||
            a[0].as.nesne->type != ObjType::LIST) {
          throw std::runtime_error(
              "gorev.baslat_plan icin adim listesi beklenir.");
        }

        struct PlanAdimi {
          enum class Tip { Bekle, Komut };
          Tip tip = Tip::Bekle;
          double saniye = 0.0;
          std::string komut;
        };

        const auto *adimlarListe = static_cast<ObjList *>(a[0].as.nesne);
        std::vector<PlanAdimi> adimlar;
        adimlar.reserve(adimlarListe->ogeler.size());

        for (const Value &adimDegeri : adimlarListe->ogeler) {
          if (!adimDegeri.nesneMi() || adimDegeri.as.nesne == nullptr ||
              adimDegeri.as.nesne->type != ObjType::DICT) {
            throw std::runtime_error(
                "gorev.baslat_plan adimlari sozluk olmalidir.");
          }

          const auto *adim = static_cast<ObjDict *>(adimDegeri.as.nesne);
          const auto turIt = adim->alanlar.find("tur");
          const auto argIt = adim->alanlar.find("arg");
          if (turIt == adim->alanlar.end() || argIt == adim->alanlar.end()) {
            throw std::runtime_error(
                "gorev.baslat_plan adiminda 'tur' ve 'arg' alanlari "
                "zorunludur.");
          }
          if (!objTipiMi(turIt->second, ObjType::STRING)) {
            throw std::runtime_error(
                "gorev.baslat_plan adiminda 'tur' metin olmalidir.");
          }

          const std::string tur =
              static_cast<ObjString *>(turIt->second.as.nesne)->deger;
          if (tur == "bekle") {
            const double saniye =
                vm.sayiyaCevir(argIt->second, "gorev.baslat_plan.bekle");
            if (!std::isfinite(saniye) || saniye < 0.0) {
              throw std::runtime_error(
                  "gorev.baslat_plan/bekle icin sifirdan buyuk sayi "
                  "beklenir.");
            }
            PlanAdimi adimBekle;
            adimBekle.tip = PlanAdimi::Tip::Bekle;
            adimBekle.saniye = saniye;
            adimlar.push_back(std::move(adimBekle));
            continue;
          }

          if (tur == "komut") {
            const std::string komut = vm.metneCevir(argIt->second);
            if (!sistemKomutuKisitliModDisi() &&
                !sistemKomutuGuvenliMi(komut)) {
              throw std::runtime_error(
                  "gorev.baslat_plan/komut kisitli modda tehlikeli karakter "
                  "iceremez. Gerekiyorsa ORHUN_UNSAFE=1 ile acin.");
            }
            PlanAdimi adimKomut;
            adimKomut.tip = PlanAdimi::Tip::Komut;
            adimKomut.komut = komut;
            adimlar.push_back(std::move(adimKomut));
            continue;
          }

          throw std::runtime_error(
              "gorev.baslat_plan adiminda desteklenmeyen tur: " + tur);
        }

        if (adimlar.empty()) {
          throw std::runtime_error(
              "gorev.baslat_plan icin en az bir adim gereklidir.");
        }

        const int kimlik = vm.gorevSonrakiKimlik_++;
        VM::GorevKaydi kayit{
            std::async(std::launch::async,
                       [adimlar = std::move(adimlar)]() -> double {
                         double sonDeger = 1.0;
                         for (const PlanAdimi &adim : adimlar) {
                           if (adim.tip == PlanAdimi::Tip::Bekle) {
                             const auto ms = std::chrono::milliseconds(
                                 static_cast<int>(
                                     std::llround(adim.saniye * 1000.0)));
                             if (ms.count() > 0) {
                               std::this_thread::sleep_for(ms);
                             }
                             sonDeger = 1.0;
                             continue;
                           }

                           const int cikis =
                               yerlesik::komutCalistirGuvenli(adim.komut);
                           sonDeger = static_cast<double>(cikis);
                         }
                         return sonDeger;
                       })};
        vm.gorevler_.emplace(kimlik, std::move(kayit));
        return Value::sayi(static_cast<double>(kimlik));
      });
  gorev->alanlar["tamamlandi_mi"] = nativeOlustur(
      "gorev.tamamlandi_mi", 1,
      [](VM &vm, const std::vector<Value> &a) -> Value {
        const double d = vm.sayiyaCevir(a[0], "gorev.tamamlandi_mi");
        if (!tamSayiMi(d)) {
          throw std::runtime_error(
              "gorev.tamamlandi_mi icin gorev kimligi tam sayi olmalidir.");
        }
        const int kimlik = static_cast<int>(std::llround(d));
        const auto it = vm.gorevler_.find(kimlik);
        if (it == vm.gorevler_.end()) {
          return Value::mantik(false);
        }
        if (it->second.sonucHazir) {
          return Value::mantik(true);
        }
        if (it->second.future.valid() &&
            it->second.future.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
          it->second.sonuc = it->second.future.get();
          it->second.sonucHazir = true;
          return Value::mantik(true);
        }
        return Value::mantik(false);
      });
  gorev->alanlar["bekle"] = nativeOlustur(
      "gorev.bekle", 1, [](VM &vm, const std::vector<Value> &a) -> Value {
        const double d = vm.sayiyaCevir(a[0], "gorev.bekle");
        if (!tamSayiMi(d)) {
          throw std::runtime_error(
              "gorev.bekle icin gorev kimligi tam sayi olmalidir.");
        }
        const int kimlik = static_cast<int>(std::llround(d));
        const auto it = vm.gorevler_.find(kimlik);
        if (it == vm.gorevler_.end()) {
          throw std::runtime_error("gorev.bekle: gecersiz gorev kimligi.");
        }
        if (!it->second.sonucHazir) {
          if (!it->second.future.valid()) {
            throw std::runtime_error(
                "gorev.bekle: gorev sonucu kullanilamaz durumda.");
          }
          it->second.sonuc = it->second.future.get();
          it->second.sonucHazir = true;
        }
        return Value::sayi(it->second.sonuc);
      });
  gorev->alanlar["hepsi_bekle"] = nativeOlustur(
      "gorev.hepsi_bekle", 1, [](VM &vm, const std::vector<Value> &a) -> Value {
        if (!a[0].nesneMi() || a[0].as.nesne == nullptr ||
            a[0].as.nesne->type != ObjType::LIST) {
          throw std::runtime_error(
              "gorev.hepsi_bekle icin gorev kimlikleri listesi beklenir.");
        }
        const auto *liste = static_cast<ObjList *>(a[0].as.nesne);
        std::vector<Value> sonuc;
        sonuc.reserve(liste->ogeler.size());
        for (const Value &oge : liste->ogeler) {
          const double d = vm.sayiyaCevir(oge, "gorev.hepsi_bekle");
          if (!tamSayiMi(d)) {
            throw std::runtime_error("gorev.hepsi_bekle listesindeki tum "
                                     "kimlikler tam sayi olmalidir.");
          }
          const int kimlik = static_cast<int>(std::llround(d));
          const auto it = vm.gorevler_.find(kimlik);
          if (it == vm.gorevler_.end()) {
            throw std::runtime_error(
                "gorev.hepsi_bekle: gecersiz gorev kimligi.");
          }
          if (!it->second.sonucHazir) {
            if (!it->second.future.valid()) {
              throw std::runtime_error(
                  "gorev.hepsi_bekle: gorev sonucu kullanilamaz durumda.");
            }
            it->second.sonuc = it->second.future.get();
            it->second.sonucHazir = true;
          }
          sonuc.push_back(Value::sayi(it->second.sonuc));
        }
        return vm.yeniListe(std::move(sonuc));
      });
  globaller_["gorev"] = Value::nesne(gorev);

  auto *ffi = memory_.allocate<ObjDict>();
  ffi->alanlar["yukle"] = nativeOlustur(
      "ffi.yukle", 1, [](VM &vm, const std::vector<Value> &a) -> Value {
        if (a.size() != 1) {
          throw std::runtime_error(
              "ffi.yukle(\"kutuphane\") tek arguman alir.");
        }
        if (!objTipiMi(a[0], ObjType::STRING)) {
          throw std::runtime_error(
              "ffi.yukle icin metin kutuphane yolu bekleniyor.");
        }

        const std::string yol = static_cast<ObjString *>(a[0].as.nesne)->deger;
        ffiKutuphaneErisiminiDogrula(yol, "ffi.yukle");
        const auto mevcut = vm.ffiKutuphaneKimlikleri_.find(yol);
        if (mevcut != vm.ffiKutuphaneKimlikleri_.end()) {
          return Value::sayi(static_cast<double>(mevcut->second));
        }

        auto kutuphane = std::make_shared<runtime::DynamicLibrary>(yol);
        std::string hata;
        if (!kutuphane->load(&hata)) {
          throw std::runtime_error("ffi.yukle basarisiz: '" + yol + "' (" +
                                   hata + ")");
        }

        const int kimlik = vm.ffiSonrakiKimlik_++;
        vm.ffiKutuphaneleri_[kimlik] = std::move(kutuphane);
        vm.ffiKutuphaneKimlikleri_[yol] = kimlik;
        return Value::sayi(static_cast<double>(kimlik));
      });
  ffi->alanlar["cagir"] = nativeOlustur(
      "ffi.cagir", -1, [](VM &vm, const std::vector<Value> &a) -> Value {
        ffiKullaniminiDogrula("ffi.cagir");
        if (a.size() < 2) {
          throw std::runtime_error("ffi.cagir(tutamac, \"fonksiyon\", "
                                   "...argumanlar) en az 2 arguman alir.");
        }
        if (!objTipiMi(a[1], ObjType::STRING)) {
          throw std::runtime_error(
              "ffi.cagir icinde fonksiyon adi metin olmalidir.");
        }

        int kimlik = 0;
        if (a[0].sayiMi()) {
          if (!tamSayiMi(a[0].as.sayi)) {
            throw std::runtime_error(
                "ffi.cagir icin tutamac tam sayi olmalidir.");
          }
          kimlik = static_cast<int>(std::llround(a[0].as.sayi));
        } else if (objTipiMi(a[0], ObjType::STRING)) {
          const std::string yol =
              static_cast<ObjString *>(a[0].as.nesne)->deger;
          const auto mevcut = vm.ffiKutuphaneKimlikleri_.find(yol);
          if (mevcut != vm.ffiKutuphaneKimlikleri_.end()) {
            kimlik = mevcut->second;
          } else {
            ffiKutuphaneErisiminiDogrula(yol, "ffi.cagir");
            auto kutuphane = std::make_shared<runtime::DynamicLibrary>(yol);
            std::string hata;
            if (!kutuphane->load(&hata)) {
              throw std::runtime_error("ffi.cagir: '" + yol +
                                       "' kutuphanesi yuklenemedi (" + hata +
                                       ")");
            }
            kimlik = vm.ffiSonrakiKimlik_++;
            vm.ffiKutuphaneleri_[kimlik] = std::move(kutuphane);
            vm.ffiKutuphaneKimlikleri_[yol] = kimlik;
          }
        } else {
          throw std::runtime_error("ffi.cagir icin ilk arguman tutamac(int) "
                                   "veya kutuphane adi(metin) olmalidir.");
        }

        const auto itKutuphane = vm.ffiKutuphaneleri_.find(kimlik);
        if (itKutuphane == vm.ffiKutuphaneleri_.end() || !itKutuphane->second ||
            !itKutuphane->second->isLoaded()) {
          throw std::runtime_error("ffi.cagir: gecersiz kutuphane tutamaci #" +
                                   std::to_string(kimlik));
        }

        const std::string fonksiyonAdi =
            static_cast<ObjString *>(a[1].as.nesne)->deger;
        std::string sembolHatasi;
        const std::uintptr_t fonksiyon =
            itKutuphane->second->getSymbol(fonksiyonAdi, &sembolHatasi);
        if (fonksiyon == 0) {
          throw std::runtime_error("ffi.cagir: '" + fonksiyonAdi +
                                   "' sembolu bulunamadi (" + sembolHatasi +
                                   ")");
        }

        std::vector<std::string> metinSahipligi;
        std::vector<std::intptr_t> hamArgumanlar;
        metinSahipligi.reserve(a.size() > 2 ? a.size() - 2 : 0);
        hamArgumanlar.reserve(a.size() > 2 ? a.size() - 2 : 0);

        for (std::size_t i = 2; i < a.size(); ++i) {
          if (a[i].sayiMi()) {
            if (!tamSayiMi(a[i].as.sayi)) {
              throw std::runtime_error(
                  "ffi.cagir ondalik argumanlari desteklemiyor (arg #" +
                  std::to_string(i - 1) + ").");
            }
            hamArgumanlar.push_back(
                static_cast<std::intptr_t>(std::llround(a[i].as.sayi)));
            continue;
          }
          if (a[i].mantikMi()) {
            hamArgumanlar.push_back(a[i].as.mantik ? 1 : 0);
            continue;
          }
          if (objTipiMi(a[i], ObjType::STRING)) {
            metinSahipligi.push_back(
                static_cast<ObjString *>(a[i].as.nesne)->deger);
            hamArgumanlar.push_back(
                reinterpret_cast<std::intptr_t>(metinSahipligi.back().c_str()));
            continue;
          }
          throw std::runtime_error(
              "ffi.cagir sadece int/bool/metin argumanlarini destekliyor.");
        }

        const std::intptr_t donus = ffiHamCagir(fonksiyon, hamArgumanlar);
        return Value::sayi(static_cast<double>(donus));
      });
  ffi->alanlar["cagir_metin"] = nativeOlustur(
      "ffi.cagir_metin", -1, [](VM &vm, const std::vector<Value> &a) -> Value {
        ffiKullaniminiDogrula("ffi.cagir_metin");
        if (a.size() < 2) {
          throw std::runtime_error("ffi.cagir_metin(tutamac, \"fonksiyon\", "
                                   "...argumanlar) en az 2 arguman alir.");
        }
        if (!objTipiMi(a[1], ObjType::STRING)) {
          throw std::runtime_error(
              "ffi.cagir_metin icinde fonksiyon adi metin olmalidir.");
        }

        int kimlik = 0;
        if (a[0].sayiMi()) {
          if (!tamSayiMi(a[0].as.sayi)) {
            throw std::runtime_error(
                "ffi.cagir_metin icin tutamac tam sayi olmalidir.");
          }
          kimlik = static_cast<int>(std::llround(a[0].as.sayi));
        } else if (objTipiMi(a[0], ObjType::STRING)) {
          const std::string yol =
              static_cast<ObjString *>(a[0].as.nesne)->deger;
          const auto mevcut = vm.ffiKutuphaneKimlikleri_.find(yol);
          if (mevcut != vm.ffiKutuphaneKimlikleri_.end()) {
            kimlik = mevcut->second;
          } else {
            ffiKutuphaneErisiminiDogrula(yol, "ffi.cagir_metin");
            auto kutuphane = std::make_shared<runtime::DynamicLibrary>(yol);
            std::string hata;
            if (!kutuphane->load(&hata)) {
              throw std::runtime_error("ffi.cagir_metin: '" + yol +
                                       "' kutuphanesi yuklenemedi (" + hata +
                                       ")");
            }
            kimlik = vm.ffiSonrakiKimlik_++;
            vm.ffiKutuphaneleri_[kimlik] = std::move(kutuphane);
            vm.ffiKutuphaneKimlikleri_[yol] = kimlik;
          }
        } else {
          throw std::runtime_error(
              "ffi.cagir_metin icin ilk arguman tutamac(int) veya kutuphane "
              "adi(metin) olmalidir.");
        }

        const auto itKutuphane = vm.ffiKutuphaneleri_.find(kimlik);
        if (itKutuphane == vm.ffiKutuphaneleri_.end() || !itKutuphane->second ||
            !itKutuphane->second->isLoaded()) {
          throw std::runtime_error(
              "ffi.cagir_metin: gecersiz kutuphane tutamaci #" +
              std::to_string(kimlik));
        }

        const std::string fonksiyonAdi =
            static_cast<ObjString *>(a[1].as.nesne)->deger;
        std::string sembolHatasi;
        const std::uintptr_t fonksiyon =
            itKutuphane->second->getSymbol(fonksiyonAdi, &sembolHatasi);
        if (fonksiyon == 0) {
          throw std::runtime_error("ffi.cagir_metin: '" + fonksiyonAdi +
                                   "' sembolu bulunamadi (" + sembolHatasi +
                                   ")");
        }

        std::vector<std::string> metinSahipligi;
        std::vector<std::intptr_t> hamArgumanlar;
        metinSahipligi.reserve(a.size() > 2 ? a.size() - 2 : 0);
        hamArgumanlar.reserve(a.size() > 2 ? a.size() - 2 : 0);

        for (std::size_t i = 2; i < a.size(); ++i) {
          if (a[i].sayiMi()) {
            if (!tamSayiMi(a[i].as.sayi)) {
              throw std::runtime_error(
                  "ffi.cagir_metin ondalik argumanlari desteklemiyor (arg #" +
                  std::to_string(i - 1) + ").");
            }
            hamArgumanlar.push_back(
                static_cast<std::intptr_t>(std::llround(a[i].as.sayi)));
            continue;
          }
          if (a[i].mantikMi()) {
            hamArgumanlar.push_back(a[i].as.mantik ? 1 : 0);
            continue;
          }
          if (objTipiMi(a[i], ObjType::STRING)) {
            metinSahipligi.push_back(
                static_cast<ObjString *>(a[i].as.nesne)->deger);
            hamArgumanlar.push_back(
                reinterpret_cast<std::intptr_t>(metinSahipligi.back().c_str()));
            continue;
          }
          throw std::runtime_error("ffi.cagir_metin sadece int/bool/metin "
                                   "argumanlarini destekliyor.");
        }

        const std::intptr_t donus = ffiHamCagir(fonksiyon, hamArgumanlar);
        if (donus == 0) {
          return vm.yeniString("");
        }
        return vm.yeniString(
            std::string(reinterpret_cast<const char *>(donus)));
      });
  ffi->alanlar["sembol_var_mi"] = nativeOlustur(
      "ffi.sembol_var_mi", 2, [](VM &vm, const std::vector<Value> &a) -> Value {
        ffiKullaniminiDogrula("ffi.sembol_var_mi");
        int kimlik = 0;
        if (a[0].sayiMi()) {
          if (!tamSayiMi(a[0].as.sayi)) {
            return Value::mantik(false);
          }
          kimlik = static_cast<int>(std::llround(a[0].as.sayi));
        } else if (objTipiMi(a[0], ObjType::STRING)) {
          const std::string yol =
              static_cast<ObjString *>(a[0].as.nesne)->deger;
          const auto itYol = vm.ffiKutuphaneKimlikleri_.find(yol);
          if (itYol == vm.ffiKutuphaneKimlikleri_.end()) {
            return Value::mantik(false);
          }
          kimlik = itYol->second;
        } else {
          return Value::mantik(false);
        }
        if (!objTipiMi(a[1], ObjType::STRING)) {
          return Value::mantik(false);
        }
        const auto itKutuphane = vm.ffiKutuphaneleri_.find(kimlik);
        if (itKutuphane == vm.ffiKutuphaneleri_.end() || !itKutuphane->second ||
            !itKutuphane->second->isLoaded()) {
          return Value::mantik(false);
        }
        const std::string sembol =
            static_cast<ObjString *>(a[1].as.nesne)->deger;
        return Value::mantik(itKutuphane->second->getSymbol(sembol, nullptr) !=
                             0);
      });
  ffi->alanlar["bosalt"] = nativeOlustur(
      "ffi.bosalt", 1, [](VM &vm, const std::vector<Value> &a) -> Value {
        int kimlik = 0;
        if (a[0].sayiMi()) {
          if (!tamSayiMi(a[0].as.sayi)) {
            return Value::mantik(false);
          }
          kimlik = static_cast<int>(std::llround(a[0].as.sayi));
        } else {
          return Value::mantik(false);
        }
        const auto it = vm.ffiKutuphaneleri_.find(kimlik);
        if (it == vm.ffiKutuphaneleri_.end()) {
          return Value::mantik(false);
        }
        if (it->second) {
          it->second->close();
        }
        vm.ffiKutuphaneleri_.erase(it);
        for (auto itYol = vm.ffiKutuphaneKimlikleri_.begin();
             itYol != vm.ffiKutuphaneKimlikleri_.end();) {
          if (itYol->second == kimlik) {
            itYol = vm.ffiKutuphaneKimlikleri_.erase(itYol);
          } else {
            ++itYol;
          }
        }
        return Value::mantik(true);
      });
  ffi->alanlar["tanimla"] = nativeOlustur(
      "ffi.tanimla", -1, [](VM &vm, const std::vector<Value> &a) -> Value {
        ffiKullaniminiDogrula("ffi.tanimla");
        if (a.size() < 3 || a.size() > 4) {
          throw std::runtime_error(
              "ffi.tanimla(kutuphane, \"sembol\", \"donus\", [argTipleri]) 3 "
              "veya 4 arguman alir.");
        }
        if (!objTipiMi(a[1], ObjType::STRING) ||
            !objTipiMi(a[2], ObjType::STRING)) {
          throw std::runtime_error(
              "ffi.tanimla ikinci ve ucuncu argumanlarda metin bekler.");
        }

        int kimlik = 0;
        if (a[0].sayiMi()) {
          if (!tamSayiMi(a[0].as.sayi)) {
            throw std::runtime_error(
                "ffi.tanimla icin kutuphane tutamaci tam sayi olmalidir.");
          }
          kimlik = static_cast<int>(std::llround(a[0].as.sayi));
        } else if (objTipiMi(a[0], ObjType::STRING)) {
          const std::string yol =
              static_cast<ObjString *>(a[0].as.nesne)->deger;
          const auto itYol = vm.ffiKutuphaneKimlikleri_.find(yol);
          if (itYol != vm.ffiKutuphaneKimlikleri_.end()) {
            kimlik = itYol->second;
          } else {
            ffiKutuphaneErisiminiDogrula(yol, "ffi.tanimla");
            auto kutuphane = std::make_shared<runtime::DynamicLibrary>(yol);
            std::string hata;
            if (!kutuphane->load(&hata)) {
              throw std::runtime_error("ffi.tanimla: '" + yol +
                                       "' kutuphanesi yuklenemedi (" + hata +
                                       ")");
            }
            kimlik = vm.ffiSonrakiKimlik_++;
            vm.ffiKutuphaneleri_[kimlik] = std::move(kutuphane);
            vm.ffiKutuphaneKimlikleri_[yol] = kimlik;
          }
        } else {
          throw std::runtime_error("ffi.tanimla icin ilk arguman tutamac(int) "
                                   "veya kutuphane adi(metin) olmalidir.");
        }

        const auto itKutuphane = vm.ffiKutuphaneleri_.find(kimlik);
        if (itKutuphane == vm.ffiKutuphaneleri_.end() || !itKutuphane->second ||
            !itKutuphane->second->isLoaded()) {
          throw std::runtime_error(
              "ffi.tanimla: gecersiz kutuphane tutamaci #" +
              std::to_string(kimlik));
        }

        auto tipCoz = [](const std::string &metin) -> VM::FFIType {
          const std::string tip = asciiKucult(metin);
          if (tip == "void" || tip == "bos") {
            return VM::FFIType::NONE;
          }
          if (tip == "int" || tip == "tam" || tip == "int64") {
            return VM::FFIType::INT64;
          }
          if (tip == "double" || tip == "ondalik" || tip == "sayi") {
            return VM::FFIType::DOUBLE;
          }
          if (tip == "string" || tip == "metin" || tip == "str") {
            return VM::FFIType::STRING;
          }
          if (tip == "pointer" || tip == "isaretci" || tip == "ptr") {
            return VM::FFIType::POINTER;
          }
          throw std::runtime_error("ffi.tanimla: taninmayan tip '" + metin +
                                   "'.");
        };

        VM::FFISignature imza;
        imza.sembolAdi = static_cast<ObjString *>(a[1].as.nesne)->deger;
        imza.donusTipi = tipCoz(static_cast<ObjString *>(a[2].as.nesne)->deger);

        if (a.size() == 4) {
          if (!a[3].nesneMi() || a[3].as.nesne == nullptr ||
              a[3].as.nesne->type != ObjType::LIST) {
            throw std::runtime_error(
                "ffi.tanimla dorduncu argumanda tip listesi bekler.");
          }
          const auto *tipler = static_cast<ObjList *>(a[3].as.nesne);
          for (const Value &tipDegeri : tipler->ogeler) {
            if (!objTipiMi(tipDegeri, ObjType::STRING)) {
              throw std::runtime_error(
                  "ffi.tanimla tip listesi metinlerden olusmalidir.");
            }
            imza.argumanTipleri.push_back(
                tipCoz(static_cast<ObjString *>(tipDegeri.as.nesne)->deger));
          }
        }

        const int islevKimligi = vm.ffiSonrakiIslevKimlik_++;
        vm.ffiIslevBaglantilari_[islevKimligi] =
            VM::FFIBinding{kimlik, std::move(imza)};
        return Value::sayi(static_cast<double>(islevKimligi));
      });
  ffi->alanlar["cagir_tanimli"] = nativeOlustur(
      "ffi.cagir_tanimli", -1,
      [](VM &vm, const std::vector<Value> &a) -> Value {
        ffiKullaniminiDogrula("ffi.cagir_tanimli");
        if (a.empty()) {
          throw std::runtime_error("ffi.cagir_tanimli(islevKimligi, "
                                   "...argumanlar) en az 1 arguman alir.");
        }
        if (!a[0].sayiMi() || !tamSayiMi(a[0].as.sayi)) {
          throw std::runtime_error("ffi.cagir_tanimli icin ilk arguman islev "
                                   "kimligi tam sayi olmalidir.");
        }
        const int islevKimligi = static_cast<int>(std::llround(a[0].as.sayi));
        const auto itBaglanti = vm.ffiIslevBaglantilari_.find(islevKimligi);
        if (itBaglanti == vm.ffiIslevBaglantilari_.end()) {
          throw std::runtime_error(
              "ffi.cagir_tanimli: gecersiz islev kimligi #" +
              std::to_string(islevKimligi));
        }
        const VM::FFIBinding &baglanti = itBaglanti->second;
        const auto itKutuphane =
            vm.ffiKutuphaneleri_.find(baglanti.kutuphaneKimligi);
        if (itKutuphane == vm.ffiKutuphaneleri_.end() || !itKutuphane->second ||
            !itKutuphane->second->isLoaded()) {
          throw std::runtime_error(
              "ffi.cagir_tanimli: kutuphane tutamaci gecersiz.");
        }

        const std::size_t beklenen = baglanti.imza.argumanTipleri.size();
        const std::size_t gelen = a.size() - 1;
        if (beklenen != gelen) {
          throw std::runtime_error(
              "ffi.cagir_tanimli: beklenen arguman sayisi " +
              std::to_string(beklenen) + ", gelen " + std::to_string(gelen) +
              ".");
        }

        std::string sembolHatasi;
        const std::uintptr_t fonksiyon = itKutuphane->second->getSymbol(
            baglanti.imza.sembolAdi, &sembolHatasi);
        if (fonksiyon == 0) {
          throw std::runtime_error(
              "ffi.cagir_tanimli: '" + baglanti.imza.sembolAdi +
              "' sembolu bulunamadi (" + sembolHatasi + ")");
        }

        const bool tumArgumanlarCift = std::all_of(
            baglanti.imza.argumanTipleri.begin(),
            baglanti.imza.argumanTipleri.end(),
            [](VM::FFIType tip) { return tip == VM::FFIType::DOUBLE; });

        if (baglanti.imza.donusTipi == VM::FFIType::DOUBLE) {
          if (!tumArgumanlarCift) {
            throw std::runtime_error("ffi.cagir_tanimli: DOUBLE donus icin tum "
                                     "arguman tipleri DOUBLE olmalidir.");
          }
          std::vector<double> ciftArgumanlar;
          ciftArgumanlar.reserve(gelen);
          for (std::size_t i = 0; i < gelen; ++i) {
            ciftArgumanlar.push_back(
                vm.sayiyaCevir(a[i + 1], "ffi.cagir_tanimli"));
          }
          return Value::sayi(ffiCiftCagir(fonksiyon, ciftArgumanlar));
        }

        std::vector<std::intptr_t> hamArgumanlar;
        std::vector<std::string> metinSahipligi;
        hamArgumanlar.reserve(gelen);
        metinSahipligi.reserve(gelen);

        for (std::size_t i = 0; i < gelen; ++i) {
          const VM::FFIType tip = baglanti.imza.argumanTipleri[i];
          const Value &arg = a[i + 1];
          switch (tip) {
          case VM::FFIType::INT64:
          case VM::FFIType::POINTER: {
            const double d = vm.sayiyaCevir(arg, "ffi.cagir_tanimli");
            if (!tamSayiMi(d)) {
              throw std::runtime_error("ffi.cagir_tanimli: #" +
                                       std::to_string(i + 1) +
                                       " argumani tam sayi/pointer olmalidir.");
            }
            hamArgumanlar.push_back(
                static_cast<std::intptr_t>(std::llround(d)));
            break;
          }
          case VM::FFIType::STRING: {
            if (!objTipiMi(arg, ObjType::STRING)) {
              throw std::runtime_error("ffi.cagir_tanimli: #" +
                                       std::to_string(i + 1) +
                                       " argumani metin olmalidir.");
            }
            metinSahipligi.push_back(
                static_cast<ObjString *>(arg.as.nesne)->deger);
            hamArgumanlar.push_back(
                reinterpret_cast<std::intptr_t>(metinSahipligi.back().c_str()));
            break;
          }
          case VM::FFIType::DOUBLE:
            throw std::runtime_error("ffi.cagir_tanimli: DOUBLE argumanlari "
                                     "icin tum imza DOUBLE olmali.");
          case VM::FFIType::NONE:
            throw std::runtime_error(
                "ffi.cagir_tanimli: VOID arguman tipi gecersiz.");
          }
        }

        const std::intptr_t donus = ffiHamCagir(fonksiyon, hamArgumanlar);
        switch (baglanti.imza.donusTipi) {
        case VM::FFIType::NONE:
          return Value::sayi(0.0);
        case VM::FFIType::INT64:
        case VM::FFIType::POINTER:
          return Value::sayi(static_cast<double>(donus));
        case VM::FFIType::STRING:
          if (donus == 0) {
            return vm.yeniString("");
          }
          return vm.yeniString(
              std::string(reinterpret_cast<const char *>(donus)));
        case VM::FFIType::DOUBLE:
          break;
        }
        throw std::runtime_error("ffi.cagir_tanimli: gecersiz donus tipi.");
      });
  globaller_["ffi"] = Value::nesne(ffi);

  ekleNative(
      "ffi_dis_islev_tanimla", 4,
      [](VM &vm, const std::vector<Value> &a) -> Value {
        ffiKullaniminiDogrula("ffi_dis_islev_tanimla");
        if (a.size() != 4) {
          throw std::runtime_error("ffi_dis_islev_tanimla(ad, kutuphane, "
                                   "donus, argTipleri) 4 arguman alir.");
        }
        if (!objTipiMi(a[0], ObjType::STRING) ||
            !objTipiMi(a[1], ObjType::STRING) ||
            !objTipiMi(a[2], ObjType::STRING)) {
          throw std::runtime_error("ffi_dis_islev_tanimla: ad/kutuphane/donus "
                                   "tipleri metin olmalidir.");
        }
        if (!a[3].nesneMi() || a[3].as.nesne == nullptr ||
            a[3].as.nesne->type != ObjType::LIST) {
          throw std::runtime_error(
              "ffi_dis_islev_tanimla: argTipleri liste olmalidir.");
        }

        const std::string ad = static_cast<ObjString *>(a[0].as.nesne)->deger;
        const std::string kutuphaneYolu =
            static_cast<ObjString *>(a[1].as.nesne)->deger;
        const std::string donusTipi =
            static_cast<ObjString *>(a[2].as.nesne)->deger;
        const auto *argTipleri = static_cast<ObjList *>(a[3].as.nesne);

        int kutuphaneKimligi = 0;
        const auto mevcut = vm.ffiKutuphaneKimlikleri_.find(kutuphaneYolu);
        if (mevcut != vm.ffiKutuphaneKimlikleri_.end()) {
          kutuphaneKimligi = mevcut->second;
        } else {
          ffiKutuphaneErisiminiDogrula(kutuphaneYolu,
                                       "ffi_dis_islev_tanimla");
          std::string hata;
          auto kutuphane =
              std::make_shared<runtime::DynamicLibrary>(kutuphaneYolu);
          if (!kutuphane->load(&hata)) {
            throw std::runtime_error(
                "ffi_dis_islev_tanimla: kutuphane yuklenemedi: '" +
                kutuphaneYolu + "' (" + hata + ")");
          }
          kutuphaneKimligi = vm.ffiSonrakiKimlik_++;
          vm.ffiKutuphaneleri_[kutuphaneKimligi] = std::move(kutuphane);
          vm.ffiKutuphaneKimlikleri_[kutuphaneYolu] = kutuphaneKimligi;
        }

        auto tipCoz = [](const std::string &tip) -> VM::FFIType {
          std::string t = tip;
          std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
          });
          if (t == "int" || t == "tam" || t == "tamsayi" || t == "i64") {
            return VM::FFIType::INT64;
          }
          if (t == "double" || t == "ondalik" || t == "sayi") {
            return VM::FFIType::DOUBLE;
          }
          if (t == "metin" || t == "string") {
            return VM::FFIType::STRING;
          }
          if (t == "pointer" || t == "ptr") {
            return VM::FFIType::POINTER;
          }
          if (t == "void" || t == "bos") {
            return VM::FFIType::NONE;
          }
          throw std::runtime_error("ffi_dis_islev_tanimla: taninmayan tip '" +
                                   tip + "'.");
        };

        VM::FFISignature imza;
        imza.sembolAdi = ad;
        imza.donusTipi = tipCoz(donusTipi);
        imza.argumanTipleri.reserve(argTipleri->ogeler.size());
        for (const Value &tipDegeri : argTipleri->ogeler) {
          if (!objTipiMi(tipDegeri, ObjType::STRING)) {
            throw std::runtime_error("ffi_dis_islev_tanimla: argTipleri "
                                     "yalnizca metinlerden olusmali.");
          }
          imza.argumanTipleri.push_back(
              tipCoz(static_cast<ObjString *>(tipDegeri.as.nesne)->deger));
        }

        const auto itKutuphane = vm.ffiKutuphaneleri_.find(kutuphaneKimligi);
        if (itKutuphane == vm.ffiKutuphaneleri_.end() || !itKutuphane->second ||
            !itKutuphane->second->isLoaded()) {
          throw std::runtime_error(
              "ffi_dis_islev_tanimla: gecersiz kutuphane.");
        }
        std::string sembolHatasi;
        if (itKutuphane->second->getSymbol(imza.sembolAdi, &sembolHatasi) ==
            0) {
          throw std::runtime_error("ffi_dis_islev_tanimla: '" + imza.sembolAdi +
                                   "' sembolu bulunamadi (" + sembolHatasi +
                                   ").");
        }

        const int islevKimligi = vm.ffiSonrakiIslevKimlik_++;
        vm.ffiIslevBaglantilari_[islevKimligi] =
            VM::FFIBinding{kutuphaneKimligi, std::move(imza)};

        vm.globaller_[ad] = vm.nativeOlustur(
            ad, static_cast<int>(argTipleri->ogeler.size()),
            [islevKimligi](VM &inner,
                           const std::vector<Value> &argumanlar) -> Value {
              std::vector<Value> paketli;
              paketli.reserve(argumanlar.size() + 1);
              paketli.push_back(Value::sayi(static_cast<double>(islevKimligi)));
              paketli.insert(paketli.end(), argumanlar.begin(),
                             argumanlar.end());

              auto itFfi = inner.globaller_.find("ffi");
              if (itFfi == inner.globaller_.end() || !itFfi->second.nesneMi() ||
                  itFfi->second.as.nesne == nullptr ||
                  itFfi->second.as.nesne->type != ObjType::DICT) {
                throw std::runtime_error(
                    "ffi_dis_islev_tanimla: ffi modulu bulunamadi.");
              }
              auto *ffiModul = static_cast<ObjDict *>(itFfi->second.as.nesne);
              auto itCagir = ffiModul->alanlar.find("cagir_tanimli");
              if (itCagir == ffiModul->alanlar.end()) {
                throw std::runtime_error(
                    "ffi_dis_islev_tanimla: ffi.cagir_tanimli yok.");
              }
              return inner.cagir(itCagir->second, paketli);
            });
        return Value::mantik(true);
      });

  ekleNative("dahil_et", 1,
             [](VM &vm, const std::vector<Value> &args) -> Value {
               const std::string yol = vm.metneCevir(args[0]);
               const auto cozulmusYol = orhunDahilYolunuCoz(yol);
               if (!cozulmusYol.has_value()) {
                 throw std::runtime_error("dahil_et: dosya acilamadi: " + yol +
                                          orhunDahilAramaYollariMetni());
               }

               std::ifstream in(*cozulmusYol, std::ios::binary);
               if (!in.is_open()) {
                 throw std::runtime_error("dahil_et: dosya acilamadi: " + yol);
               }
               std::ostringstream ss;
               ss << in.rdbuf();
               const std::string kaynak = ss.str();

               Lexer lexer(kaynak);
               std::vector<OrhunToken> tokenlar = lexer.tokenize();
               Parser parser(std::move(tokenlar));
               std::unique_ptr<ProgramNode> program = parser.parse();
               Compiler derleyici;
               auto modulChunk =
                   std::make_unique<BytecodeChunk>(derleyici.derle(program.get()));
               BytecodeChunk &chunk = *modulChunk;

               const auto oncekiGloballer = vm.globaller_;

               const BytecodeChunk *kayitChunk = vm.chunk_;
               auto kayitFrameYigini = std::move(vm.frameYigini_);
               auto kayitBekleyenKurucular = std::move(vm.bekleyenKurucular_);
               auto kayitTryYigini = std::move(vm.tryYigini_);
               auto kayitYigin = std::move(vm.yigin_);
               auto kayitGlobalInlineCache = std::move(vm.globalInlineCache_);
               auto kayitRuntimeSabitCache = std::move(vm.runtimeSabitCache_);
               const std::size_t kayitGcEsigi = vm.gcEsigi_;

               // Ic ice calistirmada dis frame/stack degerleri gecici olarak
               // kenara alinir; modul bitene kadar GC bu kokleri goremez.
               ++vm.gcErtelemeDerinligi_;
               vm.gcEsigi_ = std::numeric_limits<std::size_t>::max() / 2;
               try {
                 vm.calistir(chunk);
               } catch (...) {
                 --vm.gcErtelemeDerinligi_;
                 vm.chunk_ = kayitChunk;
                 vm.frameYigini_ = std::move(kayitFrameYigini);
                 vm.bekleyenKurucular_ = std::move(kayitBekleyenKurucular);
                 vm.tryYigini_ = std::move(kayitTryYigini);
                 vm.yigin_ = std::move(kayitYigin);
                 vm.globalInlineCache_ = std::move(kayitGlobalInlineCache);
                 vm.runtimeSabitCache_ = std::move(kayitRuntimeSabitCache);
                 vm.gcEsigi_ = kayitGcEsigi;
                 throw;
               }
               --vm.gcErtelemeDerinligi_;
               vm.chunk_ = kayitChunk;
               vm.frameYigini_ = std::move(kayitFrameYigini);
               vm.bekleyenKurucular_ = std::move(kayitBekleyenKurucular);
               vm.tryYigini_ = std::move(kayitTryYigini);
               vm.yigin_ = std::move(kayitYigin);
               vm.globalInlineCache_ = std::move(kayitGlobalInlineCache);
               vm.runtimeSabitCache_ = std::move(kayitRuntimeSabitCache);
               vm.gcEsigi_ = kayitGcEsigi;

               auto *modul = vm.memory_.allocate<ObjDict>();
               for (const auto &[ad, deger] : vm.globaller_) {
                 const auto onceki = oncekiGloballer.find(ad);
                 if (onceki != oncekiGloballer.end() &&
                     vm.esitMi(onceki->second, deger)) {
                   continue;
                 }
                 modul->alanlar[ad] = deger;
               }

               vm.modulChunklari_.push_back(std::move(modulChunk));
               return Value::nesne(modul);
             });
}

void VM::gcGerekirseCalistir() {
  if (gcErtelemeDerinligi_ > 0) {
    return;
  }
  if (memory_.objectCount() >= gcEsigi_) {
    gcCalistir();
    gcEsigi_ = std::max<std::size_t>(1024, memory_.objectCount() * 2);
  }
}

void VM::gcCalistir() {
  memory_.collectGarbage([this](MemoryManager &mem) {
    for (const Value &deger : yigin_) {
      mem.markValue(deger);
    }
    for (const auto &[ad, deger] : globaller_) {
      (void)ad;
      mem.markValue(deger);
    }
    for (const CallFrame &frame : frameYigini_) {
      mem.markObject(frame.function);
      for (const auto &[indeks, hucre] : frame.localHucreleri) {
        (void)indeks;
        if (hucre) {
          mem.markValue(*hucre);
        }
      }
    }
    for (const BekleyenKurucu &b : bekleyenKurucular_) {
      mem.markValue(b.olusanNesne);
    }
    for (const RuntimeSabitCacheKaydi &kayit : runtimeSabitCache_) {
      if (kayit.hazir) {
        mem.markValue(kayit.deger);
      }
    }
  });
}

void VM::pushFrame(ObjFunction *fn, std::size_t slotBase,
                   std::size_t callBase) {
  CallFrame frame;
  frame.function = fn;
  frame.ip = fn->girisIp;
  frame.slotBase = slotBase;
  frame.callBase = callBase;
  frameYigini_.push_back(frame);
}

void VM::popFrameVeDon(Value donusDegeri) {
  if (frameYigini_.empty()) {
    calismaHatasi("Ic hata: frame yigini bos.");
  }
  const CallFrame frame = frameYigini_.back();
  frameYigini_.pop_back();

  while (!tryYigini_.empty() &&
         tryYigini_.back().frameDerinligi > frameYigini_.size()) {
    tryYigini_.pop_back();
  }

  if (frameYigini_.empty()) {
    yigin_.clear();
    return;
  }

  const std::size_t cagribaslangic = frame.callBase;
  if (cagribaslangic > yigin_.size()) {
    calismaHatasi("Ic hata: gecersiz call stack temizligi.");
  }
  yigin_.resize(cagribaslangic);
  yiginPush(donusDegeri);
}

void VM::islevCagrisiHazirla(std::size_t calleeIndex, std::uint16_t argc,
                             ObjFunction *fn) {
  islevCagrisiHazirla(calleeIndex, argc, fn, calleeIndex + 1);
}

void VM::islevCagrisiHazirla(std::size_t calleeIndex, std::uint16_t argc,
                             ObjFunction *fn, std::size_t slotBase) {
  const int gelen = static_cast<int>(argc);
  if (gelen < fn->minArity || gelen > fn->maxArity) {
    calismaHatasi("Arguman sayisi uyusmuyor: '" + fn->ad + "' (beklenen " +
                  std::to_string(fn->minArity) + "-" +
                  std::to_string(fn->maxArity) + ").");
  }
  const std::size_t hedefBoyut = slotBase + fn->localSayisi;
  while (yigin_.size() < hedefBoyut) {
    yiginPush(Value::bos());
  }
  pushFrame(fn, slotBase, calleeIndex);
}

ObjClass *VM::metodUstSinifBaglamiBul(ObjInstance *alici,
                                      ObjFunction *fn) const {
  if (fn != nullptr) {
    const std::size_t ayirici = fn->ad.find('.');
    if (ayirici != std::string::npos && ayirici > 0) {
      const std::string sinifAdi = fn->ad.substr(0, ayirici);
      if (auto it = globaller_.find(sinifAdi); it != globaller_.end() &&
                                             objTipiMi(it->second,
                                                       ObjType::CLASS)) {
        return static_cast<ObjClass *>(it->second.as.nesne)->ebeveyn;
      }
    }
  }
  if (alici != nullptr && alici->sinif != nullptr) {
    return alici->sinif->ebeveyn;
  }
  return nullptr;
}

void VM::calistir(const BytecodeChunk &chunk) {
  chunk_ = &chunk;
  yigin_.clear();
  frameYigini_.clear();
  bekleyenKurucular_.clear();
  tryYigini_.clear();
  globalInlineCache_.assign(chunk.sabitler.size(), GlobalInlineCacheKaydi{});
  runtimeSabitCache_.assign(chunk.sabitler.size(), RuntimeSabitCacheKaydi{});

  auto *kok = memory_.allocate<ObjFunction>("<program>", 0, 0, &chunk, 0, 0, 0);
  pushFrame(kok, 0, 0);

  // Computed goto is opt-in: C++ exceptions and non-trivial locals can make
  // label-as-values dispatch unsafe on MinGW/GCC in try/catch-heavy bytecode.
#if defined(ORHUN_VM_ENABLE_COMPUTED_GOTO) &&                                 \
    (defined(__GNUC__) || defined(__clang__))
#define VM_USE_CGOTO
#endif

  // #define DEBUG_PRINT_CODE
  // #define DEBUG_TRACE_EXECUTION
  // #define DEBUG_STRESS_GC

#ifdef VM_USE_CGOTO
  static void *dispatch_table[] = {&&CASE_OP_SABIT,
                                   &&CASE_OP_BOS,
                                   &&CASE_OP_DOGRU,
                                   &&CASE_OP_YANLIS,
                                   &&CASE_OP_POP,
                                   &&CASE_OP_KOPYA,
                                   &&CASE_OP_GET_LOCAL,
                                   &&CASE_OP_SET_LOCAL,
                                   &&CASE_OP_DEFINE_LOCAL,
                                   &&CASE_OP_GET_GLOBAL,
                                   &&CASE_OP_SET_GLOBAL,
                                   &&CASE_OP_ALAN_AL,
                                   &&CASE_OP_ALAN_YAZ,
                                   &&CASE_OP_METOD_YAZ,
                                   &&CASE_OP_INDEKS_AL,
                                   &&CASE_OP_INDEKS_YAZ,
                                   &&CASE_OP_UZUNLUK,
                                   &&CASE_OP_LISTE_OLUSTUR,
                                   &&CASE_OP_LISTE_PUSH,
                                   &&CASE_OP_LISTE_REZERVE,
                                   &&CASE_OP_SOZLUK_OLUSTUR,
                                   &&CASE_OP_ISLEV_OLUSTUR,
                                   &&CASE_OP_CAGIR,
                                   &&CASE_OP_SINIF,
                                   &&CASE_OP_MIRAS_AL,
                                   &&CASE_OP_TOPLA,
                                   &&CASE_OP_CIKAR,
                                   &&CASE_OP_CARP,
                                   &&CASE_OP_BOL,
                                   &&CASE_OP_MOD,
                                   &&CASE_OP_NEGATE,
                                   &&CASE_OP_NOT,
                                   &&CASE_OP_ESIT,
                                   &&CASE_OP_BUYUK,
                                   &&CASE_OP_KUCUK,
                                   &&CASE_OP_VE,
                                   &&CASE_OP_VEYA,
                                   &&CASE_OP_YAZDIR,
                                   &&CASE_OP_ATLA,
                                   &&CASE_OP_ATLA_EGER_YANLIS,
                                   &&CASE_OP_DONGU,
                                   &&CASE_OP_TRY_BASLA,
                                   &&CASE_OP_TRY_BITIR,
                                   &&CASE_OP_DON,
                                   &&CASE_OP_NOP,
                                   &&CASE_OP_GUVENLI_ALAN_AL};
#define CASE(name) CASE_##name:
#define BREAK DISPATCH()
#define DISPATCH()                                                             \
  do {                                                                         \
    op = static_cast<OpCode>(byteOku());                                       \
    goto *dispatch_table[static_cast<std::uint8_t>(op)];                       \
  } while (false)
#else
#define CASE(name) case OpCode::name:
#define BREAK break
#define DISPATCH()
#endif

  while (!frameYigini_.empty()) {
    try {
      OpCode op;
#ifdef VM_USE_CGOTO
      DISPATCH();
#else
      op = static_cast<OpCode>(byteOku());
      switch (op) {
#endif
      CASE(OP_SABIT) {
        const std::uint16_t sabitIndeks = u16Oku();
        const bool anaChunk =
            frameYigini_.back().function->chunk == chunk_;
        if (anaChunk && sabitIndeks < runtimeSabitCache_.size()) {
          RuntimeSabitCacheKaydi &kayit = runtimeSabitCache_[sabitIndeks];
          if (kayit.hazir) {
            yiginPush(kayit.deger);
            BREAK;
          }
          kayit.deger = sabitDegeriniRuntimeaCevir(sabitOku(sabitIndeks));
          kayit.hazir = true;
          yiginPush(kayit.deger);
          BREAK;
        }
        yiginPush(sabitDegeriniRuntimeaCevir(sabitOku(sabitIndeks)));
        BREAK;
      }
      CASE(OP_BOS)
      yiginPush(Value::bos());
      BREAK;
      CASE(OP_DOGRU)
      yiginPush(Value::mantik(true));
      BREAK;
      CASE(OP_YANLIS)
      yiginPush(Value::mantik(false));
      BREAK;
      CASE(OP_POP)
      (void)yiginPop();
      BREAK;
      CASE(OP_KOPYA)
      yiginPush(yiginBak(0));
      BREAK;
      CASE(OP_GET_LOCAL) {
        const std::uint16_t idx = u16Oku();
        yiginPush(localEris(idx));
        BREAK;
      }
      CASE(OP_SET_LOCAL) {
        const std::uint16_t idx = u16Oku();
        localEris(idx) = yiginBak(0);
        BREAK;
      }
      CASE(OP_DEFINE_LOCAL) {
        const std::uint16_t idx = u16Oku();
        localTanimla(idx, yiginBak(0));
        BREAK;
      }
      CASE(OP_GET_GLOBAL) {
        const std::uint16_t sabitIndeks = u16Oku();
        const SabitDeger &adDegeri = sabitOku(sabitIndeks);
        if (!std::holds_alternative<std::string>(adDegeri.veri)) {
          calismaHatasi("GET_GLOBAL sabiti metin degil.");
        }
        const std::string &ad = std::get<std::string>(adDegeri.veri);
        if (auto hucre = yakalananHucreBul(ad)) {
          yiginPush(*hucre);
          BREAK;
        }
        const bool anaChunk =
            frameYigini_.back().function->chunk == chunk_;
        if (anaChunk && sabitIndeks < globalInlineCache_.size()) {
          GlobalInlineCacheKaydi &kayit = globalInlineCache_[sabitIndeks];
          if (kayit.gecerli && kayit.deger != nullptr) {
            yiginPush(*kayit.deger);
            BREAK;
          }
        }
        const auto it = globaller_.find(ad);
        if (it == globaller_.end()) {
          calismaHatasi("Tanimsiz degisken: '" + ad + "'.");
        }
        if (anaChunk && sabitIndeks < globalInlineCache_.size()) {
          GlobalInlineCacheKaydi &kayit = globalInlineCache_[sabitIndeks];
          kayit.deger = const_cast<Value *>(&it->second);
          kayit.gecerli = true;
        }
        yiginPush(it->second);
        BREAK;
      }
      CASE(OP_SET_GLOBAL) {
        const std::uint16_t sabitIndeks = u16Oku();
        const bool anaChunk =
            frameYigini_.back().function->chunk == chunk_;
        const SabitDeger &adDegeri = sabitOku(sabitIndeks);
        if (!std::holds_alternative<std::string>(adDegeri.veri)) {
          calismaHatasi("SET_GLOBAL sabiti metin degil.");
        }
        const std::string &ad = std::get<std::string>(adDegeri.veri);
        const Value atanan = yiginBak(0);
        if (auto hucre = yakalananHucreBul(ad)) {
          *hucre = atanan;
          BREAK;
        }
        auto it = globaller_.find(ad);
        if (it == globaller_.end()) {
          auto [yeniIt, _eklendi] = globaller_.emplace(ad, atanan);
          it = yeniIt;
        } else {
          it->second = atanan;
        }
        if (anaChunk && sabitIndeks < globalInlineCache_.size()) {
          GlobalInlineCacheKaydi &kayit = globalInlineCache_[sabitIndeks];
          kayit.deger = &it->second;
          kayit.gecerli = true;
        }
        BREAK;
      }
      CASE(OP_ALAN_AL) {
        const SabitDeger &adDegeri = sabitOku(u16Oku());
        if (!std::holds_alternative<std::string>(adDegeri.veri)) {
          calismaHatasi("ALAN_AL sabiti metin degil.");
        }
        const Value hedef = yiginPop();
        yiginPush(alanAl(hedef, std::get<std::string>(adDegeri.veri)));
        BREAK;
      }
      CASE(OP_GUVENLI_ALAN_AL) {
        const SabitDeger &adDegeri = sabitOku(u16Oku());
        if (!std::holds_alternative<std::string>(adDegeri.veri)) {
          calismaHatasi("GUVENLI_ALAN_AL sabiti metin degil.");
        }
        const Value hedef = yiginPop();
        if (hedef.type == ValueType::BOS) {
          yiginPush(Value::bos());
          BREAK;
        }
        yiginPush(alanAl(hedef, std::get<std::string>(adDegeri.veri)));
        BREAK;
      }
      CASE(OP_ALAN_YAZ)
      CASE(OP_METOD_YAZ) {
        const SabitDeger &adDegeri = sabitOku(u16Oku());
        if (!std::holds_alternative<std::string>(adDegeri.veri)) {
          calismaHatasi("ALAN/METOD sabiti metin degil.");
        }
        const std::string &ad = std::get<std::string>(adDegeri.veri);
        const Value deger = yiginPop();
        const Value hedef = yiginPop();
        if (!hedef.nesneMi() || hedef.as.nesne == nullptr) {
          calismaHatasi("Alan atamasi yalnizca nesnelerde yapilabilir.");
        }
        Obj *obj = hedef.as.nesne;
        if (obj->type == ObjType::DICT) {
          static_cast<ObjDict *>(obj)->alanlar[ad] = deger;
        } else if (obj->type == ObjType::INSTANCE) {
          static_cast<ObjInstance *>(obj)->alanlar[ad] = deger;
        } else if (obj->type == ObjType::CLASS) {
          auto *sinif = static_cast<ObjClass *>(obj);
          if (op == OpCode::OP_METOD_YAZ) {
            sinif->metodlar[ad] = deger;
          } else {
            sinif->alanVarsayilanlari[ad] = deger;
          }
        } else {
          calismaHatasi("Bu tipte alan atamasi desteklenmiyor.");
        }
        yiginPush(deger);
        BREAK;
      }
      CASE(OP_INDEKS_AL) {
        const Value indeks = yiginPop();
        const Value hedef = yiginPop();
        yiginPush(indeksAl(hedef, indeks));
        BREAK;
      }
      CASE(OP_INDEKS_YAZ) {
        const Value deger = yiginPop();
        const Value indeks = yiginPop();
        const Value hedef = yiginPop();
        if (!hedef.nesneMi() || hedef.as.nesne == nullptr) {
          calismaHatasi("Indeks atamasi yalnizca liste/sozlukte yapilabilir.");
        }
        if (hedef.as.nesne->type == ObjType::LIST) {
          auto *liste = static_cast<ObjList *>(hedef.as.nesne);
          const double indeksSayi = indeks.type == ValueType::SAYI
                                        ? indeks.as.sayi
                                        : sayiyaCevir(indeks, "liste atama");
          const int i = static_cast<int>(std::llround(indeksSayi));
          if (i < 0 || static_cast<std::size_t>(i) >= liste->ogeler.size()) {
            calismaHatasi("Liste indeksi sinir disi.");
          }
          liste->ogeler[static_cast<std::size_t>(i)] = deger;
        } else if (hedef.as.nesne->type == ObjType::DICT) {
          static_cast<ObjDict *>(hedef.as.nesne)
              ->alanlar[anahtaraCevir(indeks, "sozluk atama")] = deger;
        } else {
          calismaHatasi("Bu tipte indeks atamasi desteklenmiyor.");
        }
        yiginPush(deger);
        BREAK;
      }
      CASE(OP_UZUNLUK) {
        const Value hedef = yiginPop();
        if (objTipiMi(hedef, ObjType::LIST)) {
          const auto *liste = static_cast<ObjList *>(hedef.as.nesne);
          yiginPush(Value::sayi(static_cast<double>(liste->ogeler.size())));
          BREAK;
        }
        if (objTipiMi(hedef, ObjType::DICT)) {
          const auto *sozluk = static_cast<ObjDict *>(hedef.as.nesne);
          yiginPush(Value::sayi(static_cast<double>(sozluk->alanlar.size())));
          BREAK;
        }
        if (objTipiMi(hedef, ObjType::STRING)) {
          const auto *metin = static_cast<ObjString *>(hedef.as.nesne);
          yiginPush(Value::sayi(static_cast<double>(metin->deger.size())));
          BREAK;
        }
        calismaHatasi("uzunluk yalnizca liste/sozluk/metin uzerinde calisir.");
      }
      CASE(OP_LISTE_OLUSTUR) {
        const std::uint16_t adet = u16Oku();
        if (adet > yigin_.size()) {
          calismaHatasi("LISTE_OLUSTUR: yigin yetersiz.");
        }
        std::vector<Value> ogeler(adet);
        for (std::size_t i = 0; i < adet; ++i) {
          ogeler[adet - 1 - i] = yiginPop();
        }
        yiginPush(yeniListe(std::move(ogeler)));
        BREAK;
      }
      CASE(OP_LISTE_PUSH) {
        const Value deger = yiginPop();
        const Value hedef = yiginPop();
        if (!objTipiMi(hedef, ObjType::LIST)) {
          calismaHatasi("LISTE_PUSH yalnizca listelerde calisir.");
        }
        static_cast<ObjList *>(hedef.as.nesne)->ogeler.push_back(deger);
        yiginPush(Value::bos());
        BREAK;
      }
      CASE(OP_LISTE_REZERVE) {
        const Value kapasiteV = yiginPop();
        const Value hedef = yiginPop();
        if (!objTipiMi(hedef, ObjType::LIST)) {
          calismaHatasi("LISTE_REZERVE yalnizca listelerde calisir.");
        }
        const double kapasiteD = kapasiteV.type == ValueType::SAYI
                                     ? kapasiteV.as.sayi
                                     : sayiyaCevir(kapasiteV, "liste reserve");
        if (kapasiteD < 0.0 || !tamSayiMi(kapasiteD)) {
          calismaHatasi(
              "LISTE_REZERVE kapasitesi sifirdan buyuk tam sayi olmali.");
        }
        static_cast<ObjList *>(hedef.as.nesne)
            ->ogeler.reserve(static_cast<std::size_t>(kapasiteD));
        yiginPush(Value::bos());
        BREAK;
      }
      CASE(OP_SOZLUK_OLUSTUR) {
        const std::uint16_t adet = u16Oku();
        if (static_cast<std::size_t>(adet) * 2 > yigin_.size()) {
          calismaHatasi("SOZLUK_OLUSTUR: yigin yetersiz.");
        }
        std::unordered_map<std::string, Value> alanlar;
        for (std::size_t i = 0; i < adet; ++i) {
          const Value deger = yiginPop();
          const Value anahtar = yiginPop();
          alanlar[anahtaraCevir(anahtar, "sozluk olusturma")] = deger;
        }
        yiginPush(yeniSozluk(std::move(alanlar)));
        BREAK;
      }
      CASE(OP_ISLEV_OLUSTUR) {
        const SabitDeger &adDegeri = sabitOku(u16Oku());
        const std::uint16_t minArity = u16Oku();
        const std::uint16_t maxArity = u16Oku();
        const std::uint16_t giris = u16Oku();
        const std::uint16_t localSayisi = u16Oku();
        const std::uint16_t baglamArg = u16Oku();
        const std::uint16_t localAdSayisi = u16Oku();
        if (!std::holds_alternative<std::string>(adDegeri.veri)) {
          calismaHatasi("ISLEV_OLUSTUR ad sabiti metin degil.");
        }
        std::vector<std::pair<std::uint16_t, std::string>> localAdlari;
        localAdlari.reserve(localAdSayisi);
        for (std::uint16_t i = 0; i < localAdSayisi; ++i) {
          const std::uint16_t localIndeks = u16Oku();
          const SabitDeger &localAdDegeri = sabitOku(u16Oku());
          if (!std::holds_alternative<std::string>(localAdDegeri.veri)) {
            calismaHatasi("ISLEV_OLUSTUR local adi sabiti metin degil.");
          }
          localAdlari.push_back(
              {localIndeks, std::get<std::string>(localAdDegeri.veri)});
        }
        const BytecodeChunk *aktifChunk = frameYigini_.back().function->chunk;
        if (aktifChunk == nullptr) {
          calismaHatasi("ISLEV_OLUSTUR aktif chunk bulunamadi.");
        }
        auto *fn = memory_.allocate<ObjFunction>(
            std::get<std::string>(adDegeri.veri), static_cast<int>(minArity),
            static_cast<int>(maxArity), aktifChunk, giris, localSayisi,
            baglamArg);
        fn->localAdlari.resize(localSayisi);
        for (const auto &[localIndeks, localAd] : localAdlari) {
          if (localIndeks < fn->localAdlari.size()) {
            fn->localAdlari[localIndeks] = localAd;
          }
        }
        const CallFrame &olusturanFrame = frameYigini_.back();
        if (olusturanFrame.function != nullptr) {
          const ObjFunction *olusturan = olusturanFrame.function;
          for (std::size_t i = 0; i < olusturan->localAdlari.size(); ++i) {
            const std::string &localAd = olusturan->localAdlari[i];
            if (localAd.empty()) {
              continue;
            }
            fn->yakalananDegerler[localAd] =
                localHucreAl(static_cast<std::uint16_t>(i));
          }
        }
        yiginPush(Value::nesne(fn));
        BREAK;
      }
      CASE(OP_CAGIR) {
        const std::uint16_t argc = u16Oku();
        if (yigin_.size() < static_cast<std::size_t>(argc) + 1) {
          calismaHatasi("CAGIR: yigin yetersiz.");
        }
        std::size_t calleeIndex =
            yigin_.size() - static_cast<std::size_t>(argc) - 1;
        Value cagrilan = yigin_[calleeIndex];

        if (objTipiMi(cagrilan, ObjType::BOUND_METHOD)) {
          auto *bm = static_cast<ObjBoundMethod *>(cagrilan.as.nesne);
          if (objTipiMi(bm->metod, ObjType::FUNCTION)) {
            auto *fn = static_cast<ObjFunction *>(bm->metod.as.nesne);
            if (fn->baglamArgSayisi == 1) {
              yigin_[calleeIndex] = bm->alici;
              islevCagrisiHazirla(calleeIndex,
                                  static_cast<std::uint16_t>(argc + 1), fn,
                                  calleeIndex);
              BREAK;
            }
            if (fn->baglamArgSayisi == 2) {
              if (!objTipiMi(bm->alici, ObjType::INSTANCE)) {
                calismaHatasi("Bagli metod super baglami olusturamadi.");
              }
              auto *inst = static_cast<ObjInstance *>(bm->alici.as.nesne);
              ObjClass *ustSinif = metodUstSinifBaglamiBul(inst, fn);
              if (ustSinif == nullptr) {
                calismaHatasi("'ust' kullanimi icin ebeveyn sinif bulunamadi.");
              }
              Value ustRef = Value::nesne(memory_.allocate<ObjSuperRef>(
                  bm->alici, ustSinif));
              yigin_.push_back(Value::bos());
              for (std::size_t i = yigin_.size() - 1; i > calleeIndex + 1;
                   --i) {
                yigin_[i] = yigin_[i - 1];
              }
              yigin_[calleeIndex] = bm->alici;
              yigin_[calleeIndex + 1] = ustRef;
              islevCagrisiHazirla(calleeIndex,
                                  static_cast<std::uint16_t>(argc + 2), fn,
                                  calleeIndex);
              BREAK;
            }
          }

          yigin_[calleeIndex] = bm->metod;
          yigin_.insert(yigin_.begin() +
                            static_cast<std::ptrdiff_t>(calleeIndex + 1),
                        bm->alici);
          cagrilan = yigin_[calleeIndex];
          std::uint16_t yeniArgc = static_cast<std::uint16_t>(argc + 1);
          if (objTipiMi(cagrilan, ObjType::FUNCTION)) {
            auto *fn = static_cast<ObjFunction *>(cagrilan.as.nesne);
            islevCagrisiHazirla(calleeIndex, yeniArgc, fn);
            BREAK;
          }
          const Value sonuc = cagir(cagrilan, calleeIndex + 1,
                                    static_cast<std::size_t>(yeniArgc));
          yigin_.resize(calleeIndex);
          yiginPush(sonuc);
          BREAK;
        }

        if (objTipiMi(cagrilan, ObjType::FUNCTION)) {
          islevCagrisiHazirla(calleeIndex, argc,
                              static_cast<ObjFunction *>(cagrilan.as.nesne));
          BREAK;
        }

        if (objTipiMi(cagrilan, ObjType::CLASS)) {
          auto *sinif = static_cast<ObjClass *>(cagrilan.as.nesne);
          auto *inst = memory_.allocate<ObjInstance>(sinif);
          inst->alanlar = sinif->alanVarsayilanlari;
          Value instDegeri = Value::nesne(inst);

          auto ctorIt = sinif->metodlar.find("kur");
          if (ctorIt != sinif->metodlar.end()) {
            if (objTipiMi(ctorIt->second, ObjType::FUNCTION)) {
              auto *fn = static_cast<ObjFunction *>(ctorIt->second.as.nesne);
              if (fn->baglamArgSayisi == 1) {
                yigin_[calleeIndex] = instDegeri;
                islevCagrisiHazirla(calleeIndex,
                                    static_cast<std::uint16_t>(argc + 1), fn,
                                    calleeIndex);
                bekleyenKurucular_.push_back(
                    BekleyenKurucu{frameYigini_.size(), instDegeri});
                BREAK;
              }
              if (fn->baglamArgSayisi == 2) {
                ObjClass *ustSinif = metodUstSinifBaglamiBul(inst, fn);
                if (ustSinif == nullptr) {
                  calismaHatasi(
                      "Kurucu super baglami icin ebeveyn sinif bulunamadi.");
                }
                Value ustRef = Value::nesne(
                    memory_.allocate<ObjSuperRef>(instDegeri, ustSinif));
                yigin_.push_back(Value::bos());
                for (std::size_t i = yigin_.size() - 1; i > calleeIndex + 1;
                     --i) {
                  yigin_[i] = yigin_[i - 1];
                }
                yigin_[calleeIndex] = instDegeri;
                yigin_[calleeIndex + 1] = ustRef;
                islevCagrisiHazirla(calleeIndex,
                                    static_cast<std::uint16_t>(argc + 2), fn,
                                    calleeIndex);
                bekleyenKurucular_.push_back(
                    BekleyenKurucu{frameYigini_.size(), instDegeri});
                BREAK;
              }
            }

            yigin_[calleeIndex] = ctorIt->second;
            yigin_.insert(yigin_.begin() +
                              static_cast<std::ptrdiff_t>(calleeIndex + 1),
                          instDegeri);
            std::uint16_t yeniArgc = static_cast<std::uint16_t>(argc + 1);
            if (objTipiMi(yigin_[calleeIndex], ObjType::FUNCTION)) {
              auto *fn =
                  static_cast<ObjFunction *>(yigin_[calleeIndex].as.nesne);
              islevCagrisiHazirla(calleeIndex, yeniArgc, fn);
              bekleyenKurucular_.push_back(
                  BekleyenKurucu{frameYigini_.size(), instDegeri});
              BREAK;
            }
            (void)cagir(yigin_[calleeIndex], calleeIndex + 1,
                        static_cast<std::size_t>(yeniArgc));
          }

          yigin_.resize(calleeIndex);
          yiginPush(instDegeri);
          BREAK;
        }

        const Value sonuc =
            cagir(cagrilan, calleeIndex + 1, static_cast<std::size_t>(argc));
        yigin_.resize(calleeIndex);
        yiginPush(sonuc);
        BREAK;
      }
      CASE(OP_SINIF) {
        const SabitDeger &adDegeri = sabitOku(u16Oku());
        if (!std::holds_alternative<std::string>(adDegeri.veri)) {
          calismaHatasi("SINIF sabiti metin degil.");
        }
        yiginPush(Value::nesne(
            memory_.allocate<ObjClass>(std::get<std::string>(adDegeri.veri))));
        BREAK;
      }
      CASE(OP_MIRAS_AL) {
        const Value ebeveyn = yiginPop();
        const Value cocuk = yiginPop();
        if (!objTipiMi(ebeveyn, ObjType::CLASS) ||
            !objTipiMi(cocuk, ObjType::CLASS)) {
          calismaHatasi("MIRAS_AL: iki taraf da sinif olmali.");
        }
        auto *parent = static_cast<ObjClass *>(ebeveyn.as.nesne);
        auto *child = static_cast<ObjClass *>(cocuk.as.nesne);
        child->ebeveyn = parent;
        for (const auto &[k, v] : parent->metodlar) {
          child->metodlar.emplace(k, v);
        }
        for (const auto &[k, v] : parent->alanVarsayilanlari) {
          child->alanVarsayilanlari.emplace(k, v);
        }
        yiginPush(cocuk);
        BREAK;
      }
      CASE(OP_TOPLA) {
        const Value sag = yiginPop();
        const Value sol = yiginPop();
        if (sol.type == ValueType::SAYI && sag.type == ValueType::SAYI) {
          yiginPush(Value::sayi(sol.as.sayi + sag.as.sayi));
        } else if (objTipiMi(sol, ObjType::STRING) &&
                   objTipiMi(sag, ObjType::STRING)) {
          const auto *solStr = static_cast<const ObjString *>(sol.as.nesne);
          const auto *sagStr = static_cast<const ObjString *>(sag.as.nesne);
          std::string birlesik;
          birlesik.reserve(solStr->deger.size() + sagStr->deger.size());
          birlesik += solStr->deger;
          birlesik += sagStr->deger;
          yiginPush(yeniString(birlesik));
        } else if (objTipiMi(sol, ObjType::STRING) ||
                   objTipiMi(sag, ObjType::STRING)) {
          const std::string solMetin = metneCevir(sol);
          const std::string sagMetin = metneCevir(sag);
          std::string birlesik;
          birlesik.reserve(solMetin.size() + sagMetin.size());
          birlesik += solMetin;
          birlesik += sagMetin;
          yiginPush(yeniString(birlesik));
        } else {
          yiginPush(Value::sayi(sayiyaCevir(sol, "toplama") +
                                sayiyaCevir(sag, "toplama")));
        }
        BREAK;
      }
      CASE(OP_CIKAR)
      CASE(OP_CARP)
      CASE(OP_BOL) {
        const Value sagV = yiginPop();
        const Value solV = yiginPop();
        const double sag = sagV.type == ValueType::SAYI
                               ? sagV.as.sayi
                               : sayiyaCevir(sagV, "sayisal islem");
        const double sol = solV.type == ValueType::SAYI
                               ? solV.as.sayi
                               : sayiyaCevir(solV, "sayisal islem");
        if (op == OpCode::OP_CIKAR) {
          yiginPush(Value::sayi(sol - sag));
        } else if (op == OpCode::OP_CARP) {
          yiginPush(Value::sayi(sol * sag));
        } else {
          if (std::abs(sag) <= std::numeric_limits<double>::epsilon()) {
            calismaHatasi("Sıfıra bölme yapılamaz.");
          }
          yiginPush(Value::sayi(sol / sag));
        }
        BREAK;
      }
      CASE(OP_MOD) {
        const Value sagV = yiginPop();
        const Value solV = yiginPop();
        const double sag =
            sagV.type == ValueType::SAYI ? sagV.as.sayi
                                         : sayiyaCevir(sagV, "mod alma");
        const double sol =
            solV.type == ValueType::SAYI ? solV.as.sayi
                                         : sayiyaCevir(solV, "mod alma");
        if (std::abs(sag) <= std::numeric_limits<double>::epsilon()) {
          calismaHatasi("Sıfıra göre mod alınamaz.");
        }
        yiginPush(Value::sayi(std::fmod(sol, sag)));
        BREAK;
      }
      CASE(OP_NEGATE) {
        const Value deger = yiginPop();
        if (deger.type == ValueType::SAYI) {
          yiginPush(Value::sayi(-deger.as.sayi));
        } else {
          yiginPush(Value::sayi(-sayiyaCevir(deger, "isaret degistirme")));
        }
        BREAK;
      }
      CASE(OP_NOT)
      yiginPush(Value::mantik(falseMi(yiginPop())));
      BREAK;
      CASE(OP_ESIT) {
        const Value sag = yiginPop();
        const Value sol = yiginPop();
        yiginPush(Value::mantik(esitMi(sol, sag)));
        BREAK;
      }
      CASE(OP_BUYUK) {
        const Value sagV = yiginPop();
        const Value solV = yiginPop();
        const double sag = sagV.type == ValueType::SAYI
                               ? sagV.as.sayi
                               : sayiyaCevir(sagV, "karsilastirma");
        const double sol = solV.type == ValueType::SAYI
                               ? solV.as.sayi
                               : sayiyaCevir(solV, "karsilastirma");
        yiginPush(Value::mantik(sol > sag));
        BREAK;
      }
      CASE(OP_KUCUK) {
        const Value sagV = yiginPop();
        const Value solV = yiginPop();
        const double sag = sagV.type == ValueType::SAYI
                               ? sagV.as.sayi
                               : sayiyaCevir(sagV, "karsilastirma");
        const double sol = solV.type == ValueType::SAYI
                               ? solV.as.sayi
                               : sayiyaCevir(solV, "karsilastirma");
        yiginPush(Value::mantik(sol < sag));
        BREAK;
      }
      CASE(OP_VE) {
        const Value sag = yiginPop();
        const Value sol = yiginPop();
        yiginPush(Value::mantik(!falseMi(sol) && !falseMi(sag)));
        BREAK;
      }
      CASE(OP_VEYA) {
        const Value sag = yiginPop();
        const Value sol = yiginPop();
        yiginPush(Value::mantik(!falseMi(sol) || !falseMi(sag)));
        BREAK;
      }
      CASE(OP_YAZDIR)
      std::cout << metneCevir(yiginPop()) << '\n';
      BREAK;
      CASE(OP_ATLA)
      frameYigini_.back().ip += u16Oku();
      BREAK;
      CASE(OP_ATLA_EGER_YANLIS) {
        const std::uint16_t ofset = u16Oku();
        if (falseMi(yiginBak(0))) {
          frameYigini_.back().ip += ofset;
        }
        BREAK;
      }
      CASE(OP_DONGU) {
        const std::uint16_t ofset = u16Oku();
        if (frameYigini_.back().ip < ofset) {
          calismaHatasi("Gecersiz dongu geri atlamasi.");
        }
        frameYigini_.back().ip -= ofset;
        BREAK;
      }
      CASE(OP_TRY_BASLA) {
        const std::uint16_t ofset = u16Oku();
        TryFrame tf;
        tf.frameDerinligi = frameYigini_.size();
        tf.stackBoyutu = yigin_.size();
        tf.catchIp = frameYigini_.back().ip + ofset;
        tryYigini_.push_back(tf);
        BREAK;
      }
      CASE(OP_TRY_BITIR)
      if (tryYigini_.empty()) {
        calismaHatasi("TRY_BITIR eslesen TRY_BASLA olmadan kullanildi.");
      }
      tryYigini_.pop_back();
      BREAK;
      CASE(OP_DON) {
        Value donus = yiginPop();
        if (!bekleyenKurucular_.empty() &&
            bekleyenKurucular_.back().frameDerinligi == frameYigini_.size()) {
          donus = bekleyenKurucular_.back().olusanNesne;
          bekleyenKurucular_.pop_back();
        }
        popFrameVeDon(donus);
        if (frameYigini_.empty()) {
          if (gcErtelemeDerinligi_ == 0) {
            gcCalistir();
          }
          return;
        }
        BREAK;
      }
      CASE(OP_NOP)
      BREAK;
#ifndef VM_USE_CGOTO
    }
#endif
  }
  catch (const std::exception &ex) {
    if (tryYigini_.empty()) {
      throw;
    }

    const TryFrame hedef = tryYigini_.back();
    tryYigini_.pop_back();

    if (hedef.frameDerinligi == 0 ||
        frameYigini_.size() < hedef.frameDerinligi) {
      throw;
    }
    while (frameYigini_.size() > hedef.frameDerinligi) {
      frameYigini_.pop_back();
    }
    while (!tryYigini_.empty() &&
           tryYigini_.back().frameDerinligi > frameYigini_.size()) {
      tryYigini_.pop_back();
    }

    if (frameYigini_.empty()) {
      throw;
    }
    if (yigin_.size() > hedef.stackBoyutu) {
      yigin_.resize(hedef.stackBoyutu);
    } else {
      while (yigin_.size() < hedef.stackBoyutu) {
        yiginPush(Value::bos());
      }
    }

    CallFrame &aktif = frameYigini_.back();
    if (aktif.function == nullptr || aktif.function->chunk == nullptr ||
        hedef.catchIp > aktif.function->chunk->kod.size()) {
      throw;
    }
    aktif.ip = hedef.catchIp;
    std::string yakalanan = ex.what();
    const std::string onEk = "VM Calisma Hatasi";
    if (yakalanan.rfind(onEk, 0) == 0) {
      const std::size_t satirBas = yakalanan.find("(satir ");
      const std::size_t blokSon = yakalanan.find("): ");
      if (satirBas != std::string::npos && blokSon != std::string::npos &&
          blokSon > satirBas + 7) {
        const std::string satirNo =
            yakalanan.substr(satirBas + 7, blokSon - (satirBas + 7));
        const std::string mesaj = yakalanan.substr(blokSon + 3);
        yakalanan = "Satır " + satirNo + ": " + mesaj;
      } else {
        const std::size_t ikiNokta = yakalanan.find(": ");
        if (ikiNokta != std::string::npos && ikiNokta + 2 < yakalanan.size()) {
          yakalanan = yakalanan.substr(ikiNokta + 2);
        }
      }
    }
    yiginPush(yeniString(yakalanan));
  }
  gcGerekirseCalistir();
}
}

void VM::yiginPush(Value deger) { yigin_.push_back(deger); }

Value VM::yiginPop() {
  if (yigin_.empty()) {
    calismaHatasi("Yigin alttan tasma hatasi.");
  }
  Value son = yigin_.back();
  yigin_.pop_back();
  return son;
}

const Value &VM::yiginBak(std::size_t tersten) const {
  if (tersten >= yigin_.size()) {
    calismaHatasi("Yigin okunamadi (sinir disi).");
  }
  return yigin_[yigin_.size() - 1 - tersten];
}

Value &VM::localEris(std::uint16_t indeks) {
  if (frameYigini_.empty()) {
    calismaHatasi("Local erisimi icin aktif frame yok.");
  }
  CallFrame &frame = frameYigini_.back();
  const auto hucre = frame.localHucreleri.find(indeks);
  if (hucre != frame.localHucreleri.end() && hucre->second) {
    return *hucre->second;
  }
  const std::size_t hedef = frame.slotBase + indeks;
  if (hedef >= yigin_.size()) {
    calismaHatasi("Local indeks sinir disi.");
  }
  return yigin_[hedef];
}

const Value &VM::localEris(std::uint16_t indeks) const {
  if (frameYigini_.empty()) {
    calismaHatasi("Local erisimi icin aktif frame yok.");
  }
  const CallFrame &frame = frameYigini_.back();
  const auto hucre = frame.localHucreleri.find(indeks);
  if (hucre != frame.localHucreleri.end() && hucre->second) {
    return *hucre->second;
  }
  const std::size_t hedef = frame.slotBase + indeks;
  if (hedef >= yigin_.size()) {
    calismaHatasi("Local indeks sinir disi.");
  }
  return yigin_[hedef];
}

std::shared_ptr<Value> VM::localHucreAl(std::uint16_t indeks) {
  if (frameYigini_.empty()) {
    calismaHatasi("Local hucresi icin aktif frame yok.");
  }
  CallFrame &frame = frameYigini_.back();
  const auto mevcut = frame.localHucreleri.find(indeks);
  if (mevcut != frame.localHucreleri.end() && mevcut->second) {
    return mevcut->second;
  }
  const std::size_t hedef = frame.slotBase + indeks;
  if (hedef >= yigin_.size()) {
    calismaHatasi("Local hucresi icin indeks sinir disi.");
  }
  auto hucre = std::make_shared<Value>(yigin_[hedef]);
  frame.localHucreleri[indeks] = hucre;
  return hucre;
}

void VM::localTanimla(std::uint16_t indeks, const Value &deger) {
  if (frameYigini_.empty()) {
    calismaHatasi("Local tanimi icin aktif frame yok.");
  }
  CallFrame &frame = frameYigini_.back();
  const std::size_t hedef = frame.slotBase + indeks;
  if (hedef >= yigin_.size()) {
    calismaHatasi("Local tanimi icin indeks sinir disi.");
  }
  frame.localHucreleri.erase(indeks);
  yigin_[hedef] = deger;
}

std::shared_ptr<Value> VM::yakalananHucreBul(const std::string &ad) const {
  if (frameYigini_.empty() || frameYigini_.back().function == nullptr) {
    return nullptr;
  }
  const auto &yakalananlar = frameYigini_.back().function->yakalananDegerler;
  const auto bulunan = yakalananlar.find(ad);
  if (bulunan == yakalananlar.end()) {
    return nullptr;
  }
  return bulunan->second;
}

std::uint8_t VM::byteOku() {
  if (frameYigini_.empty()) {
    calismaHatasi("Frame yok.");
  }
  CallFrame &frame = frameYigini_.back();
  const BytecodeChunk *chunk = frame.function->chunk;
  if (chunk == nullptr || frame.ip >= chunk->kod.size()) {
    calismaHatasi("Bytecode sonu beklenmedik sekilde bitti.");
  }
  return chunk->kod[frame.ip++];
}

std::uint16_t VM::u16Oku() {
  const std::uint8_t yuksek = byteOku();
  const std::uint8_t dusuk = byteOku();
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(yuksek) << 8) |
                                    static_cast<std::uint16_t>(dusuk));
}

const SabitDeger &VM::sabitOku(std::uint16_t indeks) const {
  if (frameYigini_.empty()) {
    calismaHatasi("Frame yok.");
  }
  const BytecodeChunk *chunk = frameYigini_.back().function->chunk;
  if (chunk == nullptr || indeks >= chunk->sabitler.size()) {
    calismaHatasi("Sabit indeksi gecersiz.");
  }
  return chunk->sabitler[indeks];
}

Value VM::sabitDegeriniRuntimeaCevir(const SabitDeger &sabit) {
  if (std::holds_alternative<std::monostate>(sabit.veri)) {
    return Value::bos();
  }
  if (const auto *sayi = std::get_if<double>(&sabit.veri)) {
    return Value::sayi(*sayi);
  }
  if (const auto *mantik = std::get_if<bool>(&sabit.veri)) {
    return Value::mantik(*mantik);
  }
  if (const auto *metin = std::get_if<std::string>(&sabit.veri)) {
    return yeniString(*metin);
  }
  return Value::bos();
}

bool VM::falseMi(const Value &deger) const {
  switch (deger.type) {
  case ValueType::BOS:
    return true;
  case ValueType::SAYI:
    return std::abs(deger.as.sayi) <= std::numeric_limits<double>::epsilon();
  case ValueType::MANTIK:
    return !deger.as.mantik;
  case ValueType::NESNE:
    break;
  }
  if (deger.as.nesne == nullptr) {
    return true;
  }
  if (deger.as.nesne->type == ObjType::STRING) {
    return static_cast<ObjString *>(deger.as.nesne)->deger.empty();
  }
  if (deger.as.nesne->type == ObjType::LIST) {
    return static_cast<ObjList *>(deger.as.nesne)->ogeler.empty();
  }
  if (deger.as.nesne->type == ObjType::DICT) {
    return static_cast<ObjDict *>(deger.as.nesne)->alanlar.empty();
  }
  return false;
}

bool VM::esitMi(const Value &sol, const Value &sag) const {
  if (sol.type != sag.type) {
    return false;
  }
  switch (sol.type) {
  case ValueType::BOS:
    return true;
  case ValueType::SAYI:
    return sol.as.sayi == sag.as.sayi;
  case ValueType::MANTIK:
    return sol.as.mantik == sag.as.mantik;
  case ValueType::NESNE:
    if (sol.as.nesne == sag.as.nesne) {
      return true;
    }
    if (objTipiMi(sol, ObjType::STRING) && objTipiMi(sag, ObjType::STRING)) {
      return static_cast<ObjString *>(sol.as.nesne)->deger ==
             static_cast<ObjString *>(sag.as.nesne)->deger;
    }
    return false;
  }
  return false;
}

std::string VM::metneCevir(const Value &deger) const {
  if (deger.type == ValueType::BOS) {
    return "bos";
  }
  if (deger.type == ValueType::SAYI) {
    return sayiyiMetinlestir(deger.as.sayi);
  }
  if (deger.type == ValueType::MANTIK) {
    // Interpreter parity: bool degerleri 1/0 olarak goster.
    return deger.as.mantik ? "1" : "0";
  }
  if (deger.as.nesne == nullptr) {
    return "bos";
  }

  switch (deger.as.nesne->type) {
  case ObjType::STRING:
    return static_cast<ObjString *>(deger.as.nesne)->deger;
  case ObjType::LIST: {
    const auto *liste = static_cast<ObjList *>(deger.as.nesne);
    std::string sonuc = "[";
    for (std::size_t i = 0; i < liste->ogeler.size(); ++i) {
      if (i > 0) {
        sonuc += ", ";
      }
      sonuc += metneCevir(liste->ogeler[i]);
    }
    sonuc += "]";
    return sonuc;
  }
  case ObjType::DICT: {
    const auto *sozluk = static_cast<ObjDict *>(deger.as.nesne);
    std::string sonuc = "{";
    bool ilk = true;
    for (const auto &[k, v] : sozluk->alanlar) {
      if (!ilk) {
        sonuc += ", ";
      }
      ilk = false;
      sonuc += k + ": " + metneCevir(v);
    }
    sonuc += "}";
    return sonuc;
  }
  case ObjType::FUNCTION:
    return "<islev:" + static_cast<ObjFunction *>(deger.as.nesne)->ad + ">";
  case ObjType::NATIVE:
    return "<yerlesik:" + static_cast<ObjNative *>(deger.as.nesne)->ad + ">";
  case ObjType::CLASS:
    return "<sinif:" + static_cast<ObjClass *>(deger.as.nesne)->ad + ">";
  case ObjType::INSTANCE: {
    auto *inst = static_cast<ObjInstance *>(deger.as.nesne);
    return "<" +
           (inst->sinif != nullptr ? inst->sinif->ad : std::string("Nesne")) +
           " nesnesi>";
  }
  case ObjType::BOUND_METHOD:
    return "<bagli_metod>";
  case ObjType::SUPER_REF:
    return "<ust>";
  }
  return "<deger>";
}

double VM::sayiyaCevir(const Value &deger, const std::string &baglam) const {
  if (deger.type == ValueType::SAYI) {
    return deger.as.sayi;
  }
  if (deger.type == ValueType::MANTIK) {
    return deger.as.mantik ? 1.0 : 0.0;
  }
  if (objTipiMi(deger, ObjType::STRING)) {
    try {
      return std::stod(static_cast<ObjString *>(deger.as.nesne)->deger);
    } catch (...) {
      calismaHatasi("Sayisal olmayan metin ile " + baglam + " yapilamaz.");
    }
  }
  calismaHatasi("Sayisal olmayan deger ile " + baglam + " yapilamaz.");
}

std::string VM::anahtaraCevir(const Value &deger,
                              const std::string &baglam) const {
  if (objTipiMi(deger, ObjType::STRING)) {
    return static_cast<ObjString *>(deger.as.nesne)->deger;
  }
  if (deger.type == ValueType::SAYI) {
    return sayiyiMetinlestir(deger.as.sayi);
  }
  if (deger.type == ValueType::MANTIK) {
    return deger.as.mantik ? "dogru" : "yanlis";
  }
  calismaHatasi("Anahtar metne cevrilemedi (" + baglam + ").");
}

Value VM::alanAl(const Value &hedef, const std::string &alanAdi) {
  if (!hedef.nesneMi() || hedef.as.nesne == nullptr) {
    calismaHatasi("Alan erisimi yalnizca nesnelerde yapilabilir.");
  }

  Obj *obj = hedef.as.nesne;
  if (obj->type == ObjType::SUPER_REF) {
    auto *ust = static_cast<ObjSuperRef *>(obj);
    if (ust->ustSinif == nullptr) {
      calismaHatasi("'ust' referansi gecersiz.");
    }
    auto it = ust->ustSinif->metodlar.find(alanAdi);
    if (it == ust->ustSinif->metodlar.end()) {
      calismaHatasi("Ebeveyn sinifta '" + alanAdi + "' metodu bulunamadi.");
    }
    return Value::nesne(
        memory_.allocate<ObjBoundMethod>(ust->alici, it->second));
  }

  if (obj->type == ObjType::DICT) {
    auto *sozluk = static_cast<ObjDict *>(obj);
    auto it = sozluk->alanlar.find(alanAdi);
    if (it != sozluk->alanlar.end()) {
      return it->second;
    }
    calismaHatasi("Sozlukte '" + alanAdi + "' anahtari yok.");
  }

  if (obj->type == ObjType::INSTANCE) {
    auto *nesne = static_cast<ObjInstance *>(obj);
    if (auto it = nesne->alanlar.find(alanAdi); it != nesne->alanlar.end()) {
      return it->second;
    }
    if (nesne->sinif != nullptr) {
      if (auto mit = nesne->sinif->metodlar.find(alanAdi);
          mit != nesne->sinif->metodlar.end()) {
        return Value::nesne(
            memory_.allocate<ObjBoundMethod>(hedef, mit->second));
      }
    }
    calismaHatasi("Nesnede '" + alanAdi + "' alani bulunamadi.");
  }

  if (obj->type == ObjType::CLASS) {
    auto *sinif = static_cast<ObjClass *>(obj);
    if (auto it = sinif->metodlar.find(alanAdi); it != sinif->metodlar.end()) {
      return it->second;
    }
    if (auto it = sinif->alanVarsayilanlari.find(alanAdi);
        it != sinif->alanVarsayilanlari.end()) {
      return it->second;
    }
    calismaHatasi("Sinifta '" + alanAdi + "' alani bulunamadi.");
  }

  if (obj->type == ObjType::LIST) {
    if (alanAdi == "uzunluk") {
      return Value::nesne(memory_.allocate<ObjNative>(
          "liste.uzunluk", 0,
          [](VM &, const std::vector<Value> &args) -> Value {
            auto *self = static_cast<ObjList *>(args.at(0).as.nesne);
            return Value::sayi(static_cast<double>(self->ogeler.size()));
          },
          std::vector<Value>{hedef}));
    }
    if (alanAdi == "ekle") {
      return Value::nesne(memory_.allocate<ObjNative>(
          "liste.ekle", 1,
          [](VM &, const std::vector<Value> &args) -> Value {
            auto *self = static_cast<ObjList *>(args.at(0).as.nesne);
            self->ogeler.push_back(args.at(1));
            return Value::bos();
          },
          std::vector<Value>{hedef}));
    }
    calismaHatasi("Listede '" + alanAdi + "' metodu yok.");
  }

  if (obj->type == ObjType::STRING) {
    const std::string metin = static_cast<ObjString *>(obj)->deger;
    if (alanAdi == "buyuk") {
      return Value::nesne(memory_.allocate<ObjNative>(
          "metin.buyuk", 0,
          [metin](VM &vm, const std::vector<Value> &) -> Value {
            return vm.yeniString(asciiBuyut(metin));
          },
          std::vector<Value>{hedef}));
    }
    if (alanAdi == "kucuk") {
      return Value::nesne(memory_.allocate<ObjNative>(
          "metin.kucuk", 0,
          [metin](VM &vm, const std::vector<Value> &) -> Value {
            return vm.yeniString(asciiKucult(metin));
          },
          std::vector<Value>{hedef}));
    }
    calismaHatasi("Metinde '" + alanAdi + "' metodu yok.");
  }

  calismaHatasi("Bu tipte alan erisimi desteklenmiyor.");
}

Value VM::indeksAl(const Value &hedef, const Value &indeks) {
  if (!hedef.nesneMi() || hedef.as.nesne == nullptr) {
    calismaHatasi("Indeks erisimi yalnizca liste/sozluk/metinde yapilabilir.");
  }
  if (hedef.as.nesne->type == ObjType::LIST) {
    auto *liste = static_cast<ObjList *>(hedef.as.nesne);
    const double indeksSayi = indeks.type == ValueType::SAYI
                                  ? indeks.as.sayi
                                  : sayiyaCevir(indeks, "liste indeksi");
    const int i = static_cast<int>(std::llround(indeksSayi));
    if (i < 0 || static_cast<std::size_t>(i) >= liste->ogeler.size()) {
      calismaHatasi("Liste indeksi sinir disi.");
    }
    return liste->ogeler[static_cast<std::size_t>(i)];
  }
  if (hedef.as.nesne->type == ObjType::DICT) {
    auto *sozluk = static_cast<ObjDict *>(hedef.as.nesne);
    const std::string anahtar = anahtaraCevir(indeks, "sozluk indeksi");
    auto it = sozluk->alanlar.find(anahtar);
    if (it == sozluk->alanlar.end()) {
      calismaHatasi("Hata: '" + anahtar + "' anahtari sozlukte bulunamadi.");
    }
    return it->second;
  }
  if (hedef.as.nesne->type == ObjType::STRING) {
    const std::string &metin = static_cast<ObjString *>(hedef.as.nesne)->deger;
    const double indeksSayi = indeks.type == ValueType::SAYI
                                  ? indeks.as.sayi
                                  : sayiyaCevir(indeks, "metin indeksi");
    const int i = static_cast<int>(std::llround(indeksSayi));
    if (i < 0 || static_cast<std::size_t>(i) >= metin.size()) {
      calismaHatasi("Metin indeksi sinir disi.");
    }
    return yeniString(std::string(1, metin[static_cast<std::size_t>(i)]));
  }
  calismaHatasi("Bu tipte indeks erisimi desteklenmiyor.");
}

Value VM::cagir(const Value &cagrilan, const std::vector<Value> &argumanlar) {
  if (!cagrilan.nesneMi() || cagrilan.as.nesne == nullptr) {
    calismaHatasi("Cagrilan deger islev degil.");
  }
  if (cagrilan.as.nesne->type == ObjType::NATIVE) {
    auto *native = static_cast<ObjNative *>(cagrilan.as.nesne);
    if (native->arity >= 0 &&
        static_cast<int>(argumanlar.size()) != native->arity) {
      calismaHatasi("Arguman sayisi uyusmuyor: '" + native->ad + "'.");
    }
    if (native->bagliDegerler.empty()) {
      return native->fn(*this, argumanlar);
    }
    geciciBirlesikArgumanBuffer_.clear();
    geciciBirlesikArgumanBuffer_.reserve(native->bagliDegerler.size() +
                                         argumanlar.size());
    geciciBirlesikArgumanBuffer_.insert(geciciBirlesikArgumanBuffer_.end(),
                                        native->bagliDegerler.begin(),
                                        native->bagliDegerler.end());
    geciciBirlesikArgumanBuffer_.insert(geciciBirlesikArgumanBuffer_.end(),
                                        argumanlar.begin(), argumanlar.end());
    return native->fn(*this, geciciBirlesikArgumanBuffer_);
  }
  calismaHatasi("Bu deger cagrilamaz.");
}

Value VM::cagir(const Value &cagrilan, std::size_t argBaslangic,
                std::size_t argSayisi) {
  if (argBaslangic + argSayisi > yigin_.size()) {
    calismaHatasi("CAGIR: arguman araligi gecersiz.");
  }
  if (geciciArgumanBuffer_.capacity() < argSayisi) {
    geciciArgumanBuffer_.reserve(argSayisi);
  }
  geciciArgumanBuffer_.assign(
      yigin_.begin() + static_cast<std::ptrdiff_t>(argBaslangic),
      yigin_.begin() + static_cast<std::ptrdiff_t>(argBaslangic + argSayisi));
  return cagir(cagrilan, geciciArgumanBuffer_);
}

[[noreturn]] void VM::calismaHatasi(const std::string &mesaj) const {
  std::string tamMesaj = mesaj + "\n\nStack Trace:\n" + stackTraceUret();
  throw std::runtime_error(tamMesaj);
}

std::string VM::stackTraceUret() const {
  std::ostringstream ss;
  for (auto it = frameYigini_.rbegin(); it != frameYigini_.rend(); ++it) {
    const CallFrame &frame = *it;
    ObjFunction *fn = frame.function;
    if (fn == nullptr)
      continue;

    const BytecodeChunk *chunk = fn->chunk;
    std::size_t satir = 0;
    if (chunk != nullptr && frame.ip > 0 &&
        frame.ip - 1 < chunk->satirlar.size()) {
      satir = chunk->satirlar[frame.ip - 1];
    }

    std::string islevAdi = fn->ad.empty() ? "<script>" : fn->ad;
    ss << "  [satir " << satir << "] " << islevAdi << "\n";
  }
  return ss.str();
}
