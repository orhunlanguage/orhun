#pragma once

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// AST yazdırırken ortak girinti yardımcı fonksiyonu.
inline void yazdirGirinti(std::ostream &cikti, int girinti) {
  for (int i = 0; i < girinti; ++i) {
    cikti << ' ';
  }
}

// Tüm AST düğümleri için temel arayüz.
// Her düğüm üretildiği satır bilgisini taşır.
class ASTNode {
public:
  explicit ASTNode(std::size_t satir) : satir_(satir) {}
  virtual ~ASTNode() = default;

  std::size_t satir() const { return satir_; }

  virtual void yazdir_agac(std::ostream &cikti, int girinti = 0) const = 0;

private:
  std::size_t satir_;
};

class SayiNode final : public ASTNode {
public:
  SayiNode(std::string deger, std::size_t satir)
      : ASTNode(satir), deger_(std::move(deger)) {}

  const std::string &deger() const { return deger_; }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "SayiNode(" << deger_ << ") [satır " << satir() << "]\n";
  }

private:
  std::string deger_;
};

class MetinNode final : public ASTNode {
public:
  MetinNode(std::string deger, std::size_t satir)
      : ASTNode(satir), deger_(std::move(deger)) {}

  const std::string &deger() const { return deger_; }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "MetinNode(\"" << deger_ << "\") [satır " << satir() << "]\n";
  }

private:
  std::string deger_;
};

class MantikNode final : public ASTNode {
public:
  MantikNode(bool deger, std::size_t satir) : ASTNode(satir), deger_(deger) {}

  bool deger() const { return deger_; }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "MantikNode(" << (deger_ ? "doğru" : "yanlış") << ") [satır "
          << satir() << "]\n";
  }

private:
  bool deger_;
};

class KimlikNode final : public ASTNode {
public:
  KimlikNode(std::string ad, std::size_t satir)
      : ASTNode(satir), ad_(std::move(ad)) {}

  const std::string &ad() const { return ad_; }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "KimlikNode(" << ad_ << ") [satır " << satir() << "]\n";
  }

private:
  std::string ad_;
};

class TekliIslemNode final : public ASTNode {
public:
  TekliIslemNode(std::string op, std::unique_ptr<ASTNode> ifade,
                 std::size_t satir)
      : ASTNode(satir), op_(std::move(op)), ifade_(std::move(ifade)) {}

  const std::string &op() const { return op_; }

  const ASTNode *ifade() const { return ifade_.get(); }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "TekliIslemNode(" << op_ << ") [satır " << satir() << "]\n";
    ifade_->yazdir_agac(cikti, girinti + 2);
  }

private:
  std::string op_;
  std::unique_ptr<ASTNode> ifade_;
};

class IkiliIslemNode final : public ASTNode {
public:
  IkiliIslemNode(std::unique_ptr<ASTNode> sol, std::string op,
                 std::unique_ptr<ASTNode> sag, std::size_t satir)
      : ASTNode(satir), sol_(std::move(sol)), op_(std::move(op)),
        sag_(std::move(sag)) {}

  const ASTNode *sol() const { return sol_.get(); }

  const std::string &op() const { return op_; }

  const ASTNode *sag() const { return sag_.get(); }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "IkiliIslemNode(" << op_ << ") [satır " << satir() << "]\n";
    yazdirGirinti(cikti, girinti + 2);
    cikti << "Sol:\n";
    sol_->yazdir_agac(cikti, girinti + 4);
    yazdirGirinti(cikti, girinti + 2);
    cikti << "Sag:\n";
    sag_->yazdir_agac(cikti, girinti + 4);
  }

private:
  std::unique_ptr<ASTNode> sol_;
  std::string op_;
  std::unique_ptr<ASTNode> sag_;
};

class SorNode final : public ASTNode {
public:
  SorNode(std::unique_ptr<ASTNode> soruIfadesi, std::size_t satir)
      : ASTNode(satir), soruIfadesi_(std::move(soruIfadesi)) {}

  const ASTNode *soruIfadesi() const { return soruIfadesi_.get(); }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "SorNode [satır " << satir() << "]\n";
    soruIfadesi_->yazdir_agac(cikti, girinti + 2);
  }

