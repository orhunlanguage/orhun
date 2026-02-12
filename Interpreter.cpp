#include "Interpreter.h"

#include "Lexer.h"
#include "Parser.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
// İşlev içinde döndür yakalamak için iç kontrol sinyali.
struct DondurSinyali {
    explicit DondurSinyali(OrhunDegeri v) : deger(std::move(v)) {}
    OrhunDegeri deger;
};
}  // namespace

Interpreter::Interpreter() {
    gomuluIslevleriYukle();
}

void Interpreter::gomuluIslevleriYukle() {
    gomuluIslevler_["uzunluk"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 1) {
            hataFirlat(satir, "uzunluk(liste_veya_metin) tek argüman alır.");
        }

        const OrhunDegeri& hedef = args[0];
        if (std::holds_alternative<std::string>(hedef.veri)) {
            return OrhunDegeri(static_cast<int>(std::get<std::string>(hedef.veri).size()));
        }
        if (std::holds_alternative<OrhunDegeri::ListeTipi>(hedef.veri)) {
            return OrhunDegeri(static_cast<int>(std::get<OrhunDegeri::ListeTipi>(hedef.veri).size()));
        }
        if (std::holds_alternative<OrhunDegeri::SozlukTipi>(hedef.veri)) {
            return OrhunDegeri(static_cast<int>(std::get<OrhunDegeri::SozlukTipi>(hedef.veri).size()));
        }

        hataFirlat(satir, "uzunluk yalnızca metin, liste veya sözlük üzerinde kullanılabilir.");
    };

    gomuluIslevler_["listeye_ekle"] =
        [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 2) {
            hataFirlat(satir, "listeye_ekle(liste, eleman) iki argüman alır.");
        }

        if (!std::holds_alternative<OrhunDegeri::ListeTipi>(args[0].veri)) {
            hataFirlat(satir, "listeye_ekle fonksiyonunun ilk argümanı liste olmalıdır.");
        }

        OrhunDegeri::ListeTipi yeniListe = std::get<OrhunDegeri::ListeTipi>(args[0].veri);
        yeniListe.push_back(args[1]);
        return OrhunDegeri(std::move(yeniListe));
    };

    gomuluIslevler_["dosya_oku"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 1) {
            hataFirlat(satir, "dosya_oku(\"dosya.oh\") tek argüman alır.");
        }
        if (!std::holds_alternative<std::string>(args[0].veri)) {
            hataFirlat(satir, "dosya_oku için dosya yolu metin olmalıdır.");
        }

        const std::string yol = std::get<std::string>(args[0].veri);
        std::ifstream dosya(yol, std::ios::binary);
        if (!dosya.is_open()) {
            hataFirlat(satir, "'" + yol + "' dosyası okunamadı.");
        }

        std::ostringstream tampon;
        tampon << dosya.rdbuf();
        return OrhunDegeri(tampon.str());
    };

    gomuluIslevler_["dosyaya_yaz"] =
        [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 2) {
            hataFirlat(satir, "dosyaya_yaz(\"dosya.oh\", icerik) iki argüman alır.");
        }
        if (!std::holds_alternative<std::string>(args[0].veri)) {
            hataFirlat(satir, "dosyaya_yaz için dosya yolu metin olmalıdır.");
        }

        const std::string yol = std::get<std::string>(args[0].veri);
        const std::string icerik = metneCevir(args[1]);

        std::ofstream dosya(yol, std::ios::binary | std::ios::trunc);
        if (!dosya.is_open()) {
            hataFirlat(satir, "'" + yol + "' dosyasına yazılamadı.");
        }

        dosya << icerik;
        return OrhunDegeri(1);
    };

    // yazdır ve sor, dilde ayrıca anahtar kelime olarak da var.
    gomuluIslevler_["yazdır"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 1) {
            hataFirlat(satir, "yazdır tek argüman alır.");
        }
        std::cout << metneCevir(args[0]) << '\n';
        return args[0];
    };

    gomuluIslevler_["sor"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 1) {
            hataFirlat(satir, "sor tek argüman alır.");
        }
        std::cout << metneCevir(args[0]);
        std::cout.flush();

        std::string giris;
        std::getline(std::cin, giris);
        if (!std::cin && giris.empty()) {
            hataFirlat(satir, "Kullanıcı girdisi okunamadı.");
        }
        return OrhunDegeri(std::move(giris));
    };
}

