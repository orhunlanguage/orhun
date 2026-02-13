
#include "VM.h"
#include "Compiler.h"
#include "Lexer.h"
#include "Parser.h"
#include "DynamicLibrary.h"
#include "Yerlesik.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
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
#include <ctime>

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
  std::transform(metin.begin(), metin.end(), metin.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return metin;
}

std::string asciiKucult(std::string metin) {
  std::transform(metin.begin(), metin.end(), metin.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return metin;
}

bool tamSayiMi(double d) {
  return std::isfinite(d) && std::floor(d) == d;
}

std::string zamanBicimlendir(const char* bicim, std::time_t zaman) {
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
                          const std::vector<std::intptr_t>& argumanlar) {
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
      return reinterpret_cast<std::intptr_t (*)(
          std::intptr_t, std::intptr_t, std::intptr_t)>(fonksiyon)(
          argumanlar[0], argumanlar[1], argumanlar[2]);
    case 4:
      return reinterpret_cast<std::intptr_t (*)(
          std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t)>(
          fonksiyon)(argumanlar[0], argumanlar[1], argumanlar[2], argumanlar[3]);
    case 5:
      return reinterpret_cast<std::intptr_t (*)(
          std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t,
          std::intptr_t)>(fonksiyon)(argumanlar[0], argumanlar[1], argumanlar[2],
                                     argumanlar[3], argumanlar[4]);
    case 6:
      return reinterpret_cast<std::intptr_t (*)(
          std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t,
          std::intptr_t, std::intptr_t)>(fonksiyon)(
          argumanlar[0], argumanlar[1], argumanlar[2], argumanlar[3],
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
          std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t)>(
          fonksiyon)(argumanlar[0], argumanlar[1], argumanlar[2], argumanlar[3],
                     argumanlar[4], argumanlar[5], argumanlar[6], argumanlar[7]);
    default:
      throw std::runtime_error(
          "ffi.cagir simdilik en fazla 8 arguman destekliyor.");
  }
}

yerlesik::JsonDeger vmDegerindenJsona(const Value& deger) {
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

  Obj* obj = deger.as.nesne;
  switch (obj->type) {
    case ObjType::STRING:
      return yerlesik::JsonDeger(static_cast<ObjString*>(obj)->deger);
    case ObjType::LIST: {
      yerlesik::JsonDeger::Liste liste;
      for (const Value& oge : static_cast<ObjList*>(obj)->ogeler) {
        liste.push_back(vmDegerindenJsona(oge));
      }
      return yerlesik::JsonDeger(std::move(liste));
    }
    case ObjType::DICT: {
      yerlesik::JsonDeger::Sozluk sozluk;
      for (const auto& [anahtar, alt] : static_cast<ObjDict*>(obj)->alanlar) {
        sozluk[anahtar] = vmDegerindenJsona(alt);
      }
      return yerlesik::JsonDeger(std::move(sozluk));
    }
    case ObjType::INSTANCE: {
      auto* nesne = static_cast<ObjInstance*>(obj);
      yerlesik::JsonDeger::Sozluk sozluk;
      if (nesne->sinif != nullptr) {
        sozluk["__sinif"] = yerlesik::JsonDeger(nesne->sinif->ad);
      }
      for (const auto& [anahtar, alt] : nesne->alanlar) {
        sozluk[anahtar] = vmDegerindenJsona(alt);
      }
      return yerlesik::JsonDeger(std::move(sozluk));
    }
    default:
      return yerlesik::JsonDeger(nullptr);
  }
}

Value jsondanVmDegerine(VM& vm, const yerlesik::JsonDeger& deger) {
  if (std::holds_alternative<std::nullptr_t>(deger.veri)) {
    return Value::bos();
  }
  if (const auto* m = std::get_if<bool>(&deger.veri)) {
    return Value::mantik(*m);
  }
  if (const auto* s = std::get_if<double>(&deger.veri)) {
    return Value::sayi(*s);
  }
  if (const auto* metin = std::get_if<std::string>(&deger.veri)) {
    return vm.yeniString(*metin);
  }
  if (const auto* listePtr = std::get_if<yerlesik::JsonDeger::ListePtr>(&deger.veri)) {
    std::vector<Value> liste;
    if (*listePtr) {
      liste.reserve((*listePtr)->size());
      for (const auto& oge : *(*listePtr)) {
        liste.push_back(jsondanVmDegerine(vm, oge));
      }
    }
    return vm.yeniListe(std::move(liste));
  }

  const auto* sozlukPtr = std::get_if<yerlesik::JsonDeger::SozlukPtr>(&deger.veri);
  std::unordered_map<std::string, Value> sozluk;
  if (sozlukPtr && *sozlukPtr) {
    for (const auto& [anahtar, alt] : *(*sozlukPtr)) {
      sozluk[anahtar] = jsondanVmDegerine(vm, alt);
    }
  }
  return vm.yeniSozluk(std::move(sozluk));
}

}  // namespace

VM::VM() { sifirla(); }

void VM::sifirla() {
  chunk_ = nullptr;
  frameYigini_.clear();
  bekleyenKurucular_.clear();
  tryYigini_.clear();
  yigin_.clear();
  globaller_.clear();
  gorevler_.clear();
  gorevSonrakiKimlik_ = 1;
  ffiKutuphaneleri_.clear();
  ffiKutuphaneKimlikleri_.clear();
  ffiSonrakiKimlik_ = 1;
  gcEsigi_ = 1024;
  yerlesikNativesYukle();
}

Value VM::yeniString(const std::string& metin) {
  return Value::nesne(memory_.allocate<ObjString>(metin));
}

Value VM::yeniListe(std::vector<Value> ogeler) {
  return Value::nesne(memory_.allocate<ObjList>(std::move(ogeler)));
}

Value VM::yeniSozluk(std::unordered_map<std::string, Value> alanlar) {
  return Value::nesne(memory_.allocate<ObjDict>(std::move(alanlar)));
}

Value VM::nativeOlustur(const std::string& ad, int arity, ObjNative::NativeFn fn) {
  return Value::nesne(memory_.allocate<ObjNative>(ad, arity, std::move(fn)));
}

