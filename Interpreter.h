#pragma once

#include "AST.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

struct OrhunNesne;
struct NesneMetodBilgisi;
namespace runtime {
class DynamicLibrary;
}

#define ORHUN_SURUM "0.8.0"
#define ORHUN_INSA_NO "50"

class OrhunHatasi : public std::runtime_error {
public:
  explicit OrhunHatasi(const std::string &mesaj) : std::runtime_error(mesaj) {}
};

// Orhun çalışma zamanı değeri.
// v0.8: sayı(int/double), metin, liste, sözlük ve nesne desteklenir.
struct OrhunDegeri {
  // Recursive variant problemi için konteynerler shared_ptr ile taşınır.
  using ListeVeri = std::vector<OrhunDegeri>;
  using SozlukVeri = std::map<std::string, OrhunDegeri>;
  using ListeTipi = std::shared_ptr<ListeVeri>;
  using SozlukTipi = std::shared_ptr<SozlukVeri>;
  using NesneTipi = std::shared_ptr<OrhunNesne>;

  std::variant<int, double, std::string, ListeTipi, SozlukTipi, NesneTipi> veri;

  OrhunDegeri() : veri(0) {}
  explicit OrhunDegeri(int v) : veri(v) {}
  explicit OrhunDegeri(double v) : veri(v) {}
  explicit OrhunDegeri(std::string v) : veri(std::move(v)) {}
  explicit OrhunDegeri(const char *v) : veri(std::string(v)) {}
  explicit OrhunDegeri(ListeTipi v)
      : veri(v ? std::move(v) : std::make_shared<ListeVeri>()) {}
  explicit OrhunDegeri(SozlukTipi v)
      : veri(v ? std::move(v) : std::make_shared<SozlukVeri>()) {}
  explicit OrhunDegeri(ListeVeri v)
      : veri(std::make_shared<ListeVeri>(std::move(v))) {}
  explicit OrhunDegeri(SozlukVeri v)
      : veri(std::make_shared<SozlukVeri>(std::move(v))) {}
  explicit OrhunDegeri(NesneTipi v) : veri(std::move(v)) {}

  bool operator==(const OrhunDegeri &diger) const;
};

// OOP örnek nesne verisi.
struct NesneMetodBilgisi {
  const IslevTanimNode *dugum = nullptr;
  std::string tanimlayanSinif;
};

struct OrhunNesne {
  std::string sinifAdi;
  OrhunDegeri::SozlukTipi alanlar = std::make_shared<OrhunDegeri::SozlukVeri>();
  std::unordered_map<std::string, NesneMetodBilgisi> metodlar;
};

inline bool OrhunDegeri::operator==(const OrhunDegeri &diger) const {
  if (veri.index() != diger.veri.index()) {
    return false;
  }

  if (const auto *v = std::get_if<int>(&veri)) {
    return *v == std::get<int>(diger.veri);
  }
  if (const auto *v = std::get_if<double>(&veri)) {
    return *v == std::get<double>(diger.veri);
  }
  if (const auto *v = std::get_if<std::string>(&veri)) {
    return *v == std::get<std::string>(diger.veri);
  }
  if (const auto *v = std::get_if<ListeTipi>(&veri)) {
    const auto &digerListe = std::get<ListeTipi>(diger.veri);
    if (!(*v) || !digerListe) {
      return !(*v) && !digerListe;
    }
    return **v == *digerListe;
  }

  if (const auto *v = std::get_if<SozlukTipi>(&veri)) {
    const auto &digerSozluk = std::get<SozlukTipi>(diger.veri);
    if (!(*v) || !digerSozluk) {
      return !(*v) && !digerSozluk;
    }
    return **v == *digerSozluk;
  }

  const auto &nesne = std::get<NesneTipi>(veri);
  const auto &digerNesne = std::get<NesneTipi>(diger.veri);
  if (!nesne || !digerNesne) {
    return !nesne && !digerNesne;
  }
  if (nesne->sinifAdi != digerNesne->sinifAdi) {
    return false;
  }
  if (!nesne->alanlar || !digerNesne->alanlar) {
    return !nesne->alanlar && !digerNesne->alanlar;
  }
  return *nesne->alanlar == *digerNesne->alanlar;
}