private:
  std::unique_ptr<ASTNode> soruIfadesi_;
};

class ListeNode final : public ASTNode {
public:
  ListeNode(std::vector<std::unique_ptr<ASTNode>> ogeler, std::size_t satir)
      : ASTNode(satir), ogeler_(std::move(ogeler)) {}

  const std::vector<std::unique_ptr<ASTNode>> &ogeler() const {
    return ogeler_;
  }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "ListeNode [satır " << satir() << "]\n";
    for (const auto &oge : ogeler_) {
      oge->yazdir_agac(cikti, girinti + 2);
    }
  }

private:
  std::vector<std::unique_ptr<ASTNode>> ogeler_;
};

// Sözlük literal düğümü: { anahtar: deger, ... }
class SozlukNode final : public ASTNode {
public:
  using OgeTipi = std::pair<std::string, std::unique_ptr<ASTNode>>;

  SozlukNode(std::vector<OgeTipi> ogeler, std::size_t satir)
      : ASTNode(satir), ogeler_(std::move(ogeler)) {}

  const std::vector<OgeTipi> &ogeler() const { return ogeler_; }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "SozlukNode [satır " << satir() << "]\n";
    for (const auto &oge : ogeler_) {
      yazdirGirinti(cikti, girinti + 2);
      cikti << "Anahtar: " << oge.first << '\n';
      oge.second->yazdir_agac(cikti, girinti + 4);
    }
  }

private:
  std::vector<OgeTipi> ogeler_;
};

class IndeksErisimNode final : public ASTNode {
public:
  IndeksErisimNode(std::unique_ptr<ASTNode> hedef,
                   std::unique_ptr<ASTNode> indeks, std::size_t satir)
      : ASTNode(satir), hedef_(std::move(hedef)), indeks_(std::move(indeks)) {}

  const ASTNode *hedef() const { return hedef_.get(); }

  const ASTNode *indeks() const { return indeks_.get(); }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "IndeksErisimNode [satır " << satir() << "]\n";
    yazdirGirinti(cikti, girinti + 2);
    cikti << "Hedef:\n";
    hedef_->yazdir_agac(cikti, girinti + 4);
    yazdirGirinti(cikti, girinti + 2);
    cikti << "Indeks:\n";
    indeks_->yazdir_agac(cikti, girinti + 4);
  }

private:
  std::unique_ptr<ASTNode> hedef_;
  std::unique_ptr<ASTNode> indeks_;
};

// Dilim erişimi: hedef[baslangic:bitis]
// baslangic veya bitis boş bırakılabilir (örn: [:3], [2:], [:]).
class DilimErisimNode final : public ASTNode {
public:
  DilimErisimNode(std::unique_ptr<ASTNode> hedef,
                  std::unique_ptr<ASTNode> baslangic,
                  std::unique_ptr<ASTNode> bitis, std::size_t satir)
      : ASTNode(satir), hedef_(std::move(hedef)),
        baslangic_(std::move(baslangic)), bitis_(std::move(bitis)) {}

  const ASTNode *hedef() const { return hedef_.get(); }

  const ASTNode *baslangic() const { return baslangic_.get(); }

  const ASTNode *bitis() const { return bitis_.get(); }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "DilimErisimNode [satır " << satir() << "]\n";
    yazdirGirinti(cikti, girinti + 2);
    cikti << "Hedef:\n";
    hedef_->yazdir_agac(cikti, girinti + 4);
    if (baslangic_ != nullptr) {
      yazdirGirinti(cikti, girinti + 2);
      cikti << "Baslangic:\n";
      baslangic_->yazdir_agac(cikti, girinti + 4);
    }
    if (bitis_ != nullptr) {
      yazdirGirinti(cikti, girinti + 2);
      cikti << "Bitis:\n";
      bitis_->yazdir_agac(cikti, girinti + 4);
    }
  }

private:
  std::unique_ptr<ASTNode> hedef_;
  std::unique_ptr<ASTNode> baslangic_;
  std::unique_ptr<ASTNode> bitis_;
};