void VM::yerlesikNativesYukle() {
  auto ekleNative = [&](const std::string& ad, int arity, ObjNative::NativeFn fn) {
    globaller_[ad] = nativeOlustur(ad, arity, std::move(fn));
  };

  ekleNative("yazdir", -1, [](VM& vm, const std::vector<Value>& args) -> Value {
    for (std::size_t i = 0; i < args.size(); ++i) {
      if (i > 0) {
        std::cout << ' ';
      }
      std::cout << vm.metneCevir(args[i]);
    }
    std::cout << '\n';
    return Value::bos();
  });

  ekleNative("sor", 1, [](VM& vm, const std::vector<Value>& args) -> Value {
    std::cout << vm.metneCevir(args[0]);
    std::cout.flush();
    std::string giris;
    std::getline(std::cin, giris);
    return vm.yeniString(giris);
  });

  ekleNative("karekok", 1, [](VM& vm, const std::vector<Value>& args) -> Value {
    return Value::sayi(std::sqrt(vm.sayiyaCevir(args[0], "karekok")));
  });
  ekleNative("us", 2, [](VM& vm, const std::vector<Value>& args) -> Value {
    return Value::sayi(
        std::pow(vm.sayiyaCevir(args[0], "us"), vm.sayiyaCevir(args[1], "us")));
  });

  static thread_local std::mt19937 rng{std::random_device{}()};
  ekleNative("rastgele", 2, [](VM& vm, const std::vector<Value>& args) -> Value {
    const int minV = static_cast<int>(std::llround(vm.sayiyaCevir(args[0], "rastgele")));
    const int maxV = static_cast<int>(std::llround(vm.sayiyaCevir(args[1], "rastgele")));
    if (minV > maxV) {
      throw std::runtime_error("rastgele(min,max) araligi gecersiz.");
    }
    std::uniform_int_distribution<int> dist(minV, maxV);
    return Value::sayi(static_cast<double>(dist(rng)));
  });

  ekleNative("zaman", 0, [](VM&, const std::vector<Value>&) -> Value {
    const auto s = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
    return Value::sayi(static_cast<double>(s));
  });

  ekleNative("bekle", 1, [](VM& vm, const std::vector<Value>& args) -> Value {
    const double s = vm.sayiyaCevir(args[0], "bekle");
    if (s > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(
          static_cast<int>(std::llround(s * 1000.0))));
    }
    return Value::bos();
  });

  ekleNative("buyuk_harf", 1, [](VM& vm, const std::vector<Value>& args) -> Value {
    return vm.yeniString(asciiBuyut(vm.metneCevir(args[0])));
  });
  ekleNative("kucuk_harf", 1, [](VM& vm, const std::vector<Value>& args) -> Value {
    return vm.yeniString(asciiKucult(vm.metneCevir(args[0])));
  });

  ekleNative("metne_cevir", 1, [](VM& vm, const std::vector<Value>& args) -> Value {
    return vm.yeniString(vm.metneCevir(args[0]));
  });
  ekleNative("sayiya_cevir", 1, [](VM& vm, const std::vector<Value>& args) -> Value {
    try {
      return Value::sayi(std::stod(vm.metneCevir(args[0])));
    } catch (...) {
      throw std::runtime_error("sayiya_cevir: gecersiz sayi.");
    }
  });
  ekleNative("icerir", 2, [](VM& vm, const std::vector<Value>& args) -> Value {
    const std::string kaynak = vm.metneCevir(args[0]);
    const std::string aranan = vm.metneCevir(args[1]);
    return Value::mantik(kaynak.find(aranan) != std::string::npos);
  });

  ekleNative("uzunluk", 1, [](VM&, const std::vector<Value>& args) -> Value {
    const Value& d = args[0];
    if (d.nesneMi() && d.as.nesne != nullptr) {
      if (d.as.nesne->type == ObjType::STRING) {
        return Value::sayi(static_cast<double>(
            static_cast<ObjString*>(d.as.nesne)->deger.size()));
      }
      if (d.as.nesne->type == ObjType::LIST) {
        return Value::sayi(static_cast<double>(
            static_cast<ObjList*>(d.as.nesne)->ogeler.size()));
      }
      if (d.as.nesne->type == ObjType::DICT) {
        return Value::sayi(static_cast<double>(
            static_cast<ObjDict*>(d.as.nesne)->alanlar.size()));
      }
    }
    throw std::runtime_error("uzunluk: metin/liste/sozluk bekleniyor.");
  });

  ekleNative("listeye_ekle", 2,
             [](VM&, const std::vector<Value>& args) -> Value {
               const Value& hedef = args[0];
               if (!hedef.nesneMi() || hedef.as.nesne == nullptr ||
                   hedef.as.nesne->type != ObjType::LIST) {
                 throw std::runtime_error("listeye_ekle: ilk arguman liste olmali.");
               }
               static_cast<ObjList*>(hedef.as.nesne)->ogeler.push_back(args[1]);
               return Value::bos();
             });

  ekleNative("dilim_al", 3, [](VM& vm, const std::vector<Value>& args) -> Value {
    const Value& hedef = args[0];
    const Value& basDeger = args[1];
    const Value& bitDeger = args[2];

    auto sinirCevir = [&vm](const Value& deger, long long varsayilan, long long uzunluk,
                            const std::string& baglam) -> long long {
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
      auto* liste = static_cast<ObjList*>(hedef.as.nesne);
      const long long uzunluk = static_cast<long long>(liste->ogeler.size());
      long long bas =
          sinirCevir(basDeger, 0, uzunluk, "liste dilim baslangici");
      long long bit = sinirCevir(bitDeger, uzunluk, uzunluk, "liste dilim bitisi");
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
      const std::string& metin = static_cast<ObjString*>(hedef.as.nesne)->deger;
      const long long uzunluk = static_cast<long long>(metin.size());
      long long bas =
          sinirCevir(basDeger, 0, uzunluk, "metin dilim baslangici");
      long long bit = sinirCevir(bitDeger, uzunluk, uzunluk, "metin dilim bitisi");
      if (bit < bas) {
        bit = bas;
      }

      return vm.yeniString(metin.substr(static_cast<std::size_t>(bas),
                                        static_cast<std::size_t>(bit - bas)));
    }

    throw std::runtime_error("dilim_al: hedef liste veya metin olmali.");
  });

  ekleNative("parcala", 2, [](VM& vm, const std::vector<Value>& args) -> Value {
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

  auto* matematik = memory_.allocate<ObjDict>();
  matematik->alanlar["pi"] = Value::sayi(3.14159265358979323846);
  matematik->alanlar["karekok"] = globaller_["karekok"];
  matematik->alanlar["us"] = globaller_["us"];
  matematik->alanlar["rastgele"] = globaller_["rastgele"];
  globaller_["matematik"] = Value::nesne(matematik);

  auto* dosya = memory_.allocate<ObjDict>();
  dosya->alanlar["oku"] = nativeOlustur(
      "dosya.oku", 1, [](VM& vm, const std::vector<Value>& a) -> Value {
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
      "dosya.yaz", 2, [](VM& vm, const std::vector<Value>& a) -> Value {
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
      "dosya.var_mi", 1, [](VM& vm, const std::vector<Value>& a) -> Value {
        const std::string yol = vm.metneCevir(a[0]);
        std::error_code ec;
        const bool var = std::filesystem::exists(yol, ec);
        if (ec) {
          throw std::runtime_error("dosya.var_mi: " + ec.message());
        }
        return Value::mantik(var);
      });
  dosya->alanlar["sil"] = nativeOlustur(
      "dosya.sil", 1, [](VM& vm, const std::vector<Value>& a) -> Value {
        const std::string yol = vm.metneCevir(a[0]);
        std::error_code ec;
        const bool silindi = std::filesystem::remove(yol, ec);
        if (ec) {
          throw std::runtime_error("dosya.sil: " + ec.message());
        }
        return Value::mantik(silindi);
      });
  dosya->alanlar["ekle_satir"] = nativeOlustur(
      "dosya.ekle_satir", 2, [](VM& vm, const std::vector<Value>& a) -> Value {
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
      "dosya.listele", 1, [](VM& vm, const std::vector<Value>& a) -> Value {
        const std::string yol = vm.metneCevir(a[0]);
        std::error_code ec;
        if (!std::filesystem::exists(yol, ec)) {
          if (ec) {
            throw std::runtime_error("dosya.listele: " + ec.message());
          }
          throw std::runtime_error("dosya.listele: klasor bulunamadi: " + yol);
        }
        std::vector<std::string> adlar;
        for (const auto& giris : std::filesystem::directory_iterator(yol, ec)) {
          if (ec) {
            throw std::runtime_error("dosya.listele: " + ec.message());
          }
          adlar.push_back(giris.path().filename().u8string());
        }
        std::sort(adlar.begin(), adlar.end());
        std::vector<Value> liste;
        liste.reserve(adlar.size());
        for (const auto& ad : adlar) {
          liste.push_back(vm.yeniString(ad));
        }
        return vm.yeniListe(std::move(liste));
      });
  dosya->alanlar["klasor_olustur"] = nativeOlustur(
      "dosya.klasor_olustur", 1,
      [](VM& vm, const std::vector<Value>& a) -> Value {
        const std::string yol = vm.metneCevir(a[0]);
        std::error_code ec;
        const bool olustu = std::filesystem::create_directories(yol, ec);
        if (ec) {
          throw std::runtime_error("dosya.klasor_olustur: " + ec.message());
        }
        return Value::mantik(olustu || std::filesystem::exists(yol));
      });
  globaller_["dosya"] = Value::nesne(dosya);

  auto* internet = memory_.allocate<ObjDict>();
  internet->alanlar["getir"] = nativeOlustur(
      "internet.getir", 1, [](VM& vm, const std::vector<Value>& a) -> Value {
        const std::string url = vm.metneCevir(a[0]);
        return vm.yeniString(yerlesik::internetIcerigiGetir(url));
      });
  internet->alanlar["indir"] = nativeOlustur(
      "internet.indir", 2, [](VM& vm, const std::vector<Value>& a) -> Value {
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

  auto* json = memory_.allocate<ObjDict>();
  json->alanlar["coz"] = nativeOlustur(
      "json.coz", 1, [](VM& vm, const std::vector<Value>& a) -> Value {
        try {
          const std::string metin = vm.metneCevir(a[0]);
          const yerlesik::JsonDeger json = yerlesik::jsonCoz(metin);
          return jsondanVmDegerine(vm, json);
        } catch (const std::exception& ex) {
          throw std::runtime_error("json.coz hatasi: " + std::string(ex.what()));
        }
      });
  json->alanlar["yaz"] = nativeOlustur(
      "json.yaz", 1, [](VM& vm, const std::vector<Value>& a) -> Value {
        try {
          const yerlesik::JsonDeger json = vmDegerindenJsona(a[0]);
          return vm.yeniString(yerlesik::jsonYaz(json, false, 2));
        } catch (const std::exception& ex) {
          throw std::runtime_error("json.yaz hatasi: " + std::string(ex.what()));
        }
      });
  json->alanlar["guzel_yaz"] = nativeOlustur(
      "json.guzel_yaz", -1, [](VM& vm, const std::vector<Value>& a) -> Value {
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
        } catch (const std::exception& ex) {
          throw std::runtime_error("json.guzel_yaz hatasi: " + std::string(ex.what()));
        }
      });
  globaller_["json"] = Value::nesne(json);

  auto* metin = memory_.allocate<ObjDict>();
  metin->alanlar["buyuk"] = nativeOlustur(
      "metin.buyuk", 1, [](VM& vm, const std::vector<Value>& a) -> Value {
        return vm.yeniString(asciiBuyut(vm.metneCevir(a[0])));
      });
  metin->alanlar["kucuk"] = nativeOlustur(
      "metin.kucuk", 1, [](VM& vm, const std::vector<Value>& a) -> Value {
        return vm.yeniString(asciiKucult(vm.metneCevir(a[0])));
      });
  metin->alanlar["uzunluk"] = nativeOlustur(
      "metin.uzunluk", 1, [](VM& vm, const std::vector<Value>& a) -> Value {
        return Value::sayi(static_cast<double>(vm.metneCevir(a[0]).size()));
      });
  metin->alanlar["icerir"] = nativeOlustur(
      "metin.icerir", 2, [](VM& vm, const std::vector<Value>& a) -> Value {
        const std::string kaynak = vm.metneCevir(a[0]);
        const std::string aranan = vm.metneCevir(a[1]);
        return Value::mantik(kaynak.find(aranan) != std::string::npos);
      });
  metin->alanlar["parcala"] = globaller_["parcala"];
  metin->alanlar["birlestir"] = nativeOlustur(
      "metin.birlestir", 2, [](VM& vm, const std::vector<Value>& a) -> Value {
        if (!a[0].nesneMi() || a[0].as.nesne == nullptr ||
            a[0].as.nesne->type != ObjType::LIST) {
          throw std::runtime_error("metin.birlestir ilk argumanda liste bekler.");
        }
        const std::string ayirici = vm.metneCevir(a[1]);
        const auto* liste = static_cast<ObjList*>(a[0].as.nesne);
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

  auto* regex = memory_.allocate<ObjDict>();
  regex->alanlar["eslesir"] = nativeOlustur(
      "regex.eslesir", 2, [](VM& vm, const std::vector<Value>& a) -> Value {
        try {
          const std::regex desen(vm.metneCevir(a[1]));
          return Value::mantik(
              std::regex_search(vm.metneCevir(a[0]), desen));
        } catch (const std::regex_error& ex) {
          throw std::runtime_error("regex.eslesir deseni gecersiz: " +
                                   std::string(ex.what()));
        }
      });
  regex->alanlar["ilk"] = nativeOlustur(
      "regex.ilk", 2, [](VM& vm, const std::vector<Value>& a) -> Value {
        try {
          const std::regex desen(vm.metneCevir(a[1]));
          std::smatch sonuc;
          const std::string kaynak = vm.metneCevir(a[0]);
          if (!std::regex_search(kaynak, sonuc, desen)) {
            return vm.yeniString("");
          }
          return vm.yeniString(sonuc.str(0));
        } catch (const std::regex_error& ex) {
          throw std::runtime_error("regex.ilk deseni gecersiz: " +
                                   std::string(ex.what()));
        }
      });
  regex->alanlar["tum"] = nativeOlustur(
      "regex.tum", 2, [](VM& vm, const std::vector<Value>& a) -> Value {
        try {
          const std::regex desen(vm.metneCevir(a[1]));
          const std::string kaynak = vm.metneCevir(a[0]);
          std::vector<Value> liste;
          for (std::sregex_iterator it(kaynak.begin(), kaynak.end(), desen), son;
               it != son; ++it) {
            liste.push_back(vm.yeniString((*it).str(0)));
          }
          return vm.yeniListe(std::move(liste));
        } catch (const std::regex_error& ex) {
          throw std::runtime_error("regex.tum deseni gecersiz: " +
                                   std::string(ex.what()));
        }
      });
  regex->alanlar["degistir"] = nativeOlustur(
      "regex.degistir", 3, [](VM& vm, const std::vector<Value>& a) -> Value {
        try {
          const std::regex desen(vm.metneCevir(a[1]));
          return vm.yeniString(std::regex_replace(
              vm.metneCevir(a[0]), desen, vm.metneCevir(a[2])));
        } catch (const std::regex_error& ex) {
          throw std::runtime_error("regex.degistir deseni gecersiz: " +
                                   std::string(ex.what()));
        }
      });
  globaller_["regex"] = Value::nesne(regex);

  auto* tarih = memory_.allocate<ObjDict>();
  tarih->alanlar["simdi"] = nativeOlustur(
      "tarih.simdi", 0, [](VM& vm, const std::vector<Value>&) -> Value {
        const std::time_t t = std::time(nullptr);
        return vm.yeniString(zamanBicimlendir("%Y-%m-%d %H:%M:%S", t));
      });
  tarih->alanlar["bugun"] = nativeOlustur(
      "tarih.bugun", 0, [](VM& vm, const std::vector<Value>&) -> Value {
        const std::time_t t = std::time(nullptr);
        return vm.yeniString(zamanBicimlendir("%Y-%m-%d", t));
      });
  tarih->alanlar["unix"] = nativeOlustur(
      "tarih.unix", 0, [](VM&, const std::vector<Value>&) -> Value {
        return Value::sayi(static_cast<double>(std::time(nullptr)));
      });
  globaller_["tarih"] = Value::nesne(tarih);

  auto* veritabani = memory_.allocate<ObjDict>();
  veritabani->alanlar["kaydet"] = nativeOlustur(
      "veritabani.kaydet", -1, [](VM& vm, const std::vector<Value>& a) -> Value {
        if (a.size() != 2 && a.size() != 3) {
          throw std::runtime_error(
              "veritabani.kaydet(anahtar, deger, [dosya]) iki veya uc arguman alir.");
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
                tablo = static_cast<ObjDict*>(kok.as.nesne)->alanlar;
              }
            }
          }
        }

        tablo[anahtar] = a[1];
        yerlesik::JsonDeger::Sozluk jsonSozluk;
        for (const auto& [k, v] : tablo) {
          jsonSozluk[k] = vmDegerindenJsona(v);
        }
        const std::string jsonMetin =
            yerlesik::jsonYaz(yerlesik::JsonDeger(std::move(jsonSozluk)), true, 2);
        std::ofstream out(yol, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
          throw std::runtime_error("veritabani.kaydet: yazilamadi: " + yol);
        }
        out << jsonMetin;
        return Value::mantik(true);
      });
  veritabani->alanlar["oku"] = nativeOlustur(
      "veritabani.oku", -1, [](VM& vm, const std::vector<Value>& a) -> Value {
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
          throw std::runtime_error("veritabani.oku: gecersiz veritabani formati.");
        }
        auto* sozluk = static_cast<ObjDict*>(kok.as.nesne);
        const auto it = sozluk->alanlar.find(anahtar);
        if (it == sozluk->alanlar.end()) {
          throw std::runtime_error("veritabani.oku: anahtar bulunamadi: " + anahtar);
        }
        return it->second;
      });
  veritabani->alanlar["listele"] = nativeOlustur(
      "veritabani.listele", -1,
      [](VM& vm, const std::vector<Value>& a) -> Value {
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
        auto* sozluk = static_cast<ObjDict*>(kok.as.nesne);
        std::vector<Value> anahtarlar;
        anahtarlar.reserve(sozluk->alanlar.size());
        for (const auto& [k, _] : sozluk->alanlar) {
          anahtarlar.push_back(vm.yeniString(k));
        }
        return vm.yeniListe(std::move(anahtarlar));
      });
  veritabani->alanlar["sil"] = nativeOlustur(
      "veritabani.sil", -1, [](VM& vm, const std::vector<Value>& a) -> Value {
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
            tablo = static_cast<ObjDict*>(kok.as.nesne)->alanlar;
          }
        }

        const bool silindi = tablo.erase(anahtar) > 0;
        yerlesik::JsonDeger::Sozluk jsonSozluk;
        for (const auto& [k, v] : tablo) {
          jsonSozluk[k] = vmDegerindenJsona(v);
        }
        std::ofstream out(yol, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
          throw std::runtime_error("veritabani.sil: yazilamadi: " + yol);
        }
        out << yerlesik::jsonYaz(yerlesik::JsonDeger(std::move(jsonSozluk)), true,
                                 2);
        return Value::mantik(silindi);
      });
  globaller_["veritabani"] = Value::nesne(veritabani);

  auto* sunucu = memory_.allocate<ObjDict>();
  sunucu->alanlar["baslat"] = nativeOlustur(
      "sunucu.baslat", -1, [](VM& vm, const std::vector<Value>& a) -> Value {
        if (a.empty() || a.size() > 2) {
          throw std::runtime_error(
              "sunucu.baslat(port, [\"klasor\"]) bir veya iki arguman alir.");
        }
        const double portDegeri = vm.sayiyaCevir(a[0], "sunucu.baslat");
        if (!tamSayiMi(portDegeri)) {
          throw std::runtime_error("sunucu.baslat icin port tam sayi olmalidir.");
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
      "sunucu.durdur", 0, [](VM&, const std::vector<Value>&) -> Value {
        yerlesik::paylasimliHttpSunucu().durdur();
        return Value::mantik(true);
      });
  sunucu->alanlar["calisiyor_mu"] = nativeOlustur(
      "sunucu.calisiyor_mu", 0, [](VM&, const std::vector<Value>&) -> Value {
        return Value::mantik(yerlesik::paylasimliHttpSunucu().calisiyorMu());
      });
  globaller_["sunucu"] = Value::nesne(sunucu);

  auto* sistem = memory_.allocate<ObjDict>();
  sistem->alanlar["komut"] = nativeOlustur(
      "sistem.komut", 1, [](VM& vm, const std::vector<Value>& a) -> Value {
        const std::string komut = vm.metneCevir(a[0]);
        const int cikis = std::system(komut.c_str());
        if (cikis == -1) {
          throw std::runtime_error("sistem.komut calistirilamadi.");
        }
        return Value::sayi(static_cast<double>(cikis));
      });
  globaller_["sistem"] = Value::nesne(sistem);

  auto* gorev = memory_.allocate<ObjDict>();
  gorev->alanlar["baslat_bekle"] = nativeOlustur(
      "gorev.baslat_bekle", 1, [](VM& vm, const std::vector<Value>& a) -> Value {
        const double saniye = vm.sayiyaCevir(a[0], "gorev.baslat_bekle");
        if (!std::isfinite(saniye) || saniye < 0.0) {
          throw std::runtime_error("gorev.baslat_bekle icin sifirdan buyuk sayi beklenir.");
        }
        const int kimlik = vm.gorevSonrakiKimlik_++;
        VM::GorevKaydi kayit{
            std::async(std::launch::async, [saniye]() -> double {
              const auto ms =
                  std::chrono::milliseconds(static_cast<int>(std::llround(saniye * 1000.0)));
              if (ms.count() > 0) {
                std::this_thread::sleep_for(ms);
              }
              return 1.0;
            })};
        vm.gorevler_.emplace(kimlik, std::move(kayit));
        return Value::sayi(static_cast<double>(kimlik));
      });
  gorev->alanlar["baslat_komut"] = nativeOlustur(
      "gorev.baslat_komut", 1, [](VM& vm, const std::vector<Value>& a) -> Value {
        const std::string komut = vm.metneCevir(a[0]);
        const int kimlik = vm.gorevSonrakiKimlik_++;
        VM::GorevKaydi kayit{
            std::async(std::launch::async, [komut]() -> double {
              const int cikis = std::system(komut.c_str());
              return static_cast<double>(cikis);
            })};
        vm.gorevler_.emplace(kimlik, std::move(kayit));
        return Value::sayi(static_cast<double>(kimlik));
      });
  gorev->alanlar["tamamlandi_mi"] = nativeOlustur(
      "gorev.tamamlandi_mi", 1, [](VM& vm, const std::vector<Value>& a) -> Value {
        const double d = vm.sayiyaCevir(a[0], "gorev.tamamlandi_mi");
        if (!tamSayiMi(d)) {
          throw std::runtime_error("gorev.tamamlandi_mi icin gorev kimligi tam sayi olmalidir.");
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
            it->second.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
          it->second.sonuc = it->second.future.get();
          it->second.sonucHazir = true;
          return Value::mantik(true);
        }
        return Value::mantik(false);
      });
  gorev->alanlar["bekle"] = nativeOlustur(
      "gorev.bekle", 1, [](VM& vm, const std::vector<Value>& a) -> Value {
        const double d = vm.sayiyaCevir(a[0], "gorev.bekle");
        if (!tamSayiMi(d)) {
          throw std::runtime_error("gorev.bekle icin gorev kimligi tam sayi olmalidir.");
        }
        const int kimlik = static_cast<int>(std::llround(d));
        const auto it = vm.gorevler_.find(kimlik);
        if (it == vm.gorevler_.end()) {
          throw std::runtime_error("gorev.bekle: gecersiz gorev kimligi.");
        }
        if (!it->second.sonucHazir) {
          if (!it->second.future.valid()) {
            throw std::runtime_error("gorev.bekle: gorev sonucu kullanilamaz durumda.");
          }
          it->second.sonuc = it->second.future.get();
          it->second.sonucHazir = true;
        }
        return Value::sayi(it->second.sonuc);
      });
  gorev->alanlar["hepsi_bekle"] = nativeOlustur(
      "gorev.hepsi_bekle", 1, [](VM& vm, const std::vector<Value>& a) -> Value {
        if (!a[0].nesneMi() || a[0].as.nesne == nullptr ||
            a[0].as.nesne->type != ObjType::LIST) {
          throw std::runtime_error("gorev.hepsi_bekle icin gorev kimlikleri listesi beklenir.");
        }
        const auto* liste = static_cast<ObjList*>(a[0].as.nesne);
        std::vector<Value> sonuc;
        sonuc.reserve(liste->ogeler.size());
        for (const Value& oge : liste->ogeler) {
          const double d = vm.sayiyaCevir(oge, "gorev.hepsi_bekle");
          if (!tamSayiMi(d)) {
            throw std::runtime_error(
                "gorev.hepsi_bekle listesindeki tum kimlikler tam sayi olmalidir.");
          }
          const int kimlik = static_cast<int>(std::llround(d));
          const auto it = vm.gorevler_.find(kimlik);
          if (it == vm.gorevler_.end()) {
            throw std::runtime_error("gorev.hepsi_bekle: gecersiz gorev kimligi.");
          }
          if (!it->second.sonucHazir) {
            if (!it->second.future.valid()) {
              throw std::runtime_error("gorev.hepsi_bekle: gorev sonucu kullanilamaz durumda.");
            }
            it->second.sonuc = it->second.future.get();
            it->second.sonucHazir = true;
          }
          sonuc.push_back(Value::sayi(it->second.sonuc));
        }
        return vm.yeniListe(std::move(sonuc));
      });
  globaller_["gorev"] = Value::nesne(gorev);

  auto* ffi = memory_.allocate<ObjDict>();
  ffi->alanlar["yukle"] = nativeOlustur(
      "ffi.yukle", 1, [](VM& vm, const std::vector<Value>& a) -> Value {
        if (a.size() != 1) {
          throw std::runtime_error("ffi.yukle(\"kutuphane\") tek arguman alir.");
        }
        if (!objTipiMi(a[0], ObjType::STRING)) {
          throw std::runtime_error("ffi.yukle icin metin kutuphane yolu bekleniyor.");
        }

        const std::string yol = static_cast<ObjString*>(a[0].as.nesne)->deger;
        const auto mevcut = vm.ffiKutuphaneKimlikleri_.find(yol);
        if (mevcut != vm.ffiKutuphaneKimlikleri_.end()) {
          return Value::sayi(static_cast<double>(mevcut->second));
        }

        auto kutuphane = std::make_shared<runtime::DynamicLibrary>(yol);
        std::string hata;
        if (!kutuphane->load(&hata)) {
          throw std::runtime_error("ffi.yukle basarisiz: '" + yol + "' (" + hata + ")");
        }

        const int kimlik = vm.ffiSonrakiKimlik_++;
        vm.ffiKutuphaneleri_[kimlik] = std::move(kutuphane);
        vm.ffiKutuphaneKimlikleri_[yol] = kimlik;
        return Value::sayi(static_cast<double>(kimlik));
      });
  ffi->alanlar["cagir"] = nativeOlustur(
      "ffi.cagir", -1, [](VM& vm, const std::vector<Value>& a) -> Value {
        if (a.size() < 2) {
          throw std::runtime_error(
              "ffi.cagir(tutamac, \"fonksiyon\", ...argumanlar) en az 2 arguman alir.");
        }
        if (!objTipiMi(a[1], ObjType::STRING)) {
          throw std::runtime_error("ffi.cagir icinde fonksiyon adi metin olmalidir.");
        }

        int kimlik = 0;
        if (a[0].sayiMi()) {
          if (!tamSayiMi(a[0].as.sayi)) {
            throw std::runtime_error("ffi.cagir icin tutamac tam sayi olmalidir.");
          }
          kimlik = static_cast<int>(std::llround(a[0].as.sayi));
        } else if (objTipiMi(a[0], ObjType::STRING)) {
          const std::string yol = static_cast<ObjString*>(a[0].as.nesne)->deger;
          const auto mevcut = vm.ffiKutuphaneKimlikleri_.find(yol);
          if (mevcut != vm.ffiKutuphaneKimlikleri_.end()) {
            kimlik = mevcut->second;
          } else {
            auto kutuphane = std::make_shared<runtime::DynamicLibrary>(yol);
            std::string hata;
            if (!kutuphane->load(&hata)) {
              throw std::runtime_error("ffi.cagir: '" + yol +
                                       "' kutuphanesi yuklenemedi (" + hata + ")");
            }
            kimlik = vm.ffiSonrakiKimlik_++;
            vm.ffiKutuphaneleri_[kimlik] = std::move(kutuphane);
            vm.ffiKutuphaneKimlikleri_[yol] = kimlik;
          }
        } else {
          throw std::runtime_error(
              "ffi.cagir icin ilk arguman tutamac(int) veya kutuphane adi(metin) olmalidir.");
        }

        const auto itKutuphane = vm.ffiKutuphaneleri_.find(kimlik);
        if (itKutuphane == vm.ffiKutuphaneleri_.end() || !itKutuphane->second ||
            !itKutuphane->second->isLoaded()) {
          throw std::runtime_error("ffi.cagir: gecersiz kutuphane tutamaci #" +
                                   std::to_string(kimlik));
        }

        const std::string fonksiyonAdi = static_cast<ObjString*>(a[1].as.nesne)->deger;
        std::string sembolHatasi;
        const std::uintptr_t fonksiyon =
            itKutuphane->second->getSymbol(fonksiyonAdi, &sembolHatasi);
        if (fonksiyon == 0) {
          throw std::runtime_error("ffi.cagir: '" + fonksiyonAdi +
                                   "' sembolu bulunamadi (" + sembolHatasi + ")");
        }

        std::vector<std::string> metinSahipligi;
        std::vector<std::intptr_t> hamArgumanlar;
        metinSahipligi.reserve(a.size() > 2 ? a.size() - 2 : 0);
        hamArgumanlar.reserve(a.size() > 2 ? a.size() - 2 : 0);

        for (std::size_t i = 2; i < a.size(); ++i) {
          if (a[i].sayiMi()) {
            if (!tamSayiMi(a[i].as.sayi)) {
              throw std::runtime_error("ffi.cagir ondalik argumanlari desteklemiyor (arg #" +
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
            metinSahipligi.push_back(static_cast<ObjString*>(a[i].as.nesne)->deger);
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
      "ffi.cagir_metin", -1, [](VM& vm, const std::vector<Value>& a) -> Value {
        if (a.size() < 2) {
          throw std::runtime_error(
              "ffi.cagir_metin(tutamac, \"fonksiyon\", ...argumanlar) en az 2 arguman alir.");
        }
        if (!objTipiMi(a[1], ObjType::STRING)) {
          throw std::runtime_error("ffi.cagir_metin icinde fonksiyon adi metin olmalidir.");
        }

        int kimlik = 0;
        if (a[0].sayiMi()) {
          if (!tamSayiMi(a[0].as.sayi)) {
            throw std::runtime_error("ffi.cagir_metin icin tutamac tam sayi olmalidir.");
          }
          kimlik = static_cast<int>(std::llround(a[0].as.sayi));
        } else if (objTipiMi(a[0], ObjType::STRING)) {
          const std::string yol = static_cast<ObjString*>(a[0].as.nesne)->deger;
          const auto mevcut = vm.ffiKutuphaneKimlikleri_.find(yol);
          if (mevcut != vm.ffiKutuphaneKimlikleri_.end()) {
            kimlik = mevcut->second;
          } else {
            auto kutuphane = std::make_shared<runtime::DynamicLibrary>(yol);
            std::string hata;
            if (!kutuphane->load(&hata)) {
              throw std::runtime_error("ffi.cagir_metin: '" + yol +
                                       "' kutuphanesi yuklenemedi (" + hata + ")");
            }
            kimlik = vm.ffiSonrakiKimlik_++;
            vm.ffiKutuphaneleri_[kimlik] = std::move(kutuphane);
            vm.ffiKutuphaneKimlikleri_[yol] = kimlik;
          }
        } else {
          throw std::runtime_error(
              "ffi.cagir_metin icin ilk arguman tutamac(int) veya kutuphane adi(metin) olmalidir.");
        }

        const auto itKutuphane = vm.ffiKutuphaneleri_.find(kimlik);
        if (itKutuphane == vm.ffiKutuphaneleri_.end() || !itKutuphane->second ||
            !itKutuphane->second->isLoaded()) {
          throw std::runtime_error("ffi.cagir_metin: gecersiz kutuphane tutamaci #" +
                                   std::to_string(kimlik));
        }

        const std::string fonksiyonAdi = static_cast<ObjString*>(a[1].as.nesne)->deger;
        std::string sembolHatasi;
        const std::uintptr_t fonksiyon =
            itKutuphane->second->getSymbol(fonksiyonAdi, &sembolHatasi);
        if (fonksiyon == 0) {
          throw std::runtime_error("ffi.cagir_metin: '" + fonksiyonAdi +
                                   "' sembolu bulunamadi (" + sembolHatasi + ")");
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
            metinSahipligi.push_back(static_cast<ObjString*>(a[i].as.nesne)->deger);
            hamArgumanlar.push_back(
                reinterpret_cast<std::intptr_t>(metinSahipligi.back().c_str()));
            continue;
          }
          throw std::runtime_error(
              "ffi.cagir_metin sadece int/bool/metin argumanlarini destekliyor.");
        }

        const std::intptr_t donus = ffiHamCagir(fonksiyon, hamArgumanlar);
        if (donus == 0) {
          return vm.yeniString("");
        }
        return vm.yeniString(std::string(reinterpret_cast<const char*>(donus)));
      });
  ffi->alanlar["sembol_var_mi"] = nativeOlustur(
      "ffi.sembol_var_mi", 2, [](VM& vm, const std::vector<Value>& a) -> Value {
        int kimlik = 0;
        if (a[0].sayiMi()) {
          if (!tamSayiMi(a[0].as.sayi)) {
            return Value::mantik(false);
          }
          kimlik = static_cast<int>(std::llround(a[0].as.sayi));
        } else if (objTipiMi(a[0], ObjType::STRING)) {
          const std::string yol = static_cast<ObjString*>(a[0].as.nesne)->deger;
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
        const std::string sembol = static_cast<ObjString*>(a[1].as.nesne)->deger;
        return Value::mantik(itKutuphane->second->getSymbol(sembol, nullptr) != 0);
      });
  ffi->alanlar["bosalt"] = nativeOlustur(
      "ffi.bosalt", 1, [](VM& vm, const std::vector<Value>& a) -> Value {
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
  globaller_["ffi"] = Value::nesne(ffi);

  ekleNative("dahil_et", 1, [](VM& vm, const std::vector<Value>& args) -> Value {
    const std::string yol = vm.metneCevir(args[0]);
    std::ifstream in(yol, std::ios::binary);
    if (!in.is_open()) {
      throw std::runtime_error("dahil_et: dosya acilamadi: " + yol);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string kaynak = ss.str();

    Lexer lexer(kaynak);
    std::vector<Token> tokenlar = lexer.tokenize();
    Parser parser(std::move(tokenlar));
    std::unique_ptr<ProgramNode> program = parser.parse();
    Compiler derleyici;
    BytecodeChunk chunk = derleyici.derle(program.get());

    auto modulVm = std::make_shared<VM>();
    std::unordered_set<std::string> yerlesikAdlar;
    for (const auto& [ad, _] : modulVm->globaller_) {
      yerlesikAdlar.insert(ad);
    }
    modulVm->calistir(chunk);

    auto* modul = vm.memory_.allocate<ObjDict>();
    for (const auto& [ad, deger] : modulVm->globaller_) {
      if (yerlesikAdlar.find(ad) != yerlesikAdlar.end()) {
        continue;
      }
      if (deger.nesneMi() && deger.as.nesne != nullptr) {
        const ObjType tip = deger.as.nesne->type;
        if (tip == ObjType::FUNCTION || tip == ObjType::NATIVE ||
            tip == ObjType::CLASS || tip == ObjType::BOUND_METHOD ||
            tip == ObjType::SUPER_REF) {
          // VM'ler arasi islev/sinif tasima henuz yok; veri odakli export.
          continue;
        }
      }
      modul->alanlar[ad] = jsondanVmDegerine(vm, vmDegerindenJsona(deger));
    }

    return Value::nesne(modul);
  });
}

void VM::gcGerekirseCalistir() {
  if (memory_.objectCount() >= gcEsigi_) {
    gcCalistir();
    gcEsigi_ = std::max<std::size_t>(1024, memory_.objectCount() * 2);
  }
}

void VM::gcCalistir() {
  memory_.collectGarbage([this](MemoryManager& mem) {
    for (const Value& deger : yigin_) {
      mem.markValue(deger);
    }
    for (const auto& [ad, deger] : globaller_) {
      (void)ad;
      mem.markValue(deger);
    }
    for (const CallFrame& frame : frameYigini_) {
      mem.markObject(frame.function);
    }
    for (const BekleyenKurucu& b : bekleyenKurucular_) {
      mem.markValue(b.olusanNesne);
    }
  });
}

void VM::pushFrame(ObjFunction* fn, std::size_t slotBase) {
  CallFrame frame;
  frame.function = fn;
  frame.ip = fn->girisIp;
  frame.slotBase = slotBase;
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

  const std::size_t cagribaslangic = frame.slotBase - 1;
  if (cagribaslangic > yigin_.size()) {
    calismaHatasi("Ic hata: gecersiz call stack temizligi.");
  }
  yigin_.resize(cagribaslangic);
  yiginPush(donusDegeri);
}

void VM::islevCagrisiHazirla(std::size_t calleeIndex, std::uint16_t argc,
                             ObjFunction* fn) {
  if (static_cast<int>(argc) != fn->arity) {
    calismaHatasi("Arguman sayisi uyusmuyor: '" + fn->ad + "'.");
  }
  const std::size_t slotBase = calleeIndex + 1;
  const std::size_t hedefBoyut = slotBase + fn->localSayisi;
  while (yigin_.size() < hedefBoyut) {
    yiginPush(Value::bos());
  }
  pushFrame(fn, slotBase);
}

void VM::calistir(const BytecodeChunk& chunk) {
  chunk_ = &chunk;
  yigin_.clear();
  frameYigini_.clear();
  bekleyenKurucular_.clear();
  tryYigini_.clear();

  auto* kok = memory_.allocate<ObjFunction>("<program>", 0, &chunk, 0, 0);
  pushFrame(kok, 0);

  while (!frameYigini_.empty()) {
    try {
      const OpCode op = static_cast<OpCode>(byteOku());
      switch (op) {
      case OpCode::OP_SABIT:
        yiginPush(sabitDegeriniRuntimeaCevir(sabitOku(u16Oku())));
        break;
      case OpCode::OP_BOS:
        yiginPush(Value::bos());
        break;
      case OpCode::OP_DOGRU:
        yiginPush(Value::mantik(true));
        break;
      case OpCode::OP_YANLIS:
        yiginPush(Value::mantik(false));
        break;
      case OpCode::OP_POP:
        (void)yiginPop();
        break;
      case OpCode::OP_KOPYA:
        yiginPush(yiginBak(0));
        break;
      case OpCode::OP_GET_LOCAL: {
        const std::uint16_t idx = u16Oku();
        yiginPush(localEris(idx));
        break;
      }
      case OpCode::OP_SET_LOCAL: {
        const std::uint16_t idx = u16Oku();
        localEris(idx) = yiginBak(0);
        break;
      }
      case OpCode::OP_GET_GLOBAL: {
        const SabitDeger& adDegeri = sabitOku(u16Oku());
        if (!std::holds_alternative<std::string>(adDegeri.veri)) {
          calismaHatasi("GET_GLOBAL sabiti metin degil.");
        }
        const std::string& ad = std::get<std::string>(adDegeri.veri);
        const auto it = globaller_.find(ad);
        if (it == globaller_.end()) {
          calismaHatasi("Tanimsiz degisken: '" + ad + "'.");
        }
        yiginPush(it->second);
        break;
      }
      case OpCode::OP_SET_GLOBAL: {
        const SabitDeger& adDegeri = sabitOku(u16Oku());
        if (!std::holds_alternative<std::string>(adDegeri.veri)) {
          calismaHatasi("SET_GLOBAL sabiti metin degil.");
        }
        globaller_[std::get<std::string>(adDegeri.veri)] = yiginBak(0);
        break;
      }
      case OpCode::OP_ALAN_AL: {
        const SabitDeger& adDegeri = sabitOku(u16Oku());
        if (!std::holds_alternative<std::string>(adDegeri.veri)) {
          calismaHatasi("ALAN_AL sabiti metin degil.");
        }
        const Value hedef = yiginPop();
        yiginPush(alanAl(hedef, std::get<std::string>(adDegeri.veri)));
        break;
      }
      case OpCode::OP_ALAN_YAZ:
      case OpCode::OP_METOD_YAZ: {
        const SabitDeger& adDegeri = sabitOku(u16Oku());
        if (!std::holds_alternative<std::string>(adDegeri.veri)) {
          calismaHatasi("ALAN/METOD sabiti metin degil.");
        }
        const std::string& ad = std::get<std::string>(adDegeri.veri);
        const Value deger = yiginPop();
        const Value hedef = yiginPop();
        if (!hedef.nesneMi() || hedef.as.nesne == nullptr) {
          calismaHatasi("Alan atamasi yalnizca nesnelerde yapilabilir.");
        }
        Obj* obj = hedef.as.nesne;
        if (obj->type == ObjType::DICT) {
          static_cast<ObjDict*>(obj)->alanlar[ad] = deger;
        } else if (obj->type == ObjType::INSTANCE) {
          static_cast<ObjInstance*>(obj)->alanlar[ad] = deger;
        } else if (obj->type == ObjType::CLASS) {
          auto* sinif = static_cast<ObjClass*>(obj);
          if (op == OpCode::OP_METOD_YAZ) {
            sinif->metodlar[ad] = deger;
          } else {
            sinif->alanVarsayilanlari[ad] = deger;
          }
        } else {
          calismaHatasi("Bu tipte alan atamasi desteklenmiyor.");
        }
        yiginPush(deger);
        break;
      }
      case OpCode::OP_INDEKS_AL: {
        const Value indeks = yiginPop();
        const Value hedef = yiginPop();
        yiginPush(indeksAl(hedef, indeks));
        break;
      }
      case OpCode::OP_INDEKS_YAZ: {
        const Value deger = yiginPop();
        const Value indeks = yiginPop();
        const Value hedef = yiginPop();
        if (!hedef.nesneMi() || hedef.as.nesne == nullptr) {
          calismaHatasi("Indeks atamasi yalnizca liste/sozlukte yapilabilir.");
        }
        if (hedef.as.nesne->type == ObjType::LIST) {
          auto* liste = static_cast<ObjList*>(hedef.as.nesne);
          const int i = static_cast<int>(std::llround(sayiyaCevir(indeks, "liste atama")));
          if (i < 0 || static_cast<std::size_t>(i) >= liste->ogeler.size()) {
            calismaHatasi("Liste indeksi sinir disi.");
          }
          liste->ogeler[static_cast<std::size_t>(i)] = deger;
        } else if (hedef.as.nesne->type == ObjType::DICT) {
          static_cast<ObjDict*>(hedef.as.nesne)
              ->alanlar[anahtaraCevir(indeks, "sozluk atama")] = deger;
        } else {
          calismaHatasi("Bu tipte indeks atamasi desteklenmiyor.");
        }
        yiginPush(deger);
        break;
      }
      case OpCode::OP_LISTE_OLUSTUR: {
        const std::uint16_t adet = u16Oku();
        if (adet > yigin_.size()) {
          calismaHatasi("LISTE_OLUSTUR: yigin yetersiz.");
        }
        std::vector<Value> ogeler(adet);
        for (std::size_t i = 0; i < adet; ++i) {
          ogeler[adet - 1 - i] = yiginPop();
        }
        yiginPush(yeniListe(std::move(ogeler)));
        break;
      }
      case OpCode::OP_SOZLUK_OLUSTUR: {
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
        break;
      }
      case OpCode::OP_ISLEV_OLUSTUR: {
        const SabitDeger& adDegeri = sabitOku(u16Oku());
        const std::uint16_t arity = u16Oku();
        const std::uint16_t giris = u16Oku();
        const std::uint16_t localSayisi = u16Oku();
        if (!std::holds_alternative<std::string>(adDegeri.veri)) {
          calismaHatasi("ISLEV_OLUSTUR ad sabiti metin degil.");
        }
        auto* fn = memory_.allocate<ObjFunction>(std::get<std::string>(adDegeri.veri),
                                                 static_cast<int>(arity), chunk_,
                                                 giris, localSayisi);
        yiginPush(Value::nesne(fn));
        break;
      }
      case OpCode::OP_CAGIR: {
        const std::uint16_t argc = u16Oku();
        if (yigin_.size() < static_cast<std::size_t>(argc) + 1) {
          calismaHatasi("CAGIR: yigin yetersiz.");
        }
        std::size_t calleeIndex = yigin_.size() - static_cast<std::size_t>(argc) - 1;
        Value cagrilan = yigin_[calleeIndex];

        if (objTipiMi(cagrilan, ObjType::BOUND_METHOD)) {
          auto* bm = static_cast<ObjBoundMethod*>(cagrilan.as.nesne);
          yigin_[calleeIndex] = bm->metod;
          yigin_.insert(yigin_.begin() + static_cast<std::ptrdiff_t>(calleeIndex + 1),
                        bm->alici);
          cagrilan = yigin_[calleeIndex];
          std::uint16_t yeniArgc = static_cast<std::uint16_t>(argc + 1);

          if (objTipiMi(cagrilan, ObjType::FUNCTION)) {
            auto* fn = static_cast<ObjFunction*>(cagrilan.as.nesne);
            if (fn->arity == static_cast<int>(argc) + 2) {
              if (!objTipiMi(bm->alici, ObjType::INSTANCE)) {
                calismaHatasi("Bagli metod super baglami olusturamadi.");
              }
              auto* inst = static_cast<ObjInstance*>(bm->alici.as.nesne);
              if (inst->sinif == nullptr || inst->sinif->ebeveyn == nullptr) {
                calismaHatasi("'ust' kullanimi icin ebeveyn sinif bulunamadi.");
              }
              Value ustRef =
                  Value::nesne(memory_.allocate<ObjSuperRef>(bm->alici, inst->sinif->ebeveyn));
              yigin_.insert(
                  yigin_.begin() + static_cast<std::ptrdiff_t>(calleeIndex + 2), ustRef);
              yeniArgc = static_cast<std::uint16_t>(yeniArgc + 1);
            }
          }

          if (objTipiMi(cagrilan, ObjType::FUNCTION)) {
            islevCagrisiHazirla(calleeIndex, yeniArgc,
                                static_cast<ObjFunction*>(cagrilan.as.nesne));
            break;
          }
          std::vector<Value> args;
          args.reserve(yeniArgc);
          for (std::size_t i = 0; i < yeniArgc; ++i) {
            args.push_back(yigin_[calleeIndex + 1 + i]);
          }
          const Value sonuc = cagir(cagrilan, args);
          yigin_.resize(calleeIndex);
          yiginPush(sonuc);
          break;
        }

        if (objTipiMi(cagrilan, ObjType::FUNCTION)) {
          islevCagrisiHazirla(calleeIndex, argc,
                              static_cast<ObjFunction*>(cagrilan.as.nesne));
          break;
        }

        if (objTipiMi(cagrilan, ObjType::CLASS)) {
          auto* sinif = static_cast<ObjClass*>(cagrilan.as.nesne);
          auto* inst = memory_.allocate<ObjInstance>(sinif);
          inst->alanlar = sinif->alanVarsayilanlari;
          Value instDegeri = Value::nesne(inst);

          auto ctorIt = sinif->metodlar.find("kur");
          if (ctorIt != sinif->metodlar.end()) {
            yigin_[calleeIndex] = ctorIt->second;
            yigin_.insert(yigin_.begin() + static_cast<std::ptrdiff_t>(calleeIndex + 1),
                          instDegeri);
            std::uint16_t yeniArgc = static_cast<std::uint16_t>(argc + 1);
            if (objTipiMi(yigin_[calleeIndex], ObjType::FUNCTION)) {
              auto* fn = static_cast<ObjFunction*>(yigin_[calleeIndex].as.nesne);
              if (fn->arity == static_cast<int>(argc) + 2) {
                if (sinif->ebeveyn == nullptr) {
                  calismaHatasi("Kurucu super baglami icin ebeveyn sinif bulunamadi.");
                }
                Value ustRef =
                    Value::nesne(memory_.allocate<ObjSuperRef>(instDegeri, sinif->ebeveyn));
                yigin_.insert(yigin_.begin() + static_cast<std::ptrdiff_t>(calleeIndex + 2),
                              ustRef);
                yeniArgc = static_cast<std::uint16_t>(yeniArgc + 1);
              }
              islevCagrisiHazirla(calleeIndex, yeniArgc,
                                  fn);
              bekleyenKurucular_.push_back(
                  BekleyenKurucu{frameYigini_.size(), instDegeri});
              break;
            }
            std::vector<Value> args;
            args.reserve(yeniArgc);
            for (std::size_t i = 0; i < yeniArgc; ++i) {
              args.push_back(yigin_[calleeIndex + 1 + i]);
            }
            (void)cagir(yigin_[calleeIndex], args);
          }

          yigin_.resize(calleeIndex);
          yiginPush(instDegeri);
          break;
        }

        std::vector<Value> args;
        args.reserve(argc);
        for (std::size_t i = 0; i < argc; ++i) {
          args.push_back(yigin_[calleeIndex + 1 + i]);
        }
        const Value sonuc = cagir(cagrilan, args);
        yigin_.resize(calleeIndex);
        yiginPush(sonuc);
        break;
      }
      case OpCode::OP_SINIF: {
        const SabitDeger& adDegeri = sabitOku(u16Oku());
        if (!std::holds_alternative<std::string>(adDegeri.veri)) {
          calismaHatasi("SINIF sabiti metin degil.");
        }
        yiginPush(Value::nesne(
            memory_.allocate<ObjClass>(std::get<std::string>(adDegeri.veri))));
        break;
      }
      case OpCode::OP_MIRAS_AL: {
        const Value ebeveyn = yiginPop();
        const Value cocuk = yiginPop();
        if (!objTipiMi(ebeveyn, ObjType::CLASS) || !objTipiMi(cocuk, ObjType::CLASS)) {
          calismaHatasi("MIRAS_AL: iki taraf da sinif olmali.");
        }
        auto* parent = static_cast<ObjClass*>(ebeveyn.as.nesne);
        auto* child = static_cast<ObjClass*>(cocuk.as.nesne);
        child->ebeveyn = parent;
        for (const auto& [k, v] : parent->metodlar) {
          child->metodlar.emplace(k, v);
        }
        for (const auto& [k, v] : parent->alanVarsayilanlari) {
          child->alanVarsayilanlari.emplace(k, v);
        }
        yiginPush(cocuk);
        break;
      }
      case OpCode::OP_TOPLA: {
        const Value sag = yiginPop();
        const Value sol = yiginPop();
        if (objTipiMi(sol, ObjType::STRING) || objTipiMi(sag, ObjType::STRING)) {
          yiginPush(yeniString(metneCevir(sol) + metneCevir(sag)));
        } else {
          yiginPush(Value::sayi(sayiyaCevir(sol, "toplama") +
                                sayiyaCevir(sag, "toplama")));
        }
        break;
      }
      case OpCode::OP_CIKAR:
      case OpCode::OP_CARP:
      case OpCode::OP_BOL: {
        const double sag = sayiyaCevir(yiginPop(), "sayisal islem");
        const double sol = sayiyaCevir(yiginPop(), "sayisal islem");
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
        break;
      }
      case OpCode::OP_NEGATE:
        yiginPush(Value::sayi(-sayiyaCevir(yiginPop(), "isaret degistirme")));
        break;
      case OpCode::OP_NOT:
        yiginPush(Value::mantik(falseMi(yiginPop())));
        break;
      case OpCode::OP_ESIT: {
        const Value sag = yiginPop();
        const Value sol = yiginPop();
        yiginPush(Value::mantik(esitMi(sol, sag)));
        break;
      }
      case OpCode::OP_BUYUK: {
        const double sag = sayiyaCevir(yiginPop(), "karsilastirma");
        const double sol = sayiyaCevir(yiginPop(), "karsilastirma");
        yiginPush(Value::mantik(sol > sag));
        break;
      }
      case OpCode::OP_KUCUK: {
        const double sag = sayiyaCevir(yiginPop(), "karsilastirma");
        const double sol = sayiyaCevir(yiginPop(), "karsilastirma");
        yiginPush(Value::mantik(sol < sag));
        break;
      }
      case OpCode::OP_VE: {
        const Value sag = yiginPop();
        const Value sol = yiginPop();
        yiginPush(Value::mantik(!falseMi(sol) && !falseMi(sag)));
        break;
      }
      case OpCode::OP_VEYA: {
        const Value sag = yiginPop();
        const Value sol = yiginPop();
        yiginPush(Value::mantik(!falseMi(sol) || !falseMi(sag)));
        break;
      }
      case OpCode::OP_YAZDIR:
        std::cout << metneCevir(yiginPop()) << '\n';
        break;
      case OpCode::OP_ATLA:
        frameYigini_.back().ip += u16Oku();
        break;
      case OpCode::OP_ATLA_EGER_YANLIS: {
        const std::uint16_t ofset = u16Oku();
        if (falseMi(yiginBak(0))) {
          frameYigini_.back().ip += ofset;
        }
        break;
      }
      case OpCode::OP_DONGU: {
        const std::uint16_t ofset = u16Oku();
        if (frameYigini_.back().ip < ofset) {
          calismaHatasi("Gecersiz dongu geri atlamasi.");
        }
        frameYigini_.back().ip -= ofset;
        break;
      }
      case OpCode::OP_TRY_BASLA: {
        const std::uint16_t ofset = u16Oku();
        TryFrame tf;
        tf.frameDerinligi = frameYigini_.size();
        tf.stackBoyutu = yigin_.size();
        tf.catchIp = frameYigini_.back().ip + ofset;
        tryYigini_.push_back(tf);
        break;
      }
      case OpCode::OP_TRY_BITIR:
        if (tryYigini_.empty()) {
          calismaHatasi("TRY_BITIR eslesen TRY_BASLA olmadan kullanildi.");
        }
        tryYigini_.pop_back();
        break;
      case OpCode::OP_DON: {
        Value donus = yiginPop();
        if (!bekleyenKurucular_.empty() &&
            bekleyenKurucular_.back().frameDerinligi == frameYigini_.size()) {
          donus = bekleyenKurucular_.back().olusanNesne;
          bekleyenKurucular_.pop_back();
        }
        popFrameVeDon(donus);
        if (frameYigini_.empty()) {
          gcCalistir();
          return;
        }
        break;
      }
      }
    } catch (const std::exception& ex) {
      if (tryYigini_.empty()) {
        throw;
      }

      const TryFrame hedef = tryYigini_.back();
      tryYigini_.pop_back();

      if (hedef.frameDerinligi == 0 || frameYigini_.size() < hedef.frameDerinligi) {
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

      CallFrame& aktif = frameYigini_.back();
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

const Value& VM::yiginBak(std::size_t tersten) const {
  if (tersten >= yigin_.size()) {
    calismaHatasi("Yigin okunamadi (sinir disi).");
  }
  return yigin_[yigin_.size() - 1 - tersten];
}

Value& VM::localEris(std::uint16_t indeks) {
  if (frameYigini_.empty()) {
    calismaHatasi("Local erisimi icin aktif frame yok.");
  }
  const std::size_t hedef = frameYigini_.back().slotBase + indeks;
  if (hedef >= yigin_.size()) {
    calismaHatasi("Local indeks sinir disi.");
  }
  return yigin_[hedef];
}

const Value& VM::localEris(std::uint16_t indeks) const {
  if (frameYigini_.empty()) {
    calismaHatasi("Local erisimi icin aktif frame yok.");
  }
  const std::size_t hedef = frameYigini_.back().slotBase + indeks;
  if (hedef >= yigin_.size()) {
    calismaHatasi("Local indeks sinir disi.");
  }
  return yigin_[hedef];
}

std::uint8_t VM::byteOku() {
  if (frameYigini_.empty()) {
    calismaHatasi("Frame yok.");
  }
  CallFrame& frame = frameYigini_.back();
  const BytecodeChunk* chunk = frame.function->chunk;
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

const SabitDeger& VM::sabitOku(std::uint16_t indeks) const {
  if (frameYigini_.empty()) {
    calismaHatasi("Frame yok.");
  }
  const BytecodeChunk* chunk = frameYigini_.back().function->chunk;
  if (chunk == nullptr || indeks >= chunk->sabitler.size()) {
    calismaHatasi("Sabit indeksi gecersiz.");
  }
  return chunk->sabitler[indeks];
}

Value VM::sabitDegeriniRuntimeaCevir(const SabitDeger& sabit) {
  if (std::holds_alternative<std::monostate>(sabit.veri)) {
    return Value::bos();
  }
  if (const auto* sayi = std::get_if<double>(&sabit.veri)) {
    return Value::sayi(*sayi);
  }
  if (const auto* mantik = std::get_if<bool>(&sabit.veri)) {
    return Value::mantik(*mantik);
  }
  if (const auto* metin = std::get_if<std::string>(&sabit.veri)) {
    return yeniString(*metin);
  }
  return Value::bos();
}

bool VM::falseMi(const Value& deger) const {
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
    return static_cast<ObjString*>(deger.as.nesne)->deger.empty();
  }
  if (deger.as.nesne->type == ObjType::LIST) {
    return static_cast<ObjList*>(deger.as.nesne)->ogeler.empty();
  }
  if (deger.as.nesne->type == ObjType::DICT) {
    return static_cast<ObjDict*>(deger.as.nesne)->alanlar.empty();
  }
  return false;
}

bool VM::esitMi(const Value& sol, const Value& sag) const {
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
        return static_cast<ObjString*>(sol.as.nesne)->deger ==
               static_cast<ObjString*>(sag.as.nesne)->deger;
      }
      return false;
  }
  return false;
}

std::string VM::metneCevir(const Value& deger) const {
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
      return static_cast<ObjString*>(deger.as.nesne)->deger;
    case ObjType::LIST: {
      const auto* liste = static_cast<ObjList*>(deger.as.nesne);
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
      const auto* sozluk = static_cast<ObjDict*>(deger.as.nesne);
      std::string sonuc = "{";
      bool ilk = true;
      for (const auto& [k, v] : sozluk->alanlar) {
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
      return "<islev:" + static_cast<ObjFunction*>(deger.as.nesne)->ad + ">";
    case ObjType::NATIVE:
      return "<yerlesik:" + static_cast<ObjNative*>(deger.as.nesne)->ad + ">";
    case ObjType::CLASS:
      return "<sinif:" + static_cast<ObjClass*>(deger.as.nesne)->ad + ">";
    case ObjType::INSTANCE: {
      auto* inst = static_cast<ObjInstance*>(deger.as.nesne);
      return "<" + (inst->sinif != nullptr ? inst->sinif->ad : std::string("Nesne")) +
             " nesnesi>";
    }
    case ObjType::BOUND_METHOD:
      return "<bagli_metod>";
    case ObjType::SUPER_REF:
      return "<ust>";
  }
  return "<deger>";
}

double VM::sayiyaCevir(const Value& deger, const std::string& baglam) const {
  if (deger.type == ValueType::SAYI) {
    return deger.as.sayi;
  }
  if (deger.type == ValueType::MANTIK) {
    return deger.as.mantik ? 1.0 : 0.0;
  }
  if (objTipiMi(deger, ObjType::STRING)) {
    try {
      return std::stod(static_cast<ObjString*>(deger.as.nesne)->deger);
    } catch (...) {
      calismaHatasi("Sayisal olmayan metin ile " + baglam + " yapilamaz.");
    }
  }
  calismaHatasi("Sayisal olmayan deger ile " + baglam + " yapilamaz.");
}

std::string VM::anahtaraCevir(const Value& deger, const std::string& baglam) const {
  if (objTipiMi(deger, ObjType::STRING)) {
    return static_cast<ObjString*>(deger.as.nesne)->deger;
  }
  if (deger.type == ValueType::SAYI) {
    return sayiyiMetinlestir(deger.as.sayi);
  }
  if (deger.type == ValueType::MANTIK) {
    return deger.as.mantik ? "dogru" : "yanlis";
  }
  calismaHatasi("Anahtar metne cevrilemedi (" + baglam + ").");
}

Value VM::alanAl(const Value& hedef, const std::string& alanAdi) {
  if (!hedef.nesneMi() || hedef.as.nesne == nullptr) {
    calismaHatasi("Alan erisimi yalnizca nesnelerde yapilabilir.");
  }

  Obj* obj = hedef.as.nesne;
  if (obj->type == ObjType::SUPER_REF) {
    auto* ust = static_cast<ObjSuperRef*>(obj);
    if (ust->ustSinif == nullptr) {
      calismaHatasi("'ust' referansi gecersiz.");
    }
    auto it = ust->ustSinif->metodlar.find(alanAdi);
    if (it == ust->ustSinif->metodlar.end()) {
      calismaHatasi("Ebeveyn sinifta '" + alanAdi + "' metodu bulunamadi.");
    }
    return Value::nesne(memory_.allocate<ObjBoundMethod>(ust->alici, it->second));
  }

  if (obj->type == ObjType::DICT) {
    auto* sozluk = static_cast<ObjDict*>(obj);
    auto it = sozluk->alanlar.find(alanAdi);
    if (it != sozluk->alanlar.end()) {
      return it->second;
    }
    calismaHatasi("Sozlukte '" + alanAdi + "' anahtari yok.");
  }

  if (obj->type == ObjType::INSTANCE) {
    auto* nesne = static_cast<ObjInstance*>(obj);
    if (auto it = nesne->alanlar.find(alanAdi); it != nesne->alanlar.end()) {
      return it->second;
    }
    if (nesne->sinif != nullptr) {
      if (auto mit = nesne->sinif->metodlar.find(alanAdi);
          mit != nesne->sinif->metodlar.end()) {
        return Value::nesne(memory_.allocate<ObjBoundMethod>(hedef, mit->second));
      }
    }
    calismaHatasi("Nesnede '" + alanAdi + "' alani bulunamadi.");
  }

  if (obj->type == ObjType::CLASS) {
    auto* sinif = static_cast<ObjClass*>(obj);
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
          [](VM&, const std::vector<Value>& args) -> Value {
            auto* self = static_cast<ObjList*>(args.at(0).as.nesne);
            return Value::sayi(static_cast<double>(self->ogeler.size()));
          },
          std::vector<Value>{hedef}));
    }
    if (alanAdi == "ekle") {
      return Value::nesne(memory_.allocate<ObjNative>(
          "liste.ekle", 1,
          [](VM&, const std::vector<Value>& args) -> Value {
            auto* self = static_cast<ObjList*>(args.at(0).as.nesne);
            self->ogeler.push_back(args.at(1));
            return Value::bos();
          },
          std::vector<Value>{hedef}));
    }
    calismaHatasi("Listede '" + alanAdi + "' metodu yok.");
  }

  if (obj->type == ObjType::STRING) {
    const std::string metin = static_cast<ObjString*>(obj)->deger;
    if (alanAdi == "buyuk") {
      return Value::nesne(memory_.allocate<ObjNative>(
          "metin.buyuk", 0,
          [metin](VM& vm, const std::vector<Value>&) -> Value {
            return vm.yeniString(asciiBuyut(metin));
          },
          std::vector<Value>{hedef}));
    }
    if (alanAdi == "kucuk") {
      return Value::nesne(memory_.allocate<ObjNative>(
          "metin.kucuk", 0,
          [metin](VM& vm, const std::vector<Value>&) -> Value {
            return vm.yeniString(asciiKucult(metin));
          },
          std::vector<Value>{hedef}));
    }
    calismaHatasi("Metinde '" + alanAdi + "' metodu yok.");
  }

  calismaHatasi("Bu tipte alan erisimi desteklenmiyor.");
}

Value VM::indeksAl(const Value& hedef, const Value& indeks) {
  if (!hedef.nesneMi() || hedef.as.nesne == nullptr) {
    calismaHatasi("Indeks erisimi yalnizca liste/sozluk/metinde yapilabilir.");
  }
  if (hedef.as.nesne->type == ObjType::LIST) {
    auto* liste = static_cast<ObjList*>(hedef.as.nesne);
    const int i = static_cast<int>(std::llround(sayiyaCevir(indeks, "liste indeksi")));
    if (i < 0 || static_cast<std::size_t>(i) >= liste->ogeler.size()) {
      calismaHatasi("Liste indeksi sinir disi.");
    }
    return liste->ogeler[static_cast<std::size_t>(i)];
  }
  if (hedef.as.nesne->type == ObjType::DICT) {
    auto* sozluk = static_cast<ObjDict*>(hedef.as.nesne);
    const std::string anahtar = anahtaraCevir(indeks, "sozluk indeksi");
    auto it = sozluk->alanlar.find(anahtar);
    if (it == sozluk->alanlar.end()) {
      calismaHatasi("Hata: '" + anahtar + "' anahtari sozlukte bulunamadi.");
    }
    return it->second;
  }
  if (hedef.as.nesne->type == ObjType::STRING) {
    const std::string& metin = static_cast<ObjString*>(hedef.as.nesne)->deger;
    const int i = static_cast<int>(std::llround(sayiyaCevir(indeks, "metin indeksi")));
    if (i < 0 || static_cast<std::size_t>(i) >= metin.size()) {
      calismaHatasi("Metin indeksi sinir disi.");
    }
    return yeniString(std::string(1, metin[static_cast<std::size_t>(i)]));
  }
  calismaHatasi("Bu tipte indeks erisimi desteklenmiyor.");
}

Value VM::cagir(const Value& cagrilan, const std::vector<Value>& argumanlar) {
  if (!cagrilan.nesneMi() || cagrilan.as.nesne == nullptr) {
    calismaHatasi("Cagrilan deger islev degil.");
  }
  if (cagrilan.as.nesne->type == ObjType::NATIVE) {
    auto* native = static_cast<ObjNative*>(cagrilan.as.nesne);
    if (native->arity >= 0 &&
        static_cast<int>(argumanlar.size()) != native->arity) {
      calismaHatasi("Arguman sayisi uyusmuyor: '" + native->ad + "'.");
    }
    std::vector<Value> tumArgumanlar = native->bagliDegerler;
    tumArgumanlar.insert(tumArgumanlar.end(), argumanlar.begin(), argumanlar.end());
    return native->fn(*this, tumArgumanlar);
  }
  calismaHatasi("Bu deger cagrilamaz.");
}

[[noreturn]] void VM::calismaHatasi(const std::string& mesaj) const {
  std::size_t satir = 0;
  if (!frameYigini_.empty()) {
    const CallFrame& frame = frameYigini_.back();
    const BytecodeChunk* chunk = frame.function != nullptr ? frame.function->chunk : nullptr;
    if (chunk != nullptr && frame.ip > 0 && frame.ip - 1 < chunk->satirlar.size()) {
      satir = chunk->satirlar[frame.ip - 1];
    }
  }
  throw std::runtime_error("VM Calisma Hatasi (satir " + std::to_string(satir) +
                           "): " + mesaj);
}
