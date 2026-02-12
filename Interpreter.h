#pragma once

#include "AST.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// Orhun çalışma zamanı değeri.
// v0.5: sayı(int/double), metin, liste ve sözlük desteklenir.
struct OrhunDegeri {
    using ListeTipi = std::vector<OrhunDegeri>;
    using SozlukTipi = std::map<std::string, OrhunDegeri>;

    std::variant<int, double, std::string, ListeTipi, SozlukTipi> veri;

    OrhunDegeri() : veri(0) {}
    explicit OrhunDegeri(int v) : veri(v) {}
    explicit OrhunDegeri(double v) : veri(v) {}
    explicit OrhunDegeri(std::string v) : veri(std::move(v)) {}
    explicit OrhunDegeri(const char* v) : veri(std::string(v)) {}
    explicit OrhunDegeri(ListeTipi v) : veri(std::move(v)) {}
    explicit OrhunDegeri(SozlukTipi v) : veri(std::move(v)) {}

    bool operator==(const OrhunDegeri& diger) const {
        return veri == diger.veri;
    }
};

class Interpreter {
public:
    Interpreter();

    // Program veya herhangi bir komut düğümünü çalıştırır.
    void calistir(const ASTNode* dugum);

private:
    using DegiskenTablosu = std::unordered_map<std::string, OrhunDegeri>;
    using GomuluIslev = std::function<OrhunDegeri(const std::vector<OrhunDegeri>&, std::size_t)>;

    DegiskenTablosu globalHafiza_;
    std::vector<DegiskenTablosu> yerelKapsamYigini_;

    std::unordered_map<std::string, const IslevTanimNode*> islevTablosu_;
    std::unordered_map<std::string, GomuluIslev> gomuluIslevler_;
    std::vector<std::unique_ptr<ProgramNode>> yukluModuller_;

    void gomuluIslevleriYukle();

    void calistirBlock(const BlockNode* block);
    void calistirAtama(const AtamaNode* dugum);
    void calistirYazdir(const YazdirNode* dugum);
    void calistirEger(const EgerNode* dugum);
    void calistirTekrarla(const TekrarlaNode* dugum);
    void calistirIslevTanim(const IslevTanimNode* dugum);
    void calistirDondur(const DondurNode* dugum);
    void calistirDahilEt(const DahilEtNode* dugum);
    void calistirIfadeKomut(const IfadeKomutNode* dugum);

    OrhunDegeri ifadeHesapla(const ASTNode* dugum);
    OrhunDegeri tekliIslemHesapla(const TekliIslemNode* dugum);
    OrhunDegeri ikiliIslemHesapla(const IkiliIslemNode* dugum);
    OrhunDegeri listeIslemi(const OrhunDegeri& sol,
                            const OrhunDegeri& sag,
                            const std::string& op,
                            std::size_t satir);
    OrhunDegeri sorCalistir(const SorNode* dugum);
    OrhunDegeri listeOlustur(const ListeNode* dugum);
    OrhunDegeri sozlukOlustur(const SozlukNode* dugum);
    OrhunDegeri indeksErisim(const IndeksErisimNode* dugum);
    OrhunDegeri alanErisim(const AlanErisimNode* dugum);
    OrhunDegeri islevCagir(const IslevCagriNode* dugum);

    DegiskenTablosu& aktifKapsam();
    const OrhunDegeri& degiskenBul(const std::string& ad, std::size_t satir) const;

    bool dogruMu(const OrhunDegeri& deger) const;
    bool esittir(const OrhunDegeri& sol, const OrhunDegeri& sag) const;
    std::string metneCevir(const OrhunDegeri& deger) const;

    // Sayı yardımcıları: int/double birlikte işlenir.
    bool sayiMi(const OrhunDegeri& deger) const;
    double sayiDegeri(const OrhunDegeri& deger, std::size_t satir, const std::string& baglam) const;
    bool tamSayiMi(const OrhunDegeri& deger) const;
    bool tamSayiMi(double deger) const;
    std::size_t listeIndeksiCevir(const OrhunDegeri& deger, std::size_t satir, const std::string& baglam) const;

    [[noreturn]] void hataFirlat(std::size_t satir, const std::string& mesaj) const;
};