void Interpreter::calistir(const ASTNode* dugum) {
    if (dugum == nullptr) {
        hataFirlat(0, "Boş AST düğümü çalıştırılamaz.");
    }

    if (const auto* program = dynamic_cast<const ProgramNode*>(dugum)) {
        for (const auto& komut : program->komutlar()) {
            calistir(komut.get());
        }
        return;
    }

    if (const auto* block = dynamic_cast<const BlockNode*>(dugum)) {
        calistirBlock(block);
        return;
    }

    if (const auto* atama = dynamic_cast<const AtamaNode*>(dugum)) {
        calistirAtama(atama);
        return;
    }

    if (const auto* yazdir = dynamic_cast<const YazdirNode*>(dugum)) {
        calistirYazdir(yazdir);
        return;
    }

    if (const auto* eger = dynamic_cast<const EgerNode*>(dugum)) {
        calistirEger(eger);
        return;
    }

    if (const auto* tekrarla = dynamic_cast<const TekrarlaNode*>(dugum)) {
        calistirTekrarla(tekrarla);
        return;
    }

    if (const auto* islev = dynamic_cast<const IslevTanimNode*>(dugum)) {
        calistirIslevTanim(islev);
        return;
    }

    if (const auto* dondur = dynamic_cast<const DondurNode*>(dugum)) {
        calistirDondur(dondur);
        return;
    }

    if (const auto* dahilEt = dynamic_cast<const DahilEtNode*>(dugum)) {
        calistirDahilEt(dahilEt);
        return;
    }

    if (const auto* ifadeKomut = dynamic_cast<const IfadeKomutNode*>(dugum)) {
        calistirIfadeKomut(ifadeKomut);
        return;
    }

    hataFirlat(dugum->satir(), "Bilinmeyen komut düğümü.");
}

void Interpreter::calistirBlock(const BlockNode* block) {
    for (const auto& komut : block->komutlar()) {
        calistir(komut.get());
    }
}

void Interpreter::calistirAtama(const AtamaNode* dugum) {
    aktifKapsam()[dugum->degiskenAdi()] = ifadeHesapla(dugum->ifade());
}

void Interpreter::calistirYazdir(const YazdirNode* dugum) {
    std::cout << metneCevir(ifadeHesapla(dugum->ifade())) << '\n';
}

void Interpreter::calistirEger(const EgerNode* dugum) {
    const OrhunDegeri kosul = ifadeHesapla(dugum->kosul());
    if (dogruMu(kosul)) {
        calistirBlock(dugum->dogruBlok());
    } else if (dugum->yanlisBlok() != nullptr) {
        calistirBlock(dugum->yanlisBlok());
    }
}

void Interpreter::calistirTekrarla(const TekrarlaNode* dugum) {
    const OrhunDegeri tekrarDegeri = ifadeHesapla(dugum->kacKezIfadesi());
    if (!tamSayiMi(tekrarDegeri)) {
        hataFirlat(dugum->satir(), "'tekrarla' ifadesinde tekrar sayısı tam sayı olmalıdır.");
    }

    const int kez = static_cast<int>(sayiDegeri(tekrarDegeri, dugum->satir(), "tekrar sayısı"));
    if (kez < 0) {
        hataFirlat(dugum->satir(), "'tekrarla' tekrar sayısı negatif olamaz.");
    }

    for (int i = 0; i < kez; ++i) {
        calistirBlock(dugum->govde());
    }
}

void Interpreter::calistirIslevTanim(const IslevTanimNode* dugum) {
    islevTablosu_[dugum->ad()] = dugum;
}

void Interpreter::calistirDondur(const DondurNode* dugum) {
    if (yerelKapsamYigini_.empty()) {
        hataFirlat(dugum->satir(), "'döndür' yalnızca işlev içinde kullanılabilir.");
    }
    throw DondurSinyali(ifadeHesapla(dugum->ifade()));
}