class Interpreter {
public:
  Interpreter();
  ~Interpreter();

  // Program veya herhangi bir komut düğümünü çalıştırır.
  void calistir(const ASTNode *dugum);

private:
  enum class FFIType {
    NONE,
    INT64,
    DOUBLE,
    STRING,
    POINTER,
  };

  struct FFISignature {
    std::string sembolAdi;
    FFIType donusTipi = FFIType::INT64;
    std::vector<FFIType> argumanTipleri;
  };

  struct FFIBinding {
    int kutuphaneKimligi = 0;
    FFISignature imza;
  };

  using DegiskenTablosu = std::unordered_map<std::string, OrhunDegeri>;
  using KapsamPtr = std::shared_ptr<DegiskenTablosu>;
  using GomuluIslev =
      std::function<OrhunDegeri(const std::vector<OrhunDegeri> &, std::size_t)>;

  DegiskenTablosu globalHafiza_;
  std::vector<KapsamPtr> yerelKapsamYigini_;
  int donguDerinligi_ = 0;

  std::unordered_map<std::string, const IslevTanimNode *> islevTablosu_;
  std::unordered_map<std::string, const IsimsizIslevNode *>
      anonimIslevTablosu_;
  struct ClosureKaydi {
    const IslevTanimNode *islev = nullptr;
    const IsimsizIslevNode *anonimIslev = nullptr;
    std::vector<KapsamPtr> yakalananKapsamlar;
    std::string ad;
  };
  std::unordered_map<std::string, ClosureKaydi> closureTablosu_;
  std::unordered_map<std::string, const SinifTanimNode *> sinifTablosu_;
  std::unordered_map<std::string, GomuluIslev> gomuluIslevler_;
  std::vector<std::unique_ptr<ProgramNode>> yukluModuller_;
  std::unordered_map<int, std::shared_ptr<runtime::DynamicLibrary>>
      ffiKutuphaneleri_;
  std::unordered_map<std::string, int> ffiKutuphaneKimlikleri_;
  std::unordered_map<int, FFIBinding> ffiIslevBaglantilari_;
  int ffiSonrakiKimlik_ = 1;
  int ffiSonrakiIslevKimlik_ = 1;
  std::size_t anonimIslevSayaci_ = 0;
  std::size_t closureSayaci_ = 0;
  struct CagriCercevesi {
    std::string ad;
    std::size_t satir = 0;
  };
  std::vector<CagriCercevesi> cagriYigini_;

  void gomuluIslevleriYukle();
  void yerlesikModulleriYukle();

  void calistirBlock(const BlockNode *block);
  void calistirAtama(const AtamaNode *dugum);
  void calistirCokluAtama(const CokluAtamaNode *dugum);
  void calistirYazdir(const YazdirNode *dugum);
  void calistirEger(const EgerNode *dugum);
  void calistirTekrarla(const TekrarlaNode *dugum);
  void calistirSurece(const SureceNode *dugum);
  void calistirIslevTanim(const IslevTanimNode *dugum);
  void calistirDisIslevTanim(const DisIslevTanimNode *dugum);
  void calistirSinifTanim(const SinifTanimNode *dugum);
  void calistirDenemeYakala(const DenemeYakalaNode *dugum);
  void calistirKir(const KirNode *dugum);
  void calistirDevam(const DevamNode *dugum);
  void calistirDondur(const DondurNode *dugum);
  void calistirDahilEt(const DahilEtNode *dugum);
  void calistirIfadeKomut(const IfadeKomutNode *dugum);