// Nokta notasyonu erişimi: hedef.alan
class AlanErisimNode final : public ASTNode {
public:
  AlanErisimNode(std::unique_ptr<ASTNode> hedef, std::string alanAdi,
                 std::size_t satir)
      : ASTNode(satir), hedef_(std::move(hedef)), alanAdi_(std::move(alanAdi)) {
  }

  const ASTNode *hedef() const { return hedef_.get(); }

  const std::string &alanAdi() const { return alanAdi_; }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "AlanErisimNode(" << alanAdi_ << ") [satır " << satir() << "]\n";
    hedef_->yazdir_agac(cikti, girinti + 2);
  }

private:
  std::unique_ptr<ASTNode> hedef_;
  std::string alanAdi_;
};

// benim.alan erişimi için özel düğüm.
class BenimErisimNode final : public ASTNode {
public:
  BenimErisimNode(std::string alanAdi, std::size_t satir)
      : ASTNode(satir), alanAdi_(std::move(alanAdi)) {}

  const std::string &alanAdi() const { return alanAdi_; }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "BenimErisimNode(" << alanAdi_ << ") [satır " << satir()
          << "]\n";
  }

private:
  std::string alanAdi_;
};

// ust.metod erişimi için özel düğüm.
class UstErisimNode final : public ASTNode {
public:
  UstErisimNode(std::string metodAdi, std::size_t satir)
      : ASTNode(satir), metodAdi_(std::move(metodAdi)) {}

  const std::string &metodAdi() const { return metodAdi_; }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "UstErisimNode(" << metodAdi_ << ") [satır " << satir() << "]\n";
  }

private:
  std::string metodAdi_;
};

class IslevCagriNode final : public ASTNode {
public:
  IslevCagriNode(std::string ad,
                 std::vector<std::unique_ptr<ASTNode>> argumanlar,
                 std::size_t satir)
      : ASTNode(satir), ad_(std::move(ad)), argumanlar_(std::move(argumanlar)) {
  }

  const std::string &ad() const { return ad_; }

  const std::vector<std::unique_ptr<ASTNode>> &argumanlar() const {
    return argumanlar_;
  }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "IslevCagriNode(" << ad_ << ") [satır " << satir() << "]\n";
    for (const auto &arg : argumanlar_) {
      arg->yazdir_agac(cikti, girinti + 2);
    }
  }

private:
  std::string ad_;
  std::vector<std::unique_ptr<ASTNode>> argumanlar_;
};

// yeni SinifAdi(...) ifadesi.
class YeniNesneNode final : public ASTNode {
public:
  YeniNesneNode(std::string sinifAdi, std::vector<std::unique_ptr<ASTNode>> argumanlar,
                std::size_t satir)
      : ASTNode(satir), sinifAdi_(std::move(sinifAdi)),
        argumanlar_(std::move(argumanlar)) {}

  const std::string &sinifAdi() const { return sinifAdi_; }

  const std::vector<std::unique_ptr<ASTNode>> &argumanlar() const {
    return argumanlar_;
  }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "YeniNesneNode(" << sinifAdi_ << ") [satır " << satir() << "]\n";
    for (const auto &arg : argumanlar_) {
      arg->yazdir_agac(cikti, girinti + 2);
    }
  }

private:
  std::string sinifAdi_;
  std::vector<std::unique_ptr<ASTNode>> argumanlar_;
};

// [ ifade için degisken içinde kaynak (opsiyonel: eğer kosul) ] üreteci.
class ListeUretecNode final : public ASTNode {
public:
  ListeUretecNode(std::unique_ptr<ASTNode> ifade, std::string donguDegiskeni,
                  std::unique_ptr<ASTNode> kaynakListe,
                  std::unique_ptr<ASTNode> kosul, std::size_t satir)
      : ASTNode(satir), ifade_(std::move(ifade)),
        donguDegiskeni_(std::move(donguDegiskeni)),
        kaynakListe_(std::move(kaynakListe)), kosul_(std::move(kosul)) {}

  const ASTNode *ifade() const { return ifade_.get(); }

  const std::string &donguDegiskeni() const { return donguDegiskeni_; }

  const ASTNode *kaynakListe() const { return kaynakListe_.get(); }