void Interpreter::calistirDahilEt(const DahilEtNode* dugum) {
    std::ifstream dosya(dugum->dosyaAdi(), std::ios::binary);
    if (!dosya.is_open()) {
        hataFirlat(dugum->satir(), "dahil_et: '" + dugum->dosyaAdi() + "' dosyası açılamadı.");
    }

    std::ostringstream tampon;
    tampon << dosya.rdbuf();

    try {
        Lexer lexer(tampon.str());
        std::vector<Token> tokenlar = lexer.tokenize();
        Parser parser(std::move(tokenlar));
        std::unique_ptr<ProgramNode> program = parser.parse();

        ProgramNode* programHam = program.get();
        yukluModuller_.push_back(std::move(program));
        calistir(programHam);
    } catch (const std::exception& ex) {
        hataFirlat(dugum->satir(), "dahil_et içinde hata: " + std::string(ex.what()));
    }
}

void Interpreter::calistirIfadeKomut(const IfadeKomutNode* dugum) {
    static_cast<void>(ifadeHesapla(dugum->ifade()));
}

OrhunDegeri Interpreter::ifadeHesapla(const ASTNode* dugum) {
    if (dugum == nullptr) {
        hataFirlat(0, "Boş ifade düğümü değerlendirilemez.");
    }

    if (const auto* sayi = dynamic_cast<const SayiNode*>(dugum)) {
        try {
            if (sayi->deger().find('.') != std::string::npos) {
                std::size_t okunan = 0;
                const double deger = std::stod(sayi->deger(), &okunan);
                if (okunan != sayi->deger().size()) {
                    hataFirlat(sayi->satir(), "'" + sayi->deger() + "' geçerli bir sayı değil.");
                }
                return OrhunDegeri(deger);
            }

            std::size_t okunan = 0;
            const int deger = std::stoi(sayi->deger(), &okunan, 10);
            if (okunan != sayi->deger().size()) {
                hataFirlat(sayi->satir(), "'" + sayi->deger() + "' geçerli bir sayı değil.");
            }
            return OrhunDegeri(deger);
        } catch (const std::exception&) {
            hataFirlat(sayi->satir(), "'" + sayi->deger() + "' geçerli bir sayı değil.");
        }
    }

    if (const auto* metin = dynamic_cast<const MetinNode*>(dugum)) {
        return OrhunDegeri(metin->deger());
    }

    if (const auto* mantik = dynamic_cast<const MantikNode*>(dugum)) {
        return OrhunDegeri(mantik->deger() ? 1 : 0);
    }

    if (const auto* kimlik = dynamic_cast<const KimlikNode*>(dugum)) {
        return degiskenBul(kimlik->ad(), kimlik->satir());
    }

    if (const auto* tekli = dynamic_cast<const TekliIslemNode*>(dugum)) {
        return tekliIslemHesapla(tekli);
    }

    if (const auto* ikili = dynamic_cast<const IkiliIslemNode*>(dugum)) {
        return ikiliIslemHesapla(ikili);
    }

    if (const auto* sor = dynamic_cast<const SorNode*>(dugum)) {
        return sorCalistir(sor);
    }

    if (const auto* liste = dynamic_cast<const ListeNode*>(dugum)) {
        return listeOlustur(liste);
    }

    if (const auto* sozluk = dynamic_cast<const SozlukNode*>(dugum)) {
        return sozlukOlustur(sozluk);
    }

    if (const auto* indeks = dynamic_cast<const IndeksErisimNode*>(dugum)) {
        return indeksErisim(indeks);
    }

    if (const auto* alan = dynamic_cast<const AlanErisimNode*>(dugum)) {
        return alanErisim(alan);
    }

    if (const auto* cagri = dynamic_cast<const IslevCagriNode*>(dugum)) {
        return islevCagir(cagri);
    }

    hataFirlat(dugum->satir(), "İfade değerlendirme sırasında geçersiz düğüm türü alındı.");
}

OrhunDegeri Interpreter::tekliIslemHesapla(const TekliIslemNode* dugum) {
    const OrhunDegeri deger = ifadeHesapla(dugum->ifade());

    if (dugum->op() == "değil") {
        return OrhunDegeri(dogruMu(deger) ? 0 : 1);
    }

    if (dugum->op() == "-") {
        if (std::holds_alternative<int>(deger.veri)) {
            return OrhunDegeri(-std::get<int>(deger.veri));
        }
        if (std::holds_alternative<double>(deger.veri)) {
            return OrhunDegeri(-std::get<double>(deger.veri));
        }
        hataFirlat(dugum->satir(), "Tekli '-' yalnızca sayılarla kullanılabilir.");
    }

    hataFirlat(dugum->satir(), "Bilinmeyen tekli operatör: " + dugum->op());
}

