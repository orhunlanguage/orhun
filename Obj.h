#pragma once

#include "Value.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct BytecodeChunk;
class VM;

enum class ObjType {
  STRING = 0,
  LIST,
  DICT,
  FUNCTION,
  NATIVE,
  CLASS,
  INSTANCE,
  BOUND_METHOD,
  SUPER_REF,
};

// VM heap'inde yer alan tum nesnelerin ortak tabani.
struct Obj {
  explicit Obj(ObjType tur) : type(tur) {}
  virtual ~Obj() = default;

  ObjType type;
  bool isMarked = false;
  Obj* next = nullptr; // GC icin tek yonlu bagli liste
};

struct ObjString final : Obj {
  explicit ObjString(std::string degeri)
      : Obj(ObjType::STRING), deger(std::move(degeri)) {}

  std::string deger;
};

struct ObjList final : Obj {
  ObjList() : Obj(ObjType::LIST) {}
  explicit ObjList(std::vector<Value> ogeler_)
      : Obj(ObjType::LIST), ogeler(std::move(ogeler_)) {}

  std::vector<Value> ogeler;
};

struct ObjDict final : Obj {
  ObjDict() : Obj(ObjType::DICT) {}
  explicit ObjDict(std::unordered_map<std::string, Value> alanlar_)
      : Obj(ObjType::DICT), alanlar(std::move(alanlar_)) {}

  std::unordered_map<std::string, Value> alanlar;
};

struct ObjFunction final : Obj {
  ObjFunction() : Obj(ObjType::FUNCTION) {}
  ObjFunction(std::string ad_, int minArity_, int maxArity_,
              const BytecodeChunk* chunk_, std::uint16_t girisIp_,
              std::uint16_t localSayisi_, std::uint16_t baglamArgSayisi_ = 0)
      : Obj(ObjType::FUNCTION), ad(std::move(ad_)), minArity(minArity_),
        maxArity(maxArity_), chunk(chunk_), girisIp(girisIp_),
        localSayisi(localSayisi_),
        baglamArgSayisi(baglamArgSayisi_) {}

  std::string ad;
  int minArity = 0;
  int maxArity = 0;
  const BytecodeChunk* chunk = nullptr;
  std::uint16_t girisIp = 0;
  std::uint16_t localSayisi = 0;
  std::uint16_t baglamArgSayisi = 0;
  std::vector<std::string> localAdlari;
  std::unordered_map<std::string, std::shared_ptr<Value>> yakalananDegerler;
};

struct ObjNative final : Obj {
  using NativeFn = std::function<Value(VM&, const std::vector<Value>&)>;

  ObjNative(std::string ad_, int arity_, NativeFn fn_,
            std::vector<Value> bagliDegerler_ = {})
      : Obj(ObjType::NATIVE), ad(std::move(ad_)), arity(arity_),
        fn(std::move(fn_)), bagliDegerler(std::move(bagliDegerler_)) {}

  std::string ad;
  int arity = -1; // -1 = degisken arguman
  NativeFn fn;
  std::vector<Value> bagliDegerler;
};

struct ObjClass final : Obj {
  ObjClass() : Obj(ObjType::CLASS) {}
  explicit ObjClass(std::string ad_) : Obj(ObjType::CLASS), ad(std::move(ad_)) {}

  std::string ad;
  ObjClass* ebeveyn = nullptr;
  std::unordered_map<std::string, Value> metodlar;
  std::unordered_map<std::string, Value> alanVarsayilanlari;
};

struct ObjInstance final : Obj {
  explicit ObjInstance(ObjClass* sinif_)
      : Obj(ObjType::INSTANCE), sinif(sinif_) {}

  ObjClass* sinif = nullptr;
  std::unordered_map<std::string, Value> alanlar;
};

struct ObjBoundMethod final : Obj {
  ObjBoundMethod(Value alici_, Value metod_)
      : Obj(ObjType::BOUND_METHOD), alici(alici_), metod(metod_) {}

  Value alici;
  Value metod;
};

struct ObjSuperRef final : Obj {
  ObjSuperRef(Value alici_, ObjClass* ustSinif_)
      : Obj(ObjType::SUPER_REF), alici(alici_), ustSinif(ustSinif_) {}

  Value alici;
  ObjClass* ustSinif = nullptr;
};

inline bool objTipiMi(const Value& deger, ObjType tip) {
  return deger.type == ValueType::NESNE && deger.as.nesne != nullptr &&
         deger.as.nesne->type == tip;
}
