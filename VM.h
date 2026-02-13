#pragma once

#include "Chunk.h"
#include "Memory.h"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace runtime {
class DynamicLibrary;
}

// Stack tabanli bytecode sanal makinesi (Faz 2 runtime + GC).
class VM {
public:
  VM();

  void sifirla();
  void calistir(const BytecodeChunk& chunk);

  // Native fonksiyonlarin kolay Value uretmesi icin yardimcilar.
  Value yeniString(const std::string& metin);
  Value yeniListe(std::vector<Value> ogeler);
  Value yeniSozluk(std::unordered_map<std::string, Value> alanlar);

private:
  struct CallFrame {
    ObjFunction* function = nullptr;
    std::size_t ip = 0;
    std::size_t slotBase = 0;
  };
  struct BekleyenKurucu {
    std::size_t frameDerinligi = 0;
    Value olusanNesne = Value::bos();
  };
  struct TryFrame {
    std::size_t frameDerinligi = 0;
    std::size_t stackBoyutu = 0;
    std::size_t catchIp = 0;
  };

  const BytecodeChunk* chunk_ = nullptr;
  std::vector<CallFrame> frameYigini_;
  std::vector<BekleyenKurucu> bekleyenKurucular_;
  std::vector<TryFrame> tryYigini_;
  std::vector<Value> yigin_;
  std::unordered_map<std::string, Value> globaller_;
  std::unordered_map<int, std::shared_ptr<runtime::DynamicLibrary>>
      ffiKutuphaneleri_;
  std::unordered_map<std::string, int> ffiKutuphaneKimlikleri_;
  int ffiSonrakiKimlik_ = 1;

  MemoryManager memory_;
  std::size_t gcEsigi_ = 1024;

  void yerlesikNativesYukle();
  Value nativeOlustur(const std::string& ad, int arity, ObjNative::NativeFn fn);
  void gcGerekirseCalistir();
  void gcCalistir();

  void yiginPush(Value deger);
  Value yiginPop();
  const Value& yiginBak(std::size_t tersten) const;
  Value& localEris(std::uint16_t indeks);
  const Value& localEris(std::uint16_t indeks) const;

  std::uint8_t byteOku();
  std::uint16_t u16Oku();
  const SabitDeger& sabitOku(std::uint16_t indeks) const;
  Value sabitDegeriniRuntimeaCevir(const SabitDeger& sabit);

  bool falseMi(const Value& deger) const;
  bool esitMi(const Value& sol, const Value& sag) const;
  std::string metneCevir(const Value& deger) const;
  double sayiyaCevir(const Value& deger, const std::string& baglam) const;
  std::string anahtaraCevir(const Value& deger, const std::string& baglam) const;

  Value alanAl(const Value& hedef, const std::string& alanAdi);
  Value indeksAl(const Value& hedef, const Value& indeks);
  Value cagir(const Value& cagrilan, const std::vector<Value>& argumanlar);
  void islevCagrisiHazirla(std::size_t calleeIndex, std::uint16_t argc,
                           ObjFunction* fn);
  void pushFrame(ObjFunction* fn, std::size_t slotBase);
  void popFrameVeDon(Value donusDegeri);

  [[noreturn]] void calismaHatasi(const std::string& mesaj) const;
};