OrhunDegeri Interpreter::ikiliIslemHesapla(const IkiliIslemNode* dugum) {
    const std::string& op = dugum->op();

    // ve/veya için kısa devre mantığı.
    if (op == "ve") {
        const OrhunDegeri sol = ifadeHesapla(dugum->sol());
        if (!dogruMu(sol)) {
            return OrhunDegeri(0);
        }
        return OrhunDegeri(dogruMu(ifadeHesapla(dugum->sag())) ? 1 : 0);
    }

    if (op == "veya") {
        const OrhunDegeri sol = ifadeHesapla(dugum->sol());
        if (dogruMu(sol)) {
            return OrhunDegeri(1);
        }
        return OrhunDegeri(dogruMu(ifadeHesapla(dugum->sag())) ? 1 : 0);
    }

    const OrhunDegeri sol = ifadeHesapla(dugum->sol());
    const OrhunDegeri sag = ifadeHesapla(dugum->sag());

    if (op == "eşit") {
        return OrhunDegeri(esittir(sol, sag) ? 1 : 0);
    }
    if (op == "eşit_değil") {
        return OrhunDegeri(esittir(sol, sag) ? 0 : 1);
    }
    if (op == "büyük" || op == "küçük") {
        const double a = sayiDegeri(sol, dugum->satir(), "karşılaştırma");
        const double b = sayiDegeri(sag, dugum->satir(), "karşılaştırma");
        if (op == "büyük") {
            return OrhunDegeri(a > b ? 1 : 0);
        }
        return OrhunDegeri(a < b ? 1 : 0);
    }

    if (op == "+" || op == "-" || op == "*" || op == "/") {
        return listeIslemi(sol, sag, op, dugum->satir());
    }

    hataFirlat(dugum->satir(), "Bilinmeyen ikili operatör: " + op);
}

OrhunDegeri Interpreter::listeIslemi(const OrhunDegeri& sol,
                                     const OrhunDegeri& sag,
                                     const std::string& op,
                                     std::size_t satir) {
    const bool solListe = std::holds_alternative<OrhunDegeri::ListeTipi>(sol.veri);
    const bool sagListe = std::holds_alternative<OrhunDegeri::ListeTipi>(sag.veri);

    // + operatöründe en az bir taraf metinse metin birleştirme yapılır.
    if (op == "+" &&
        (std::holds_alternative<std::string>(sol.veri) || std::holds_alternative<std::string>(sag.veri))) {
        return OrhunDegeri(metneCevir(sol) + metneCevir(sag));
    }

    const bool solSayi = sayiMi(sol);
    const bool sagSayi = sayiMi(sag);

    if (solSayi && sagSayi) {
        const double a = sayiDegeri(sol, satir, "aritmetik işlem");
        const double b = sayiDegeri(sag, satir, "aritmetik işlem");

        if (op == "+") {
            if (std::holds_alternative<int>(sol.veri) && std::holds_alternative<int>(sag.veri)) {
                return OrhunDegeri(static_cast<int>(a + b));
            }
            return OrhunDegeri(a + b);
        }
        if (op == "-") {
            if (std::holds_alternative<int>(sol.veri) && std::holds_alternative<int>(sag.veri)) {
                return OrhunDegeri(static_cast<int>(a - b));
            }
            return OrhunDegeri(a - b);
        }
        if (op == "*") {
            if (std::holds_alternative<int>(sol.veri) && std::holds_alternative<int>(sag.veri)) {
                return OrhunDegeri(static_cast<int>(a * b));
            }
            return OrhunDegeri(a * b);
        }
        if (op == "/") {
            if (std::fabs(b) < 1e-12) {
                hataFirlat(satir, "Sıfıra bölme yapılamaz.");
            }
            return OrhunDegeri(a / b);
        }

        hataFirlat(satir, "Bilinmeyen aritmetik operatör: " + op);
    }

    if (solListe && sagListe) {
        const auto& a = std::get<OrhunDegeri::ListeTipi>(sol.veri);
        const auto& b = std::get<OrhunDegeri::ListeTipi>(sag.veri);

        if (a.size() != b.size()) {
            hataFirlat(satir, "Matris boyutları eşleşmiyor");
        }

        OrhunDegeri::ListeTipi sonuc;
        sonuc.reserve(a.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            sonuc.push_back(listeIslemi(a[i], b[i], op, satir));
        }
        return OrhunDegeri(std::move(sonuc));
    }

    if (solListe && sagSayi) {
        const auto& a = std::get<OrhunDegeri::ListeTipi>(sol.veri);
        OrhunDegeri::ListeTipi sonuc;
        sonuc.reserve(a.size());
        for (const auto& oge : a) {
            sonuc.push_back(listeIslemi(oge, sag, op, satir));
        }
        return OrhunDegeri(std::move(sonuc));
    }

    if (solSayi && sagListe) {
        const auto& b = std::get<OrhunDegeri::ListeTipi>(sag.veri);
        OrhunDegeri::ListeTipi sonuc;
        sonuc.reserve(b.size());
        for (const auto& oge : b) {
            sonuc.push_back(listeIslemi(sol, oge, op, satir));
        }
        return OrhunDegeri(std::move(sonuc));
    }

    hataFirlat(satir, "Tip uyuşmazlığı: '" + op + "' işlemi bu tiplerle kullanılamaz.");
}