  OrhunDegeri ifadeHesapla(const ASTNode *dugum);
  OrhunDegeri tekliIslemHesapla(const TekliIslemNode *dugum);
  OrhunDegeri ikiliIslemHesapla(const IkiliIslemNode *dugum);
  OrhunDegeri listeIslemi(const OrhunDegeri &sol, const OrhunDegeri &sag,
                          const std::string &op, std::size_t satir);
  OrhunDegeri sorCalistir(const SorNode *dugum);
  OrhunDegeri listeOlustur(const ListeNode *dugum);
  OrhunDegeri listeUreteciOlustur(const ListeUretecNode *dugum);
  OrhunDegeri sozlukOlustur(const SozlukNode *dugum);
  OrhunDegeri dilimErisim(const DilimErisimNode *dugum);
  OrhunDegeri indeksErisim(const IndeksErisimNode *dugum);
  OrhunDegeri guvenliAlanErisim(const GuvenliAlanErisimNode *dugum);
  OrhunDegeri alanErisim(const AlanErisimNode *dugum);
  OrhunDegeri benimErisim(const BenimErisimNode *dugum);
  OrhunDegeri yeniNesneOlustur(const YeniNesneNode *dugum);
  OrhunDegeri islevCagir(const IslevCagriNode *dugum,
                         bool dondurZorunlu = true);
  OrhunDegeri anonimIslevOlustur(const IsimsizIslevNode *dugum);
  OrhunDegeri dahilEtDegerlendir(const DahilEtNode *dugum);
  OrhunDegeri ustIslevCagir(const std::string &metodAdi,
                            const std::vector<OrhunDegeri> &argumanlar,
                            std::size_t satir);
  OrhunDegeri islevCagirAdaGore(const std::string &ad,
                                const std::vector<OrhunDegeri> &argumanlar,
                                std::size_t satir,
                                bool dondurZorunlu = true);
  OrhunDegeri kullaniciIslevCalistir(const IslevTanimNode *islev,
                                     const std::vector<OrhunDegeri> &argumanlar,
                                     std::size_t satir,
                                     const OrhunDegeri *benimDegeri,
                                     const std::string *etkinSinifAdi,
                                     bool dondurZorunlu,
                                     const std::vector<KapsamPtr>
                                         *yakalananKapsamlar = nullptr);
  OrhunDegeri anonimIslevCalistir(const IsimsizIslevNode *islev,
                                  const std::vector<OrhunDegeri> &argumanlar,
                                  std::size_t satir,
                                  const OrhunDegeri *benimDegeri,
                                  const std::string *etkinSinifAdi,
                                  bool dondurZorunlu,
                                  const std::vector<KapsamPtr>
                                      *yakalananKapsamlar = nullptr);
  OrhunDegeri nesneMetoduCagir(const OrhunDegeri &hedef,
                               const std::string &metodAdi,
                               const std::vector<OrhunDegeri> &argumanlar,
                               std::size_t satir);
  OrhunDegeri noktaYoluDegeri(const std::string &yol, std::size_t satir) const;
  bool islevReferansiCoz(const OrhunDegeri &deger, std::string &gercekAd) const;

  DegiskenTablosu &aktifKapsam();
  void atamaHedefiYaz(const std::string &ad, const OrhunDegeri &deger,
                      bool bildirimMi, std::size_t satir);
  OrhunDegeri &degiskenBulYazilabilir(const std::string &ad, std::size_t satir);
  const OrhunDegeri &degiskenBul(const std::string &ad,
                                 std::size_t satir) const;
  OrhunDegeri &atananHedefYazilabilir(const ASTNode *hedef, std::size_t satir,
                                      bool sonHedef);

  bool dogruMu(const OrhunDegeri &deger) const;
  bool esittir(const OrhunDegeri &sol, const OrhunDegeri &sag) const;
  std::string metneCevir(const OrhunDegeri &deger) const;
  std::string metinGomuleriCoz(const std::string &metin,
                               std::size_t satir) const;

  // Sayı yardımcıları: int/double birlikte işlenir.
  bool sayiMi(const OrhunDegeri &deger) const;
  double sayiDegeri(const OrhunDegeri &deger, std::size_t satir,
                    const std::string &baglam) const;
  bool tamSayiMi(const OrhunDegeri &deger) const;
  bool tamSayiMi(double deger) const;
  long long dilimSiniriCevir(const ASTNode *sinirIfadesi, long long varsayilan,
                             long long uzunluk, std::size_t satir,
                             const std::string &baglam);
  std::size_t listeIndeksiCevir(const OrhunDegeri &deger, std::size_t satir,
                                const std::string &baglam) const;
  std::string stackTraceOlustur() const;

  [[noreturn]] void hataFirlat(std::size_t satir,
                               const std::string &mesaj) const;
};