  const ASTNode *kosul() const { return kosul_.get(); }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "ListeUretecNode(" << donguDegiskeni_ << ") [satır " << satir()
          << "]\n";
    yazdirGirinti(cikti, girinti + 2);
    cikti << "Ifade:\n";
    ifade_->yazdir_agac(cikti, girinti + 4);
    yazdirGirinti(cikti, girinti + 2);
    cikti << "Kaynak:\n";
    kaynakListe_->yazdir_agac(cikti, girinti + 4);
    if (kosul_ != nullptr) {
      yazdirGirinti(cikti, girinti + 2);
      cikti << "Kosul:\n";
      kosul_->yazdir_agac(cikti, girinti + 4);
    }
  }

private:
  std::unique_ptr<ASTNode> ifade_;
  std::string donguDegiskeni_;
  std::unique_ptr<ASTNode> kaynakListe_;
  std::unique_ptr<ASTNode> kosul_;
};

class BlockNode final : public ASTNode {
public:
  explicit BlockNode(std::size_t satir) : ASTNode(satir) {}

  void komutEkle(std::unique_ptr<ASTNode> komut) {
    komutlar_.push_back(std::move(komut));
  }

  const std::vector<std::unique_ptr<ASTNode>> &komutlar() const {
    return komutlar_;
  }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "BlockNode [satır " << satir() << "]\n";
    for (const auto &komut : komutlar_) {
      komut->yazdir_agac(cikti, girinti + 2);
    }
  }

private:
  std::vector<std::unique_ptr<ASTNode>> komutlar_;
};

class ProgramNode final : public ASTNode {
public:
  explicit ProgramNode(std::size_t satir = 1) : ASTNode(satir) {}

  void komutEkle(std::unique_ptr<ASTNode> komut) {
    komutlar_.push_back(std::move(komut));
  }

  const std::vector<std::unique_ptr<ASTNode>> &komutlar() const {
    return komutlar_;
  }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "ProgramNode [satır " << satir() << "]\n";
    for (const auto &komut : komutlar_) {
      komut->yazdir_agac(cikti, girinti + 2);
    }
  }

private:
  std::vector<std::unique_ptr<ASTNode>> komutlar_;
};

class AtamaNode final : public ASTNode {
public:
  AtamaNode(std::unique_ptr<ASTNode> hedef, std::unique_ptr<ASTNode> ifade,
            std::size_t satir)
      : ASTNode(satir), hedef_(std::move(hedef)),
        ifade_(std::move(ifade)) {}

  const ASTNode *hedef() const { return hedef_.get(); }

  const ASTNode *ifade() const { return ifade_.get(); }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "AtamaNode [satır " << satir() << "]\n";
    yazdirGirinti(cikti, girinti + 2);
    cikti << "Hedef:\n";
    hedef_->yazdir_agac(cikti, girinti + 4);
    yazdirGirinti(cikti, girinti + 2);
    cikti << "Deger:\n";
    ifade_->yazdir_agac(cikti, girinti + 4);
  }

private:
  std::unique_ptr<ASTNode> hedef_;
  std::unique_ptr<ASTNode> ifade_;
};

class YazdirNode final : public ASTNode {
public:
  YazdirNode(std::unique_ptr<ASTNode> ifade, std::size_t satir)
      : ASTNode(satir), ifade_(std::move(ifade)) {}

  const ASTNode *ifade() const { return ifade_.get(); }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "YazdirNode [satır " << satir() << "]\n";
    ifade_->yazdir_agac(cikti, girinti + 2);
  }

private:
  std::unique_ptr<ASTNode> ifade_;
};

class EgerNode final : public ASTNode {
public:
  EgerNode(std::unique_ptr<ASTNode> kosul, std::unique_ptr<BlockNode> dogruBlok,
           std::unique_ptr<BlockNode> yanlisBlok, std::size_t satir)
      : ASTNode(satir), kosul_(std::move(kosul)),
        dogruBlok_(std::move(dogruBlok)), yanlisBlok_(std::move(yanlisBlok)) {}

  const ASTNode *kosul() const { return kosul_.get(); }

  const BlockNode *dogruBlok() const { return dogruBlok_.get(); }