OrhunDegeri Interpreter::sorCalistir(const SorNode* dugum) {
    const OrhunDegeri soru = ifadeHesapla(dugum->soruIfadesi());
    std::cout << metneCevir(soru);
    std::cout.flush();

    std::string giris;
    std::getline(std::cin, giris);
    if (!std::cin && giris.empty()) {
        hataFirlat(dugum->satir(), "Kullanıcı girdisi okunamadı.");
    }

    return OrhunDegeri(std::move(giris));
}

OrhunDegeri Interpreter::listeOlustur(const ListeNode* dugum) {
    OrhunDegeri::ListeTipi liste;
    liste.reserve(dugum->ogeler().size());

    for (const auto& oge : dugum->ogeler()) {
        liste.push_back(ifadeHesapla(oge.get()));
    }

    return OrhunDegeri(std::move(liste));
}

OrhunDegeri Interpreter::sozlukOlustur(const SozlukNode* dugum) {
    OrhunDegeri::SozlukTipi sozluk;
    for (const auto& oge : dugum->ogeler()) {
        sozluk[oge.first] = ifadeHesapla(oge.second.get());
    }
    return OrhunDegeri(std::move(sozluk));
}

OrhunDegeri Interpreter::indeksErisim(const IndeksErisimNode* dugum) {
    const OrhunDegeri hedef = ifadeHesapla(dugum->hedef());
    const OrhunDegeri indeks = ifadeHesapla(dugum->indeks());

    if (std::holds_alternative<OrhunDegeri::ListeTipi>(hedef.veri)) {
        const auto& liste = std::get<OrhunDegeri::ListeTipi>(hedef.veri);
        const std::size_t idx = listeIndeksiCevir(indeks, dugum->satir(), "liste indeksi");
        if (idx >= liste.size()) {
            hataFirlat(dugum->satir(), "Liste indeksi sınır dışında.");
        }
        return liste[idx];
    }

    if (std::holds_alternative<OrhunDegeri::SozlukTipi>(hedef.veri)) {
        if (!std::holds_alternative<std::string>(indeks.veri)) {
            hataFirlat(dugum->satir(), "Sözlük anahtarı metin olmalıdır.");
        }

        const std::string& anahtar = std::get<std::string>(indeks.veri);
        const auto& sozluk = std::get<OrhunDegeri::SozlukTipi>(hedef.veri);
        const auto bulunan = sozluk.find(anahtar);
        if (bulunan == sozluk.end()) {
            hataFirlat(dugum->satir(), "'" + anahtar + "' anahtarı sözlükte bulunamadı!");
        }
        return bulunan->second;
    }

    hataFirlat(dugum->satir(), "İndeks erişimi yalnızca liste veya sözlük üzerinde kullanılabilir.");
}

