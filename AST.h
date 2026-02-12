#pragma once

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// AST yazdırırken ortak girinti yardımcı fonksiyonu.
inline void yazdirGirinti(std::ostream& cikti, int girinti) {
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

    std::size_t satir() const {
        return satir_;
    }

    virtual void yazdir_agac(std::ostream& cikti, int girinti = 0) const = 0;

private:
    std::size_t satir_;
};

class SayiNode final : public ASTNode {
public:
    SayiNode(std::string deger, std::size_t satir) : ASTNode(satir), deger_(std::move(deger)) {}

    const std::string& deger() const {
        return deger_;
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
        yazdirGirinti(cikti, girinti);
        cikti << "SayiNode(" << deger_ << ") [satır " << satir() << "]\n";
    }

private:
    std::string deger_;
};

class MetinNode final : public ASTNode {
public:
    MetinNode(std::string deger, std::size_t satir) : ASTNode(satir), deger_(std::move(deger)) {}

    const std::string& deger() const {
        return deger_;
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
        yazdirGirinti(cikti, girinti);
        cikti << "MetinNode(\"" << deger_ << "\") [satır " << satir() << "]\n";
    }

private:
    std::string deger_;
};

class MantikNode final : public ASTNode {
public:
    MantikNode(bool deger, std::size_t satir) : ASTNode(satir), deger_(deger) {}

    bool deger() const {
        return deger_;
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
        yazdirGirinti(cikti, girinti);
        cikti << "MantikNode(" << (deger_ ? "doğru" : "yanlış") << ") [satır " << satir() << "]\n";
    }

private:
    bool deger_;
};

class KimlikNode final : public ASTNode {
public:
    KimlikNode(std::string ad, std::size_t satir) : ASTNode(satir), ad_(std::move(ad)) {}

    const std::string& ad() const {
        return ad_;
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
        yazdirGirinti(cikti, girinti);
        cikti << "KimlikNode(" << ad_ << ") [satır " << satir() << "]\n";
    }

private:
    std::string ad_;
};

class TekliIslemNode final : public ASTNode {
public:
    TekliIslemNode(std::string op, std::unique_ptr<ASTNode> ifade, std::size_t satir)
        : ASTNode(satir), op_(std::move(op)), ifade_(std::move(ifade)) {}

    const std::string& op() const {
        return op_;
    }