  const BlockNode *yanlisBlok() const { return yanlisBlok_.get(); }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "EgerNode [satır " << satir() << "]\n";
    yazdirGirinti(cikti, girinti + 2);
    cikti << "Kosul:\n";
    kosul_->yazdir_agac(cikti, girinti + 4);
    yazdirGirinti(cikti, girinti + 2);
    cikti << "DogruBlok:\n";
    dogruBlok_->yazdir_agac(cikti, girinti + 4);
    if (yanlisBlok_ != nullptr) {
      yazdirGirinti(cikti, girinti + 2);
      cikti << "YanlisBlok:\n";
      yanlisBlok_->yazdir_agac(cikti, girinti + 4);
    }
  }

private:
  std::unique_ptr<ASTNode> kosul_;
  std::unique_ptr<BlockNode> dogruBlok_;
  std::unique_ptr<BlockNode> yanlisBlok_;
};

class TekrarlaNode final : public ASTNode {
public:
  TekrarlaNode(std::unique_ptr<ASTNode> kacKezIfadesi,
               std::unique_ptr<BlockNode> govde, std::size_t satir)
      : ASTNode(satir), kacKezIfadesi_(std::move(kacKezIfadesi)),
        govde_(std::move(govde)) {}

  const ASTNode *kacKezIfadesi() const { return kacKezIfadesi_.get(); }

  const BlockNode *govde() const { return govde_.get(); }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "TekrarlaNode [satır " << satir() << "]\n";
    yazdirGirinti(cikti, girinti + 2);
    cikti << "KacKez:\n";
    kacKezIfadesi_->yazdir_agac(cikti, girinti + 4);
    yazdirGirinti(cikti, girinti + 2);
    cikti << "Govde:\n";
    govde_->yazdir_agac(cikti, girinti + 4);
  }

private:
  std::unique_ptr<ASTNode> kacKezIfadesi_;
  std::unique_ptr<BlockNode> govde_;
};

// sürece KOŞUL: KOMUT/BLOK
class SureceNode final : public ASTNode {
public:
  SureceNode(std::unique_ptr<ASTNode> kosul, std::unique_ptr<BlockNode> govde,
             std::size_t satir)
      : ASTNode(satir), kosul_(std::move(kosul)), govde_(std::move(govde)) {}

  const ASTNode *kosul() const { return kosul_.get(); }

  const BlockNode *govde() const { return govde_.get(); }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "SureceNode [satır " << satir() << "]\n";
    yazdirGirinti(cikti, girinti + 2);
    cikti << "Kosul:\n";
    kosul_->yazdir_agac(cikti, girinti + 4);
    yazdirGirinti(cikti, girinti + 2);
    cikti << "Govde:\n";
    govde_->yazdir_agac(cikti, girinti + 4);
  }

private:
  std::unique_ptr<ASTNode> kosul_;
  std::unique_ptr<BlockNode> govde_;
};

class IslevTanimNode final : public ASTNode {
public:
  IslevTanimNode(std::string ad, std::vector<std::string> parametreler,
                 std::unique_ptr<BlockNode> govde, std::size_t satir)
      : ASTNode(satir), ad_(std::move(ad)),
        parametreler_(std::move(parametreler)), govde_(std::move(govde)) {}

  const std::string &ad() const { return ad_; }

  const std::vector<std::string> &parametreler() const { return parametreler_; }

  const BlockNode *govde() const { return govde_.get(); }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "IslevTanimNode(" << ad_ << ") [satır " << satir() << "]\n";
    yazdirGirinti(cikti, girinti + 2);
    cikti << "Parametreler:";
    if (parametreler_.empty()) {
      cikti << " <yok>\n";
    } else {
      cikti << '\n';
      for (const auto &p : parametreler_) {
        yazdirGirinti(cikti, girinti + 4);
        cikti << "- " << p << '\n';
      }
    }
    yazdirGirinti(cikti, girinti + 2);
    cikti << "Govde:\n";
    govde_->yazdir_agac(cikti, girinti + 4);
  }

private:
  std::string ad_;
  std::vector<std::string> parametreler_;
  std::unique_ptr<BlockNode> govde_;
};

class DondurNode final : public ASTNode {
public:
  DondurNode(std::unique_ptr<ASTNode> ifade, std::size_t satir)
      : ASTNode(satir), ifade_(std::move(ifade)) {}