OrhunDegeri Interpreter::alanErisim(const AlanErisimNode* dugum) {
    const OrhunDegeri hedef = ifadeHesapla(dugum->hedef());
    if (!std::holds_alternative<OrhunDegeri::SozlukTipi>(hedef.veri)) {
        hataFirlat(dugum->satir(), "Nokta erişimi yalnızca sözlük üzerinde kullanılabilir.");
    }

    const auto& sozluk = std::get<OrhunDegeri::SozlukTipi>(hedef.veri);
    const auto bulunan = sozluk.find(dugum->alanAdi());
    if (bulunan == sozluk.end()) {
        hataFirlat(dugum->satir(), "'" + dugum->alanAdi() + "' anahtarı sözlükte bulunamadı!");
    }
    return bulunan->second;
}

OrhunDegeri Interpreter::islevCagir(const IslevCagriNode* dugum) {
    const auto yerelIslev = islevTablosu_.find(dugum->ad());
    if (yerelIslev != islevTablosu_.end()) {
        const IslevTanimNode* islev = yerelIslev->second;
        if (dugum->argumanlar().size() != islev->parametreler().size()) {
            hataFirlat(dugum->satir(), "'" + dugum->ad() + "' için argüman sayısı uyuşmuyor.");
        }

        DegiskenTablosu yeniKapsam;
        for (std::size_t i = 0; i < dugum->argumanlar().size(); ++i) {
            yeniKapsam[islev->parametreler()[i]] = ifadeHesapla(dugum->argumanlar()[i].get());
        }

        yerelKapsamYigini_.push_back(std::move(yeniKapsam));
        try {
            calistirBlock(islev->govde());
        } catch (const DondurSinyali& sinyal) {
            yerelKapsamYigini_.pop_back();
            return sinyal.deger;
        } catch (...) {
            yerelKapsamYigini_.pop_back();
            throw;
        }

        yerelKapsamYigini_.pop_back();
        hataFirlat(dugum->satir(), "'" + dugum->ad() + "' işlevi bir değer döndürmedi.");
    }

    const auto gomulu = gomuluIslevler_.find(dugum->ad());
    if (gomulu != gomuluIslevler_.end()) {
        std::vector<OrhunDegeri> argumanDegerleri;
        argumanDegerleri.reserve(dugum->argumanlar().size());
        for (const auto& arg : dugum->argumanlar()) {
            argumanDegerleri.push_back(ifadeHesapla(arg.get()));
        }
        return gomulu->second(argumanDegerleri, dugum->satir());
    }

    hataFirlat(dugum->satir(), "'" + dugum->ad() + "' adlı işlev bulunamadı.");
}

Interpreter::DegiskenTablosu& Interpreter::aktifKapsam() {
    if (!yerelKapsamYigini_.empty()) {
        return yerelKapsamYigini_.back();
    }
    return globalHafiza_;
}

const OrhunDegeri& Interpreter::degiskenBul(const std::string& ad, std::size_t satir) const {
    for (auto it = yerelKapsamYigini_.rbegin(); it != yerelKapsamYigini_.rend(); ++it) {
        const auto bulunan = it->find(ad);
        if (bulunan != it->end()) {
            return bulunan->second;
        }
    }

    const auto global = globalHafiza_.find(ad);
    if (global != globalHafiza_.end()) {
        return global->second;
    }

    throw std::runtime_error("Satır " + std::to_string(satir) + ": '" + ad + "' değişkeni bulunamadı.");
}

bool Interpreter::dogruMu(const OrhunDegeri& deger) const {
    if (std::holds_alternative<int>(deger.veri)) {
        return std::get<int>(deger.veri) != 0;
    }
    if (std::holds_alternative<double>(deger.veri)) {
        return std::fabs(std::get<double>(deger.veri)) > 1e-12;
    }
    if (std::holds_alternative<std::string>(deger.veri)) {
        return !std::get<std::string>(deger.veri).empty();
    }
    if (std::holds_alternative<OrhunDegeri::ListeTipi>(deger.veri)) {
        return !std::get<OrhunDegeri::ListeTipi>(deger.veri).empty();
    }
    return !std::get<OrhunDegeri::SozlukTipi>(deger.veri).empty();
}

bool Interpreter::esittir(const OrhunDegeri& sol, const OrhunDegeri& sag) const {
    if (sayiMi(sol) && sayiMi(sag)) {
        return std::fabs(sayiDegeri(sol, 0, "eşitlik") - sayiDegeri(sag, 0, "eşitlik")) < 1e-12;
    }
    return sol == sag;
}

