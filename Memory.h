#pragma once

#include "Obj.h"

#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

// Stop-the-world Mark-Sweep cop toplayici.
// Tasarim bilerek basit tutuldu: VM root'lari callback ile isaretlenir.
class MemoryManager {
public:
  MemoryManager() = default;
  MemoryManager(const MemoryManager&) = delete;
  MemoryManager& operator=(const MemoryManager&) = delete;

  ~MemoryManager() { tumunuTemizle(); }

  template <typename T, typename... Args>
  T* allocate(Args&&... args) {
    static_assert(std::is_base_of<Obj, T>::value,
                  "allocate<T>: T, Obj turevi olmali.");
    T* obj = new T(std::forward<Args>(args)...);
    obj->next = nesneler_;
    nesneler_ = obj;
    ++nesneSayisi_;
    return obj;
  }

  void markValue(const Value& deger) {
    if (deger.type == ValueType::NESNE && deger.as.nesne != nullptr) {
      markObject(deger.as.nesne);
    }
  }

  void markObject(Obj* obj) {
    if (obj == nullptr || obj->isMarked) {
      return;
    }
    obj->isMarked = true;
    griYigin_.push_back(obj);
  }

  void collectGarbage(const std::function<void(MemoryManager&)>& rootMarker) {
    if (rootMarker) {
      rootMarker(*this);
    }
    while (!griYigin_.empty()) {
      Obj* obj = griYigin_.back();
      griYigin_.pop_back();
      blacken(obj);
    }
    sweep();
  }

  std::size_t objectCount() const { return nesneSayisi_; }

private:
  Obj* nesneler_ = nullptr;
  std::size_t nesneSayisi_ = 0;
  std::vector<Obj*> griYigin_;

  void blacken(Obj* obj) {
    if (obj == nullptr) {
      return;
    }

    switch (obj->type) {
      case ObjType::STRING:
        return;
      case ObjType::FUNCTION: {
        auto* fn = static_cast<ObjFunction*>(obj);
        for (const auto& [ad, hucre] : fn->yakalananDegerler) {
          (void)ad;
          if (hucre) {
            markValue(*hucre);
          }
        }
        return;
      }
      case ObjType::NATIVE: {
        auto* native = static_cast<ObjNative*>(obj);
        for (const Value& bagli : native->bagliDegerler) {
          markValue(bagli);
        }
        return;
      }
      case ObjType::LIST: {
        auto* liste = static_cast<ObjList*>(obj);
        for (const Value& oge : liste->ogeler) {
          markValue(oge);
        }
        return;
      }
      case ObjType::DICT: {
        auto* sozluk = static_cast<ObjDict*>(obj);
        for (const auto& [anahtar, deger] : sozluk->alanlar) {
          (void)anahtar;
          markValue(deger);
        }
        return;
      }
      case ObjType::CLASS: {
        auto* sinif = static_cast<ObjClass*>(obj);
        for (const auto& [ad, metod] : sinif->metodlar) {
          (void)ad;
          markValue(metod);
        }
        for (const auto& [ad, alan] : sinif->alanVarsayilanlari) {
          (void)ad;
          markValue(alan);
        }
        return;
      }
      case ObjType::INSTANCE: {
        auto* nesne = static_cast<ObjInstance*>(obj);
        markObject(nesne->sinif);
        for (const auto& [ad, alan] : nesne->alanlar) {
          (void)ad;
          markValue(alan);
        }
        return;
      }
      case ObjType::BOUND_METHOD: {
        auto* bagli = static_cast<ObjBoundMethod*>(obj);
        markValue(bagli->alici);
        markValue(bagli->metod);
        return;
      }
      case ObjType::SUPER_REF: {
        auto* ust = static_cast<ObjSuperRef*>(obj);
        markValue(ust->alici);
        markObject(ust->ustSinif);
        return;
      }
    }
  }

  void sweep() {
    Obj* onceki = nullptr;
    Obj* mevcut = nesneler_;
    while (mevcut != nullptr) {
      if (mevcut->isMarked) {
        mevcut->isMarked = false;
        onceki = mevcut;
        mevcut = mevcut->next;
        continue;
      }

      Obj* silinecek = mevcut;
      mevcut = mevcut->next;
      if (onceki != nullptr) {
        onceki->next = mevcut;
      } else {
        nesneler_ = mevcut;
      }
      delete silinecek;
      if (nesneSayisi_ > 0) {
        --nesneSayisi_;
      }
    }
  }

  void tumunuTemizle() {
    Obj* mevcut = nesneler_;
    while (mevcut != nullptr) {
      Obj* sonraki = mevcut->next;
      delete mevcut;
      mevcut = sonraki;
    }
    nesneler_ = nullptr;
    nesneSayisi_ = 0;
    griYigin_.clear();
  }
};