    const ASTNode* ifade() const {
        return ifade_.get();
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
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
    IkiliIslemNode(std::unique_ptr<ASTNode> sol,
                   std::string op,
                   std::unique_ptr<ASTNode> sag,
                   std::size_t satir)
        : ASTNode(satir), sol_(std::move(sol)), op_(std::move(op)), sag_(std::move(sag)) {}

    const ASTNode* sol() const {
        return sol_.get();
    }

    const std::string& op() const {
        return op_;
    }

    const ASTNode* sag() const {
        return sag_.get();
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
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

    const ASTNode* soruIfadesi() const {
        return soruIfadesi_.get();
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
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

    const std::vector<std::unique_ptr<ASTNode>>& ogeler() const {
        return ogeler_;
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
        yazdirGirinti(cikti, girinti);
        cikti << "ListeNode [satır " << satir() << "]\n";
        for (const auto& oge : ogeler_) {
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

    const std::vector<OgeTipi>& ogeler() const {
        return ogeler_;
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
        yazdirGirinti(cikti, girinti);
        cikti << "SozlukNode [satır " << satir() << "]\n";
        for (const auto& oge : ogeler_) {
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
    IndeksErisimNode(std::unique_ptr<ASTNode> hedef, std::unique_ptr<ASTNode> indeks, std::size_t satir)
        : ASTNode(satir), hedef_(std::move(hedef)), indeks_(std::move(indeks)) {}

    const ASTNode* hedef() const {
        return hedef_.get();
    }

    const ASTNode* indeks() const {
        return indeks_.get();
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
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

// Nokta notasyonu erişimi: hedef.alan
class AlanErisimNode final : public ASTNode {
public:
    AlanErisimNode(std::unique_ptr<ASTNode> hedef, std::string alanAdi, std::size_t satir)
        : ASTNode(satir), hedef_(std::move(hedef)), alanAdi_(std::move(alanAdi)) {}

    const ASTNode* hedef() const {
        return hedef_.get();
    }

    const std::string& alanAdi() const {
        return alanAdi_;
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
        yazdirGirinti(cikti, girinti);
        cikti << "AlanErisimNode(" << alanAdi_ << ") [satır " << satir() << "]\n";
        hedef_->yazdir_agac(cikti, girinti + 2);
    }

private:
    std::unique_ptr<ASTNode> hedef_;
    std::string alanAdi_;
};

class IslevCagriNode final : public ASTNode {
public:
    IslevCagriNode(std::string ad, std::vector<std::unique_ptr<ASTNode>> argumanlar, std::size_t satir)
        : ASTNode(satir), ad_(std::move(ad)), argumanlar_(std::move(argumanlar)) {}

    const std::string& ad() const {
        return ad_;
    }

    const std::vector<std::unique_ptr<ASTNode>>& argumanlar() const {
        return argumanlar_;
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
        yazdirGirinti(cikti, girinti);
        cikti << "IslevCagriNode(" << ad_ << ") [satır " << satir() << "]\n";
        for (const auto& arg : argumanlar_) {
            arg->yazdir_agac(cikti, girinti + 2);
        }
    }

private:
    std::string ad_;
    std::vector<std::unique_ptr<ASTNode>> argumanlar_;
};

class BlockNode final : public ASTNode {
public:
    explicit BlockNode(std::size_t satir) : ASTNode(satir) {}

    void komutEkle(std::unique_ptr<ASTNode> komut) {
        komutlar_.push_back(std::move(komut));
    }

    const std::vector<std::unique_ptr<ASTNode>>& komutlar() const {
        return komutlar_;
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
        yazdirGirinti(cikti, girinti);
        cikti << "BlockNode [satır " << satir() << "]\n";
        for (const auto& komut : komutlar_) {
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

    const std::vector<std::unique_ptr<ASTNode>>& komutlar() const {
        return komutlar_;
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
        yazdirGirinti(cikti, girinti);
        cikti << "ProgramNode [satır " << satir() << "]\n";
        for (const auto& komut : komutlar_) {
            komut->yazdir_agac(cikti, girinti + 2);
        }
    }

private:
    std::vector<std::unique_ptr<ASTNode>> komutlar_;
};

class AtamaNode final : public ASTNode {
public:
    AtamaNode(std::string degiskenAdi, std::unique_ptr<ASTNode> ifade, std::size_t satir)
        : ASTNode(satir), degiskenAdi_(std::move(degiskenAdi)), ifade_(std::move(ifade)) {}

    const std::string& degiskenAdi() const {
        return degiskenAdi_;
    }

    const ASTNode* ifade() const {
        return ifade_.get();
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
        yazdirGirinti(cikti, girinti);
        cikti << "AtamaNode(" << degiskenAdi_ << ") [satır " << satir() << "]\n";
        ifade_->yazdir_agac(cikti, girinti + 2);
    }

private:
    std::string degiskenAdi_;
    std::unique_ptr<ASTNode> ifade_;
};

class YazdirNode final : public ASTNode {
public:
    YazdirNode(std::unique_ptr<ASTNode> ifade, std::size_t satir)
        : ASTNode(satir), ifade_(std::move(ifade)) {}

    const ASTNode* ifade() const {
        return ifade_.get();
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
        yazdirGirinti(cikti, girinti);
        cikti << "YazdirNode [satır " << satir() << "]\n";
        ifade_->yazdir_agac(cikti, girinti + 2);
    }

private:
    std::unique_ptr<ASTNode> ifade_;
};

class EgerNode final : public ASTNode {
public:
    EgerNode(std::unique_ptr<ASTNode> kosul,
             std::unique_ptr<BlockNode> dogruBlok,
             std::unique_ptr<BlockNode> yanlisBlok,
             std::size_t satir)
        : ASTNode(satir),
          kosul_(std::move(kosul)),
          dogruBlok_(std::move(dogruBlok)),
          yanlisBlok_(std::move(yanlisBlok)) {}

    const ASTNode* kosul() const {
        return kosul_.get();
    }

    const BlockNode* dogruBlok() const {
        return dogruBlok_.get();
    }

    const BlockNode* yanlisBlok() const {
        return yanlisBlok_.get();
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
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
    TekrarlaNode(std::unique_ptr<ASTNode> kacKezIfadesi, std::unique_ptr<BlockNode> govde, std::size_t satir)
        : ASTNode(satir), kacKezIfadesi_(std::move(kacKezIfadesi)), govde_(std::move(govde)) {}

    const ASTNode* kacKezIfadesi() const {
        return kacKezIfadesi_.get();
    }

    const BlockNode* govde() const {
        return govde_.get();
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
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

class IslevTanimNode final : public ASTNode {
public:
    IslevTanimNode(std::string ad,
                   std::vector<std::string> parametreler,
                   std::unique_ptr<BlockNode> govde,
                   std::size_t satir)
        : ASTNode(satir), ad_(std::move(ad)), parametreler_(std::move(parametreler)), govde_(std::move(govde)) {}

    const std::string& ad() const {
        return ad_;
    }

    const std::vector<std::string>& parametreler() const {
        return parametreler_;
    }

    const BlockNode* govde() const {
        return govde_.get();
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
        yazdirGirinti(cikti, girinti);
        cikti << "IslevTanimNode(" << ad_ << ") [satır " << satir() << "]\n";
        yazdirGirinti(cikti, girinti + 2);
        cikti << "Parametreler:";
        if (parametreler_.empty()) {
            cikti << " <yok>\n";
        } else {
            cikti << '\n';
            for (const auto& p : parametreler_) {
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

    const ASTNode* ifade() const {
        return ifade_.get();
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
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

    const std::string& dosyaAdi() const {
        return dosyaAdi_;
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
        yazdirGirinti(cikti, girinti);
        cikti << "DahilEtNode(\"" << dosyaAdi_ << "\") [satır " << satir() << "]\n";
    }

private:
    std::string dosyaAdi_;
};

class IfadeKomutNode final : public ASTNode {
public:
    IfadeKomutNode(std::unique_ptr<ASTNode> ifade, std::size_t satir)
        : ASTNode(satir), ifade_(std::move(ifade)) {}

    const ASTNode* ifade() const {
        return ifade_.get();
    }

    void yazdir_agac(std::ostream& cikti, int girinti = 0) const override {
        yazdirGirinti(cikti, girinti);
        cikti << "IfadeKomutNode [satır " << satir() << "]\n";
        ifade_->yazdir_agac(cikti, girinti + 2);
    }

private:
    std::unique_ptr<ASTNode> ifade_;
};