  const ASTNode *ifade() const { return ifade_.get(); }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "DondurNode [satır " << satir() << "]\n";
    ifade_->yazdir_agac(cikti, girinti + 2);
  }

private:
  std::unique_ptr<ASTNode> ifade_;
};

class DahilEtNode final : public ASTNode {
public:
  DahilEtNode(std::string dosyaAdi, std::size_t satir)
      : ASTNode(satir), dosyaAdi_(std::move(dosyaAdi)) {}

  const std::string &dosyaAdi() const { return dosyaAdi_; }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "DahilEtNode(\"" << dosyaAdi_ << "\") [satır " << satir() << "]\n";
  }

private:
  std::string dosyaAdi_;
};

// tip SinifAdi: ... sınıf tanımı.
class SinifTanimNode final : public ASTNode {
public:
  SinifTanimNode(std::string ad, std::string ebeveynAdi,
                 std::unique_ptr<BlockNode> govde,
                 std::size_t satir)
      : ASTNode(satir), ad_(std::move(ad)),
        ebeveynAdi_(std::move(ebeveynAdi)), govde_(std::move(govde)) {}

  const std::string &ad() const { return ad_; }

  const std::string &ebeveynAdi() const { return ebeveynAdi_; }

  const BlockNode *govde() const { return govde_.get(); }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "SinifTanimNode(" << ad_;
    if (!ebeveynAdi_.empty()) {
      cikti << " <- " << ebeveynAdi_;
    }
    cikti << ") [satır " << satir() << "]\n";
    govde_->yazdir_agac(cikti, girinti + 2);
  }

private:
  std::string ad_;
  std::string ebeveynAdi_;
  std::unique_ptr<BlockNode> govde_;
};

// deneme: ... yakala hata: ...
class DenemeYakalaNode final : public ASTNode {
public:
  DenemeYakalaNode(std::unique_ptr<BlockNode> denemeBlogu,
                   std::string hataDegiskeni,
                   std::unique_ptr<BlockNode> yakalaBlogu, std::size_t satir)
      : ASTNode(satir), denemeBlogu_(std::move(denemeBlogu)),
        hataDegiskeni_(std::move(hataDegiskeni)),
        yakalaBlogu_(std::move(yakalaBlogu)) {}

  const BlockNode *denemeBlogu() const { return denemeBlogu_.get(); }

  const std::string &hataDegiskeni() const { return hataDegiskeni_; }

  const BlockNode *yakalaBlogu() const { return yakalaBlogu_.get(); }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "DenemeYakalaNode(hata=" << hataDegiskeni_ << ") [satır "
          << satir() << "]\n";
    yazdirGirinti(cikti, girinti + 2);
    cikti << "Deneme:\n";
    denemeBlogu_->yazdir_agac(cikti, girinti + 4);
    yazdirGirinti(cikti, girinti + 2);
    cikti << "Yakala:\n";
    yakalaBlogu_->yazdir_agac(cikti, girinti + 4);
  }

private:
  std::unique_ptr<BlockNode> denemeBlogu_;
  std::string hataDegiskeni_;
  std::unique_ptr<BlockNode> yakalaBlogu_;
};

class KirNode final : public ASTNode {
public:
  explicit KirNode(std::size_t satir) : ASTNode(satir) {}

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "KirNode [satır " << satir() << "]\n";
  }
};

class DevamNode final : public ASTNode {
public:
  explicit DevamNode(std::size_t satir) : ASTNode(satir) {}

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "DevamNode [satır " << satir() << "]\n";
  }
};

class IfadeKomutNode final : public ASTNode {
public:
  IfadeKomutNode(std::unique_ptr<ASTNode> ifade, std::size_t satir)
      : ASTNode(satir), ifade_(std::move(ifade)) {}

  const ASTNode *ifade() const { return ifade_.get(); }

  void yazdir_agac(std::ostream &cikti, int girinti = 0) const override {
    yazdirGirinti(cikti, girinti);
    cikti << "IfadeKomutNode [satır " << satir() << "]\n";
    ifade_->yazdir_agac(cikti, girinti + 2);
  }

private:
  std::unique_ptr<ASTNode> ifade_;
};