std::string Interpreter::metneCevir(const OrhunDegeri& deger) const {
    if (std::holds_alternative<int>(deger.veri)) {
        return std::to_string(std::get<int>(deger.veri));
    }

    if (std::holds_alternative<double>(deger.veri)) {
        std::ostringstream os;
        os << std::get<double>(deger.veri);
        std::string sonuc = os.str();

        // İnsan okunabilirlik için gereksiz sıfırları temizle.
        if (sonuc.find('.') != std::string::npos) {
            while (!sonuc.empty() && sonuc.back() == '0') {
                sonuc.pop_back();
            }
            if (!sonuc.empty() && sonuc.back() == '.') {
                sonuc.pop_back();
            }
            if (sonuc.empty()) {
                sonuc = "0";
            }
        }
        return sonuc;
    }

    if (std::holds_alternative<std::string>(deger.veri)) {
        return std::get<std::string>(deger.veri);
    }

    if (std::holds_alternative<OrhunDegeri::ListeTipi>(deger.veri)) {
        const auto& liste = std::get<OrhunDegeri::ListeTipi>(deger.veri);
        std::string sonuc = "[";
        for (std::size_t i = 0; i < liste.size(); ++i) {
            if (i > 0) {
                sonuc += ", ";
            }
            sonuc += metneCevir(liste[i]);
        }
        sonuc += "]";
        return sonuc;
    }

    const auto& sozluk = std::get<OrhunDegeri::SozlukTipi>(deger.veri);
    std::string sonuc = "{";
    bool ilk = true;
    for (const auto& [anahtar, degerIc] : sozluk) {
        if (!ilk) {
            sonuc += ", ";
        }
        ilk = false;
        sonuc += anahtar + ": " + metneCevir(degerIc);
    }
    sonuc += "}";
    return sonuc;
}

bool Interpreter::sayiMi(const OrhunDegeri& deger) const {
    return std::holds_alternative<int>(deger.veri) || std::holds_alternative<double>(deger.veri);
}

double Interpreter::sayiDegeri(const OrhunDegeri& deger, std::size_t satir, const std::string& baglam) const {
    if (std::holds_alternative<int>(deger.veri)) {
        return static_cast<double>(std::get<int>(deger.veri));
    }
    if (std::holds_alternative<double>(deger.veri)) {
        return std::get<double>(deger.veri);
    }

    if (satir == 0) {
        throw std::runtime_error("Hata: " + baglam + " için sayı bekleniyordu.");
    }
    throw std::runtime_error("Satır " + std::to_string(satir) + ": " + baglam + " için sayı bekleniyordu.");
}

bool Interpreter::tamSayiMi(const OrhunDegeri& deger) const {
    if (std::holds_alternative<int>(deger.veri)) {
        return true;
    }
    if (std::holds_alternative<double>(deger.veri)) {
        return tamSayiMi(std::get<double>(deger.veri));
    }
    return false;
}

bool Interpreter::tamSayiMi(double deger) const {
    return std::fabs(deger - std::round(deger)) < 1e-12;
}

std::size_t Interpreter::listeIndeksiCevir(const OrhunDegeri& deger,
                                           std::size_t satir,
                                           const std::string& baglam) const {
    double hamDeger = 0.0;
    if (std::holds_alternative<int>(deger.veri)) {
        hamDeger = static_cast<double>(std::get<int>(deger.veri));
    } else if (std::holds_alternative<double>(deger.veri)) {
        hamDeger = std::get<double>(deger.veri);
    } else {
        hataFirlat(satir, baglam + " tam sayı olmalıdır.");
    }

    if (!tamSayiMi(hamDeger)) {
        hataFirlat(satir, baglam + " tam sayı olmalıdır.");
    }
    if (hamDeger < 0.0) {
        hataFirlat(satir, baglam + " negatif olamaz.");
    }

    return static_cast<std::size_t>(hamDeger);
}

[[noreturn]] void Interpreter::hataFirlat(std::size_t satir, const std::string& mesaj) const {
    if (satir == 0) {
        throw std::runtime_error("Hata: " + mesaj);
    }
    throw std::runtime_error("Satır " + std::to_string(satir) + ": " + mesaj);
}
