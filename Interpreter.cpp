#include "Interpreter.h"

#include "Lexer.h"
#include "Parser.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <utility>

namespace {
// İşlev içinde döndür yakalamak için iç kontrol sinyali.
struct DondurSinyali {
    explicit DondurSinyali(OrhunDegeri v) : deger(std::move(v)) {}
    OrhunDegeri deger;
};

// Döngü kontrol sinyalleri.
struct KirSinyali {};
struct DevamSinyali {};

std::vector<std::string> noktaIleBol(const std::string& metin) {
    std::vector<std::string> parcalar;
    std::size_t bas = 0;
    while (true) {
        const std::size_t nokta = metin.find('.', bas);
        if (nokta == std::string::npos) {
            parcalar.push_back(metin.substr(bas));
            break;
        }
        parcalar.push_back(metin.substr(bas, nokta - bas));
        bas = nokta + 1;
    }
    return parcalar;
}
}  // namespace

Interpreter::Interpreter() {
    gomuluIslevleriYukle();
    yerlesikModulleriYukle();
}

void Interpreter::gomuluIslevleriYukle() {
    // Genel amaçlı yardımcı kütüphane.
    gomuluIslevler_["uzunluk"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 1) {
            hataFirlat(satir, "uzunluk(liste_veya_metin) tek argüman alır.");
        }

        const OrhunDegeri& hedef = args[0];
        if (std::holds_alternative<std::string>(hedef.veri)) {
            return OrhunDegeri(static_cast<int>(std::get<std::string>(hedef.veri).size()));
        }
        if (std::holds_alternative<OrhunDegeri::ListeTipi>(hedef.veri)) {
            const auto& liste = std::get<OrhunDegeri::ListeTipi>(hedef.veri);
            return OrhunDegeri(static_cast<int>(liste ? liste->size() : 0));
        }
        if (std::holds_alternative<OrhunDegeri::SozlukTipi>(hedef.veri)) {
            const auto& sozluk = std::get<OrhunDegeri::SozlukTipi>(hedef.veri);
            return OrhunDegeri(static_cast<int>(sozluk ? sozluk->size() : 0));
        }

        hataFirlat(satir, "uzunluk yalnızca metin, liste veya sözlük üzerinde kullanılabilir.");
    };

    gomuluIslevler_["listeye_ekle"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 2) {
            hataFirlat(satir, "listeye_ekle(liste, eleman) iki argüman alır.");
        }

        if (!std::holds_alternative<OrhunDegeri::ListeTipi>(args[0].veri)) {
            hataFirlat(satir, "listeye_ekle fonksiyonunun ilk argümanı liste olmalıdır.");
        }

        const auto& mevcutListe = std::get<OrhunDegeri::ListeTipi>(args[0].veri);
        OrhunDegeri::ListeVeri yeniListe = mevcutListe ? *mevcutListe : OrhunDegeri::ListeVeri{};
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

    gomuluIslevler_["dosyaya_yaz"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
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

    // Matematik kütüphanesi.
    gomuluIslevler_["karekok"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 1) {
            hataFirlat(satir, "karekok(x) tek argüman alır.");
        }
        const double x = sayiDegeri(args[0], satir, "karekok");
        if (x < 0.0) {
            hataFirlat(satir, "karekok negatif sayı için tanımsızdır.");
        }
        return OrhunDegeri(std::sqrt(x));
    };

    gomuluIslevler_["us"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 2) {
            hataFirlat(satir, "us(taban, kuvvet) iki argüman alır.");
        }
        const double taban = sayiDegeri(args[0], satir, "us");
        const double kuvvet = sayiDegeri(args[1], satir, "us");
        return OrhunDegeri(std::pow(taban, kuvvet));
    };

    gomuluIslevler_["mutlak"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 1) {
            hataFirlat(satir, "mutlak(x) tek argüman alır.");
        }
        if (std::holds_alternative<int>(args[0].veri)) {
            return OrhunDegeri(std::abs(std::get<int>(args[0].veri)));
        }
        return OrhunDegeri(std::fabs(sayiDegeri(args[0], satir, "mutlak")));
    };

    gomuluIslevler_["sin"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 1) {
            hataFirlat(satir, "sin(x) tek argüman alır.");
        }
        return OrhunDegeri(std::sin(sayiDegeri(args[0], satir, "sin")));
    };

    gomuluIslevler_["cos"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 1) {
            hataFirlat(satir, "cos(x) tek argüman alır.");
        }
        return OrhunDegeri(std::cos(sayiDegeri(args[0], satir, "cos")));
    };

    gomuluIslevler_["tan"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 1) {
            hataFirlat(satir, "tan(x) tek argüman alır.");
        }
        return OrhunDegeri(std::tan(sayiDegeri(args[0], satir, "tan")));
    };

    gomuluIslevler_["yuvarla"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 1) {
            hataFirlat(satir, "yuvarla(x) tek argüman alır.");
        }
        const double deger = std::round(sayiDegeri(args[0], satir, "yuvarla"));
        if (deger > static_cast<double>(std::numeric_limits<int>::max()) ||
            deger < static_cast<double>(std::numeric_limits<int>::min())) {
            hataFirlat(satir, "yuvarla sonucu int aralığını aşıyor.");
        }
        return OrhunDegeri(static_cast<int>(deger));
    };

    // Rastgelelik ve zaman.
    gomuluIslevler_["rastgele"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 2) {
            hataFirlat(satir, "rastgele(min, max) iki argüman alır.");
        }
        if (!tamSayiMi(args[0]) || !tamSayiMi(args[1])) {
            hataFirlat(satir, "rastgele(min, max) için min ve max tam sayı olmalıdır.");
        }

        const long long minDeger = static_cast<long long>(sayiDegeri(args[0], satir, "rastgele"));
        const long long maxDeger = static_cast<long long>(sayiDegeri(args[1], satir, "rastgele"));
        if (minDeger > maxDeger) {
            hataFirlat(satir, "rastgele(min, max) içinde min, max'tan büyük olamaz.");
        }
        if (minDeger < static_cast<long long>(std::numeric_limits<int>::min()) ||
            maxDeger > static_cast<long long>(std::numeric_limits<int>::max())) {
            hataFirlat(satir, "rastgele aralığı int sınırları içinde olmalıdır.");
        }

        static std::mt19937 ureteci(std::random_device{}());
        std::uniform_int_distribution<int> dagilim(static_cast<int>(minDeger), static_cast<int>(maxDeger));
        return OrhunDegeri(dagilim(ureteci));
    };

    gomuluIslevler_["bekle"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 1) {
            hataFirlat(satir, "bekle(saniye) tek argüman alır.");
        }
        const double saniye = sayiDegeri(args[0], satir, "bekle");
        if (saniye < 0.0) {
            hataFirlat(satir, "bekle(saniye) negatif olamaz.");
        }
        std::this_thread::sleep_for(std::chrono::duration<double>(saniye));
        return OrhunDegeri(1);
    };

    gomuluIslevler_["zaman"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (!args.empty()) {
            hataFirlat(satir, "zaman() argüman almaz.");
        }
        const auto suan = std::chrono::system_clock::now();
        const auto epochSaniye = std::chrono::duration_cast<std::chrono::seconds>(suan.time_since_epoch()).count();
        if (epochSaniye > static_cast<long long>(std::numeric_limits<int>::max()) ||
            epochSaniye < static_cast<long long>(std::numeric_limits<int>::min())) {
            return OrhunDegeri(static_cast<double>(epochSaniye));
        }
        return OrhunDegeri(static_cast<int>(epochSaniye));
    };

    // Metin işleme.
    gomuluIslevler_["buyuk_harf"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 1) {
            hataFirlat(satir, "buyuk_harf(metin) tek argüman alır.");
        }
        if (!std::holds_alternative<std::string>(args[0].veri)) {
            hataFirlat(satir, "buyuk_harf(metin) için metin argümanı bekleniyor.");
        }
        std::string metin = std::get<std::string>(args[0].veri);
        std::transform(metin.begin(), metin.end(), metin.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        return OrhunDegeri(std::move(metin));
    };

    gomuluIslevler_["kucuk_harf"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 1) {
            hataFirlat(satir, "kucuk_harf(metin) tek argüman alır.");
        }
        if (!std::holds_alternative<std::string>(args[0].veri)) {
            hataFirlat(satir, "kucuk_harf(metin) için metin argümanı bekleniyor.");
        }
        std::string metin = std::get<std::string>(args[0].veri);
        std::transform(metin.begin(), metin.end(), metin.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return OrhunDegeri(std::move(metin));
    };

    gomuluIslevler_["parcala"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 2) {
            hataFirlat(satir, "parcala(metin, ayirici) iki argüman alır.");
        }
        if (!std::holds_alternative<std::string>(args[0].veri) ||
            !std::holds_alternative<std::string>(args[1].veri)) {
            hataFirlat(satir, "parcala(metin, ayirici) yalnızca metin argümanları kabul eder.");
        }

        const std::string metin = std::get<std::string>(args[0].veri);
        const std::string ayirici = std::get<std::string>(args[1].veri);
        OrhunDegeri::ListeVeri sonuc;

        if (ayirici.empty()) {
            sonuc.reserve(metin.size());
            for (char c : metin) {
                sonuc.emplace_back(std::string(1, c));
            }
            return OrhunDegeri(std::move(sonuc));
        }

        std::size_t baslangic = 0;
        while (true) {
            const std::size_t konum = metin.find(ayirici, baslangic);
            if (konum == std::string::npos) {
                sonuc.emplace_back(metin.substr(baslangic));
                break;
            }
            sonuc.emplace_back(metin.substr(baslangic, konum - baslangic));
            baslangic = konum + ayirici.size();
        }

        return OrhunDegeri(std::move(sonuc));
    };

    gomuluIslevler_["birlestir"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 2) {
            hataFirlat(satir, "birlestir(liste, ayirici) iki argüman alır.");
        }
        if (!std::holds_alternative<OrhunDegeri::ListeTipi>(args[0].veri) ||
            !std::holds_alternative<std::string>(args[1].veri)) {
            hataFirlat(satir, "birlestir(liste, ayirici) için liste ve metin bekleniyor.");
        }

        const auto& listePtr = std::get<OrhunDegeri::ListeTipi>(args[0].veri);
        const std::string ayirici = std::get<std::string>(args[1].veri);
        std::string sonuc;

        const OrhunDegeri::ListeVeri bosListe;
        const auto& liste = listePtr ? *listePtr : bosListe;
        for (std::size_t i = 0; i < liste.size(); ++i) {
            if (i > 0) {
                sonuc += ayirici;
            }
            sonuc += metneCevir(liste[i]);
        }

        return OrhunDegeri(std::move(sonuc));
    };

    gomuluIslevler_["metin_uzunluk"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 1) {
            hataFirlat(satir, "metin_uzunluk(metin) tek argüman alır.");
        }
        if (!std::holds_alternative<std::string>(args[0].veri)) {
            hataFirlat(satir, "metin_uzunluk(metin) için metin bekleniyor.");
        }
        return OrhunDegeri(static_cast<int>(std::get<std::string>(args[0].veri).size()));
    };

    gomuluIslevler_["icerir"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 2) {
            hataFirlat(satir, "icerir(metin, aranan) iki argüman alır.");
        }
        if (!std::holds_alternative<std::string>(args[0].veri) ||
            !std::holds_alternative<std::string>(args[1].veri)) {
            hataFirlat(satir, "icerir(metin, aranan) yalnızca metin argümanları kabul eder.");
        }
        const std::string& metin = std::get<std::string>(args[0].veri);
        const std::string& aranan = std::get<std::string>(args[1].veri);
        return OrhunDegeri(metin.find(aranan) != std::string::npos ? 1 : 0);
    };

    // Tip dönüşümleri.
    gomuluIslevler_["sayiya_cevir"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 1) {
            hataFirlat(satir, "sayiya_cevir(metin) tek argüman alır.");
        }
        if (!std::holds_alternative<std::string>(args[0].veri)) {
            hataFirlat(satir, "sayiya_cevir(metin) için metin bekleniyor.");
        }

        std::string metin = std::get<std::string>(args[0].veri);
        const auto sol = metin.find_first_not_of(" \t\r\n");
        const auto sag = metin.find_last_not_of(" \t\r\n");
        if (sol == std::string::npos) {
            hataFirlat(satir, "sayiya_cevir için boş metin verildi.");
        }
        metin = metin.substr(sol, sag - sol + 1);

        try {
            std::size_t idx = 0;
            const int tam = std::stoi(metin, &idx, 10);
            if (idx == metin.size()) {
                return OrhunDegeri(tam);
            }
        } catch (...) {
            // tam sayı değilse aşağıda ondalık denenecek.
        }

        try {
            std::size_t idx = 0;
            const double ondalik = std::stod(metin, &idx);
            if (idx == metin.size()) {
                return OrhunDegeri(ondalik);
            }
        } catch (...) {
            // aşağıda hata fırlatılacak.
        }

        hataFirlat(satir, "'" + metin + "' sayıya çevrilemedi.");
    };

    gomuluIslevler_["metne_cevir"] = [this](const std::vector<OrhunDegeri>& args, std::size_t satir) -> OrhunDegeri {
        if (args.size() != 1) {
            hataFirlat(satir, "metne_cevir(deger) tek argüman alır.");
        }
        return OrhunDegeri(metneCevir(args[0]));
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

void Interpreter::yerlesikModulleriYukle() {
    // Modül isim uzayı altında aynı gömülü işlevlerin aliaslarını üret.
    gomuluIslevler_["matematik.sin"] = gomuluIslevler_["sin"];
    gomuluIslevler_["matematik.cos"] = gomuluIslevler_["cos"];
    gomuluIslevler_["matematik.tan"] = gomuluIslevler_["tan"];
    gomuluIslevler_["matematik.karekok"] = gomuluIslevler_["karekok"];
    gomuluIslevler_["matematik.us"] = gomuluIslevler_["us"];
    gomuluIslevler_["matematik.rastgele"] = gomuluIslevler_["rastgele"];

    gomuluIslevler_["zaman.simdi"] = gomuluIslevler_["zaman"];
    gomuluIslevler_["zaman.bekle"] = gomuluIslevler_["bekle"];

    // Python benzeri erişim için hazır sözlük modülleri.
    OrhunDegeri::SozlukVeri matematik;
    matematik["pi"] = OrhunDegeri(3.14159265358979323846);
    matematik["sin"] = OrhunDegeri("__islev_ref__:matematik.sin");
    matematik["cos"] = OrhunDegeri("__islev_ref__:matematik.cos");
    matematik["tan"] = OrhunDegeri("__islev_ref__:matematik.tan");
    matematik["karekok"] = OrhunDegeri("__islev_ref__:matematik.karekok");
    matematik["us"] = OrhunDegeri("__islev_ref__:matematik.us");
    matematik["rastgele"] = OrhunDegeri("__islev_ref__:matematik.rastgele");
    globalHafiza_["matematik"] = OrhunDegeri(std::move(matematik));

    OrhunDegeri::SozlukVeri zaman;
    zaman["simdi"] = OrhunDegeri("__islev_ref__:zaman.simdi");
    zaman["bekle"] = OrhunDegeri("__islev_ref__:zaman.bekle");
    globalHafiza_["zaman"] = OrhunDegeri(std::move(zaman));
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

    if (const auto* surece = dynamic_cast<const SureceNode*>(dugum)) {
        calistirSurece(surece);
        return;
    }

    if (const auto* islev = dynamic_cast<const IslevTanimNode*>(dugum)) {
        calistirIslevTanim(islev);
        return;
    }

    if (const auto* sinif = dynamic_cast<const SinifTanimNode*>(dugum)) {
        calistirSinifTanim(sinif);
        return;
    }

    if (const auto* denemeYakala = dynamic_cast<const DenemeYakalaNode*>(dugum)) {
        calistirDenemeYakala(denemeYakala);
        return;
    }

    if (const auto* kir = dynamic_cast<const KirNode*>(dugum)) {
        calistirKir(kir);
        return;
    }

    if (const auto* devam = dynamic_cast<const DevamNode*>(dugum)) {
        calistirDevam(devam);
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
    const OrhunDegeri deger = ifadeHesapla(dugum->ifade());

    if (const auto* kimlik = dynamic_cast<const KimlikNode*>(dugum->hedef())) {
        aktifKapsam()[kimlik->ad()] = deger;
        return;
    }

    OrhunDegeri& hedef = atananHedefYazilabilir(dugum->hedef(), dugum->satir(), true);
    hedef = deger;
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

    ++donguDerinligi_;
    try {
        for (int i = 0; i < kez; ++i) {
            try {
                calistirBlock(dugum->govde());
            } catch (const DevamSinyali&) {
                continue;
            } catch (const KirSinyali&) {
                break;
            }
        }
    } catch (...) {
        --donguDerinligi_;
        throw;
    }
    --donguDerinligi_;
}

void Interpreter::calistirSurece(const SureceNode* dugum) {
    ++donguDerinligi_;
    try {
        while (dogruMu(ifadeHesapla(dugum->kosul()))) {
            try {
                calistirBlock(dugum->govde());
            } catch (const DevamSinyali&) {
                continue;
            } catch (const KirSinyali&) {
                break;
            }
        }
    } catch (...) {
        --donguDerinligi_;
        throw;
    }
    --donguDerinligi_;
}

void Interpreter::calistirIslevTanim(const IslevTanimNode* dugum) {
    islevTablosu_[dugum->ad()] = dugum;
}

void Interpreter::calistirSinifTanim(const SinifTanimNode* dugum) {
    sinifTablosu_[dugum->ad()] = dugum;
}

void Interpreter::calistirDenemeYakala(const DenemeYakalaNode* dugum) {
    try {
        calistirBlock(dugum->denemeBlogu());
    } catch (const OrhunHatasi& hata) {
        DegiskenTablosu yakalaKapsami;
        yakalaKapsami[dugum->hataDegiskeni()] = OrhunDegeri(std::string(hata.what()));
        yerelKapsamYigini_.push_back(std::move(yakalaKapsami));
        try {
            calistirBlock(dugum->yakalaBlogu());
        } catch (...) {
            yerelKapsamYigini_.pop_back();
            throw;
        }
        yerelKapsamYigini_.pop_back();
    } catch (const std::exception& ex) {
        DegiskenTablosu yakalaKapsami;
        yakalaKapsami[dugum->hataDegiskeni()] = OrhunDegeri(std::string(ex.what()));
        yerelKapsamYigini_.push_back(std::move(yakalaKapsami));
        try {
            calistirBlock(dugum->yakalaBlogu());
        } catch (...) {
            yerelKapsamYigini_.pop_back();
            throw;
        }
        yerelKapsamYigini_.pop_back();
    }
}

void Interpreter::calistirKir(const KirNode* dugum) {
    if (donguDerinligi_ <= 0) {
        hataFirlat(dugum->satir(), "'kır' yalnızca döngü içinde kullanılabilir.");
    }
    throw KirSinyali{};
}

void Interpreter::calistirDevam(const DevamNode* dugum) {
    if (donguDerinligi_ <= 0) {
        hataFirlat(dugum->satir(), "'devam' yalnızca döngü içinde kullanılabilir.");
    }
    throw DevamSinyali{};
}

void Interpreter::calistirDondur(const DondurNode* dugum) {
    if (yerelKapsamYigini_.empty()) {
        hataFirlat(dugum->satir(), "'döndür' yalnızca işlev içinde kullanılabilir.");
    }
    throw DondurSinyali(ifadeHesapla(dugum->ifade()));
}

void Interpreter::calistirDahilEt(const DahilEtNode* dugum) {
    static_cast<void>(dahilEtDegerlendir(dugum));
}

OrhunDegeri Interpreter::dahilEtDegerlendir(const DahilEtNode* dugum) {
    std::ifstream dosya(dugum->dosyaAdi(), std::ios::binary);
    if (!dosya.is_open()) {
        hataFirlat(dugum->satir(), "dahil_et: '" + dugum->dosyaAdi() + "' dosyası açılamadı.");
    }

    std::ostringstream tampon;
    tampon << dosya.rdbuf();

    try {
        // Modül içeriğini parse et.
        Lexer lexer(tampon.str());
        std::vector<Token> tokenlar = lexer.tokenize();
        Parser parser(std::move(tokenlar));
        std::unique_ptr<ProgramNode> program = parser.parse();

        // Modül AST'sini yaşam döngüsü boyunca elde tut.
        ProgramNode* programHam = program.get();
        yukluModuller_.push_back(std::move(program));

        // Modül kapsamını mevcut global ortamdan izole etmek için anlık görüntü al.
        const DegiskenTablosu globalOncesi = globalHafiza_;
        const auto islevOncesi = islevTablosu_;

        calistir(programHam);

        // Modülde üretilen değerleri sözlükte topla.
        OrhunDegeri::SozlukVeri modulSozlugu;
        for (const auto& [ad, deger] : globalHafiza_) {
            const auto onceki = globalOncesi.find(ad);
            if (onceki == globalOncesi.end() || !(onceki->second == deger)) {
                modulSozlugu[ad] = deger;
            }
        }

        // Modül işlevlerini isim uzayına (module.func) bağlamak için takma ad üret.
        static std::size_t modulSayaci = 0;
        const std::string modulOnEki =
            "__modul__" + std::to_string(++modulSayaci) + "__";

        std::vector<std::pair<std::string, const IslevTanimNode*>> aliaslar;
        for (const auto& [islevAdi, islevDugumu] : islevTablosu_) {
            const auto onceki = islevOncesi.find(islevAdi);
            if (onceki != islevOncesi.end() && onceki->second == islevDugumu) {
                continue;
            }
            const std::string alias = modulOnEki + islevAdi;
            aliaslar.push_back({alias, islevDugumu});
            modulSozlugu[islevAdi] = OrhunDegeri("__islev_ref__:" + alias);
        }

        // Ortamı geri al ve yalnızca modül alias işlevleri sistemde bırak.
        globalHafiza_ = globalOncesi;
        islevTablosu_ = islevOncesi;
        for (const auto& [alias, islevDugumu] : aliaslar) {
            islevTablosu_[alias] = islevDugumu;
        }

        return OrhunDegeri(std::move(modulSozlugu));
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

    if (const auto* uretec = dynamic_cast<const ListeUretecNode*>(dugum)) {
        return listeUreteciOlustur(uretec);
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

    if (const auto* benim = dynamic_cast<const BenimErisimNode*>(dugum)) {
        return benimErisim(benim);
    }

    if (const auto* yeniNesne = dynamic_cast<const YeniNesneNode*>(dugum)) {
        return yeniNesneOlustur(yeniNesne);
    }

    if (const auto* cagri = dynamic_cast<const IslevCagriNode*>(dugum)) {
        return islevCagir(cagri);
    }

    if (const auto* dahilEt = dynamic_cast<const DahilEtNode*>(dugum)) {
        return dahilEtDegerlendir(dahilEt);
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
        const auto& aPtr = std::get<OrhunDegeri::ListeTipi>(sol.veri);
        const auto& bPtr = std::get<OrhunDegeri::ListeTipi>(sag.veri);
        if (!aPtr || !bPtr) {
            hataFirlat(satir, "Liste işlemi için geçersiz boş liste referansı.");
        }
        const auto& a = *aPtr;
        const auto& b = *bPtr;

        if (a.size() != b.size()) {
            hataFirlat(satir, "Matris boyutları eşleşmiyor");
        }

        OrhunDegeri::ListeVeri sonuc;
        sonuc.reserve(a.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            sonuc.push_back(listeIslemi(a[i], b[i], op, satir));
        }
        return OrhunDegeri(std::move(sonuc));
    }

    if (solListe && sagSayi) {
        const auto& aPtr = std::get<OrhunDegeri::ListeTipi>(sol.veri);
        if (!aPtr) {
            hataFirlat(satir, "Liste işlemi için geçersiz boş liste referansı.");
        }
        const auto& a = *aPtr;
        OrhunDegeri::ListeVeri sonuc;
        sonuc.reserve(a.size());
        for (const auto& oge : a) {
            sonuc.push_back(listeIslemi(oge, sag, op, satir));
        }
        return OrhunDegeri(std::move(sonuc));
    }

    if (solSayi && sagListe) {
        const auto& bPtr = std::get<OrhunDegeri::ListeTipi>(sag.veri);
        if (!bPtr) {
            hataFirlat(satir, "Liste işlemi için geçersiz boş liste referansı.");
        }
        const auto& b = *bPtr;
        OrhunDegeri::ListeVeri sonuc;
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
    OrhunDegeri::ListeVeri liste;
    liste.reserve(dugum->ogeler().size());

    for (const auto& oge : dugum->ogeler()) {
        liste.push_back(ifadeHesapla(oge.get()));
    }

    return OrhunDegeri(std::move(liste));
}

OrhunDegeri Interpreter::listeUreteciOlustur(const ListeUretecNode* dugum) {
    const OrhunDegeri kaynak = ifadeHesapla(dugum->kaynakListe());
    if (!std::holds_alternative<OrhunDegeri::ListeTipi>(kaynak.veri)) {
        hataFirlat(dugum->satir(),
                   "Liste üreteci için 'içinde' kaynağı liste olmalıdır.");
    }

    const auto& kaynakListe = std::get<OrhunDegeri::ListeTipi>(kaynak.veri);
    if (!kaynakListe) {
        return OrhunDegeri(OrhunDegeri::ListeVeri{});
    }

    OrhunDegeri::ListeVeri sonuc;
    sonuc.reserve(kaynakListe->size());

    for (const auto& oge : *kaynakListe) {
        DegiskenTablosu uretecKapsami;
        uretecKapsami[dugum->donguDegiskeni()] = oge;
        yerelKapsamYigini_.push_back(std::move(uretecKapsami));
        try {
            bool ekle = true;
            if (dugum->kosul() != nullptr) {
                ekle = dogruMu(ifadeHesapla(dugum->kosul()));
            }
            if (ekle) {
                sonuc.push_back(ifadeHesapla(dugum->ifade()));
            }
        } catch (...) {
            yerelKapsamYigini_.pop_back();
            throw;
        }
        yerelKapsamYigini_.pop_back();
    }

    return OrhunDegeri(std::move(sonuc));
}

OrhunDegeri Interpreter::sozlukOlustur(const SozlukNode* dugum) {
    OrhunDegeri::SozlukVeri sozluk;
    for (const auto& oge : dugum->ogeler()) {
        sozluk[oge.first] = ifadeHesapla(oge.second.get());
    }
    return OrhunDegeri(std::move(sozluk));
}

OrhunDegeri Interpreter::indeksErisim(const IndeksErisimNode* dugum) {
    const OrhunDegeri hedef = ifadeHesapla(dugum->hedef());
    const OrhunDegeri indeks = ifadeHesapla(dugum->indeks());

    if (std::holds_alternative<OrhunDegeri::ListeTipi>(hedef.veri)) {
        const auto& listePtr = std::get<OrhunDegeri::ListeTipi>(hedef.veri);
        if (!listePtr) {
            hataFirlat(dugum->satir(), "Liste erişiminde geçersiz boş liste referansı.");
        }
        const auto& liste = *listePtr;
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
        const auto& sozlukPtr = std::get<OrhunDegeri::SozlukTipi>(hedef.veri);
        if (!sozlukPtr) {
            hataFirlat(dugum->satir(), "Sözlük erişiminde geçersiz boş sözlük referansı.");
        }
        const auto& sozluk = *sozlukPtr;
        const auto bulunan = sozluk.find(anahtar);
        if (bulunan == sozluk.end()) {
            hataFirlat(dugum->satir(), "'" + anahtar + "' anahtarı sözlükte bulunamadı!");
        }
        return bulunan->second;
    }

    if (std::holds_alternative<OrhunDegeri::NesneTipi>(hedef.veri)) {
        if (!std::holds_alternative<std::string>(indeks.veri)) {
            hataFirlat(dugum->satir(), "Nesne alan erişiminde anahtar metin olmalıdır.");
        }
        const auto& nesne = std::get<OrhunDegeri::NesneTipi>(hedef.veri);
        if (!nesne || !nesne->alanlar) {
            hataFirlat(dugum->satir(), "Nesne erişiminde geçersiz nesne.");
        }
        const std::string& anahtar = std::get<std::string>(indeks.veri);
        const auto bulunan = nesne->alanlar->find(anahtar);
        if (bulunan == nesne->alanlar->end()) {
            hataFirlat(dugum->satir(), "'" + anahtar + "' alanı nesnede bulunamadı.");
        }
        return bulunan->second;
    }

    hataFirlat(dugum->satir(),
               "İndeks erişimi yalnızca liste, sözlük veya nesne üzerinde kullanılabilir.");
}

OrhunDegeri Interpreter::alanErisim(const AlanErisimNode* dugum) {
    const OrhunDegeri hedef = ifadeHesapla(dugum->hedef());

    if (std::holds_alternative<OrhunDegeri::SozlukTipi>(hedef.veri)) {
        const auto& sozlukPtr = std::get<OrhunDegeri::SozlukTipi>(hedef.veri);
        if (!sozlukPtr) {
            hataFirlat(dugum->satir(), "Nokta erişiminde geçersiz boş sözlük referansı.");
        }
        const auto bulunan = sozlukPtr->find(dugum->alanAdi());
        if (bulunan == sozlukPtr->end()) {
            hataFirlat(dugum->satir(), "'" + dugum->alanAdi() + "' anahtarı sözlükte bulunamadı!");
        }
        return bulunan->second;
    }

    if (std::holds_alternative<OrhunDegeri::NesneTipi>(hedef.veri)) {
        const auto& nesne = std::get<OrhunDegeri::NesneTipi>(hedef.veri);
        if (!nesne || !nesne->alanlar) {
            hataFirlat(dugum->satir(), "Nokta erişiminde geçersiz nesne.");
        }
        const auto alanBul = nesne->alanlar->find(dugum->alanAdi());
        if (alanBul != nesne->alanlar->end()) {
            return alanBul->second;
        }

        if (nesne->metodlar.find(dugum->alanAdi()) != nesne->metodlar.end()) {
            return OrhunDegeri("__islev_ref__:" + nesne->sinifAdi + "." + dugum->alanAdi());
        }

        hataFirlat(dugum->satir(), "'" + dugum->alanAdi() + "' alanı nesnede bulunamadı!");
    }

    hataFirlat(dugum->satir(), "Nokta erişimi yalnızca sözlük veya nesne üzerinde kullanılabilir.");
}

OrhunDegeri Interpreter::benimErisim(const BenimErisimNode* dugum) {
    const OrhunDegeri& benimDegeri = degiskenBul("benim", dugum->satir());
    if (!std::holds_alternative<OrhunDegeri::NesneTipi>(benimDegeri.veri)) {
        hataFirlat(dugum->satir(), "'benim' yalnızca nesne metodu içinde kullanılabilir.");
    }

    const auto& nesne = std::get<OrhunDegeri::NesneTipi>(benimDegeri.veri);
    if (!nesne || !nesne->alanlar) {
        hataFirlat(dugum->satir(), "'benim' geçersiz nesneyi gösteriyor.");
    }

    const auto alan = nesne->alanlar->find(dugum->alanAdi());
    if (alan == nesne->alanlar->end()) {
        hataFirlat(dugum->satir(),
                   "'" + dugum->alanAdi() + "' alanı nesnede bulunamadı.");
    }
    return alan->second;
}

OrhunDegeri Interpreter::yeniNesneOlustur(const YeniNesneNode* dugum) {
    const auto sinifBul = sinifTablosu_.find(dugum->sinifAdi());
    if (sinifBul == sinifTablosu_.end()) {
        hataFirlat(dugum->satir(),
                   "'" + dugum->sinifAdi() + "' adlı sınıf bulunamadı.");
    }

    const SinifTanimNode* sinif = sinifBul->second;

    auto nesne = std::make_shared<OrhunNesne>();
    nesne->sinifAdi = sinif->ad();
    OrhunDegeri nesneDegeri(nesne);

    // Miras dahil, alan ve metodları ebeveynden çocuğa doğru yükle.
    std::unordered_set<std::string> ziyaretEdilenler;
    const auto sinifUyeleriniYukle =
        [&](const auto& self, const SinifTanimNode* aktifSinif) -> void {
        if (aktifSinif == nullptr) {
            return;
        }

        if (!ziyaretEdilenler.insert(aktifSinif->ad()).second) {
            hataFirlat(dugum->satir(),
                       "Sınıf mirasında döngü tespit edildi: '" + aktifSinif->ad() + "'");
        }

        if (!aktifSinif->ebeveynAdi().empty()) {
            const auto ebeveynBul = sinifTablosu_.find(aktifSinif->ebeveynAdi());
            if (ebeveynBul == sinifTablosu_.end()) {
                hataFirlat(dugum->satir(), "'" + aktifSinif->ebeveynAdi() +
                                           "' adlı ebeveyn sınıf bulunamadı.");
            }
            self(self, ebeveynBul->second);
        }

        for (const auto& komut : aktifSinif->govde()->komutlar()) {
            if (const auto* atama = dynamic_cast<const AtamaNode*>(komut.get())) {
                const auto* kimlik = dynamic_cast<const KimlikNode*>(atama->hedef());
                if (kimlik == nullptr) {
                    continue;
                }
                // Çocuğun alanı aynı ada sahipse ebeveyni override eder.
                nesne->alanlar->insert_or_assign(kimlik->ad(),
                                                 ifadeHesapla(atama->ifade()));
                continue;
            }

            if (const auto* metod = dynamic_cast<const IslevTanimNode*>(komut.get())) {
                // Çocuğun metodu aynı ada sahipse ebeveyni override eder.
                nesne->metodlar[metod->ad()] =
                    NesneMetodBilgisi{metod, aktifSinif->ad()};
                continue;
            }
        }
    };
    sinifUyeleriniYukle(sinifUyeleriniYukle, sinif);

    // Kurucu varsa otomatik çağır.
    const auto kur = nesne->metodlar.find("kur");
    if (kur != nesne->metodlar.end()) {
        std::vector<OrhunDegeri> argumanlarDeger;
        argumanlarDeger.reserve(dugum->argumanlar().size());
        for (const auto& arg : dugum->argumanlar()) {
            argumanlarDeger.push_back(ifadeHesapla(arg.get()));
        }

        static_cast<void>(kullaniciIslevCalistir(
            kur->second.dugum, argumanlarDeger, dugum->satir(), &nesneDegeri,
            &kur->second.tanimlayanSinif, false));
    } else if (!dugum->argumanlar().empty()) {
        hataFirlat(dugum->satir(),
                   "'" + dugum->sinifAdi() +
                       "' sınıfında 'kur' metodu yok; kurucu argümanı verilemez.");
    }

    return nesneDegeri;
}

OrhunDegeri Interpreter::islevCagir(const IslevCagriNode* dugum) {
    std::vector<OrhunDegeri> argumanDegerleri;
    argumanDegerleri.reserve(dugum->argumanlar().size());
    for (const auto& arg : dugum->argumanlar()) {
        argumanDegerleri.push_back(ifadeHesapla(arg.get()));
    }

    const std::string& cagriAdi = dugum->ad();
    if (cagriAdi.rfind("ust.", 0) == 0) {
        return ustIslevCagir(cagriAdi.substr(4), argumanDegerleri,
                             dugum->satir());
    }

    if (islevTablosu_.find(cagriAdi) != islevTablosu_.end() ||
        gomuluIslevler_.find(cagriAdi) != gomuluIslevler_.end()) {
        return islevCagirAdaGore(cagriAdi, argumanDegerleri, dugum->satir());
    }

    const std::size_t nokta = cagriAdi.rfind('.');
    if (nokta == std::string::npos || nokta == 0 || nokta + 1 >= cagriAdi.size()) {
        hataFirlat(dugum->satir(), "'" + cagriAdi + "' adlı işlev bulunamadı.");
    }

    const std::string hedefYolu = cagriAdi.substr(0, nokta);
    const std::string metodAdi = cagriAdi.substr(nokta + 1);
    const OrhunDegeri hedef = noktaYoluDegeri(hedefYolu, dugum->satir());
    return nesneMetoduCagir(hedef, metodAdi, argumanDegerleri, dugum->satir());
}

OrhunDegeri Interpreter::ustIslevCagir(
    const std::string& metodAdi, const std::vector<OrhunDegeri>& argumanlar,
    std::size_t satir) {
    const OrhunDegeri& benimDegeri = degiskenBul("benim", satir);
    if (!std::holds_alternative<OrhunDegeri::NesneTipi>(benimDegeri.veri)) {
        hataFirlat(satir, "'ust' yalnızca nesne metodu içinde kullanılabilir.");
    }

    const OrhunDegeri& sinifDegeri = degiskenBul("__sinif__", satir);
    if (!std::holds_alternative<std::string>(sinifDegeri.veri)) {
        hataFirlat(satir, "'ust' bağlamı çözümlenemedi (sınıf bilgisi eksik).");
    }
    const std::string aktifSinif = std::get<std::string>(sinifDegeri.veri);

    const auto sinifBul = sinifTablosu_.find(aktifSinif);
    if (sinifBul == sinifTablosu_.end()) {
        hataFirlat(satir, "'" + aktifSinif + "' sınıfı kayıtlı değil.");
    }

    std::string ebeveynAdi = sinifBul->second->ebeveynAdi();
    if (ebeveynAdi.empty()) {
        hataFirlat(satir, "'" + aktifSinif + "' sınıfının üst sınıfı yok.");
    }

    while (!ebeveynAdi.empty()) {
        const auto ebeveynBul = sinifTablosu_.find(ebeveynAdi);
        if (ebeveynBul == sinifTablosu_.end()) {
            hataFirlat(satir, "'" + ebeveynAdi + "' adlı üst sınıf bulunamadı.");
        }

        const SinifTanimNode* ebeveyn = ebeveynBul->second;
        const IslevTanimNode* bulunanMetod = nullptr;
        for (const auto& komut : ebeveyn->govde()->komutlar()) {
            const auto* metod = dynamic_cast<const IslevTanimNode*>(komut.get());
            if (metod != nullptr && metod->ad() == metodAdi) {
                bulunanMetod = metod;
                break;
            }
        }

        if (bulunanMetod != nullptr) {
            return kullaniciIslevCalistir(bulunanMetod, argumanlar, satir,
                                          &benimDegeri, &ebeveynAdi, false);
        }

        ebeveynAdi = ebeveyn->ebeveynAdi();
    }

    hataFirlat(satir, "'" + metodAdi + "' metodu üst sınıflarda bulunamadı.");
}

OrhunDegeri Interpreter::islevCagirAdaGore(
    const std::string& ad, const std::vector<OrhunDegeri>& argumanlar,
    std::size_t satir) {
    const auto yerelIslev = islevTablosu_.find(ad);
    if (yerelIslev != islevTablosu_.end()) {
        return kullaniciIslevCalistir(yerelIslev->second, argumanlar, satir,
                                      nullptr, nullptr, true);
    }

    const auto gomulu = gomuluIslevler_.find(ad);
    if (gomulu != gomuluIslevler_.end()) {
        return gomulu->second(argumanlar, satir);
    }

    hataFirlat(satir, "'" + ad + "' adlı işlev bulunamadı.");
}

OrhunDegeri Interpreter::kullaniciIslevCalistir(
    const IslevTanimNode* islev, const std::vector<OrhunDegeri>& argumanlar,
    std::size_t satir, const OrhunDegeri* benimDegeri,
    const std::string* etkinSinifAdi, bool dondurZorunlu) {
    if (argumanlar.size() != islev->parametreler().size()) {
        hataFirlat(satir, "'" + islev->ad() + "' için argüman sayısı uyuşmuyor.");
    }

    DegiskenTablosu yeniKapsam;
    if (benimDegeri != nullptr) {
        yeniKapsam["benim"] = *benimDegeri;
    }
    if (etkinSinifAdi != nullptr) {
        yeniKapsam["__sinif__"] = OrhunDegeri(*etkinSinifAdi);
    }
    for (std::size_t i = 0; i < argumanlar.size(); ++i) {
        yeniKapsam[islev->parametreler()[i]] = argumanlar[i];
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
    if (dondurZorunlu) {
        hataFirlat(satir, "'" + islev->ad() + "' işlevi bir değer döndürmedi.");
    }
    return OrhunDegeri(0);
}

OrhunDegeri Interpreter::noktaYoluDegeri(const std::string& yol,
                                         std::size_t satir) const {
    const std::vector<std::string> parcalar = noktaIleBol(yol);
    if (parcalar.empty() || parcalar.front().empty()) {
        throw OrhunHatasi("Satır " + std::to_string(satir) +
                          ": Geçersiz nokta erişim yolu: '" + yol + "'");
    }

    const OrhunDegeri* aktif = &degiskenBul(parcalar.front(), satir);
    for (std::size_t i = 1; i < parcalar.size(); ++i) {
        if (std::holds_alternative<OrhunDegeri::SozlukTipi>(aktif->veri)) {
            const auto& sozlukPtr = std::get<OrhunDegeri::SozlukTipi>(aktif->veri);
            if (!sozlukPtr) {
                throw OrhunHatasi("Satır " + std::to_string(satir) +
                                  ": Boş sözlük üzerinden nokta erişimi yapılamaz.");
            }

            const auto bulunan = sozlukPtr->find(parcalar[i]);
            if (bulunan == sozlukPtr->end()) {
                throw OrhunHatasi("Satır " + std::to_string(satir) + ": '" +
                                  parcalar[i] + "' anahtarı bulunamadı.");
            }
            aktif = &bulunan->second;
            continue;
        }

        if (std::holds_alternative<OrhunDegeri::NesneTipi>(aktif->veri)) {
            const auto& nesne = std::get<OrhunDegeri::NesneTipi>(aktif->veri);
            if (!nesne || !nesne->alanlar) {
                throw OrhunHatasi("Satır " + std::to_string(satir) +
                                  ": Geçersiz nesne üzerinden nokta erişimi yapılamaz.");
            }
            const auto alan = nesne->alanlar->find(parcalar[i]);
            if (alan == nesne->alanlar->end()) {
                throw OrhunHatasi("Satır " + std::to_string(satir) + ": '" +
                                  parcalar[i] + "' alanı bulunamadı.");
            }
            aktif = &alan->second;
            continue;
        }

        throw OrhunHatasi("Satır " + std::to_string(satir) +
                          ": '" + parcalar[i - 1] +
                          "' üzerinde nokta erişimi yapılamaz.");
    }

    return *aktif;
}

bool Interpreter::islevReferansiCoz(const OrhunDegeri& deger,
                                    std::string& gercekAd) const {
    if (!std::holds_alternative<std::string>(deger.veri)) {
        return false;
    }
    const std::string& metin = std::get<std::string>(deger.veri);
    const std::string onEk = "__islev_ref__:";
    if (metin.rfind(onEk, 0) != 0) {
        return false;
    }
    gercekAd = metin.substr(onEk.size());
    return !gercekAd.empty();
}

OrhunDegeri Interpreter::nesneMetoduCagir(
    const OrhunDegeri& hedef, const std::string& metodAdi,
    const std::vector<OrhunDegeri>& argumanlar, std::size_t satir) {
    // Liste metodları: ekle, sil, uzunluk
    if (std::holds_alternative<OrhunDegeri::ListeTipi>(hedef.veri)) {
        const auto& listePtr = std::get<OrhunDegeri::ListeTipi>(hedef.veri);
        if (!listePtr) {
            hataFirlat(satir, "Liste metodu boş liste üzerinde çağrılamaz.");
        }

        if (metodAdi == "ekle") {
            if (argumanlar.size() != 1) {
                hataFirlat(satir, "liste.ekle(eleman) bir argüman alır.");
            }
            listePtr->push_back(argumanlar[0]);
            return hedef;
        }

        if (metodAdi == "sil") {
            if (argumanlar.size() != 1) {
                hataFirlat(satir, "liste.sil(indeks) bir argüman alır.");
            }
            const std::size_t idx = listeIndeksiCevir(argumanlar[0], satir, "liste.sil indeksi");
            if (idx >= listePtr->size()) {
                hataFirlat(satir, "liste.sil indeksi sınır dışında.");
            }
            listePtr->erase(listePtr->begin() + static_cast<std::ptrdiff_t>(idx));
            return hedef;
        }

        if (metodAdi == "uzunluk") {
            if (!argumanlar.empty()) {
                hataFirlat(satir, "liste.uzunluk() argüman almaz.");
            }
            return OrhunDegeri(static_cast<int>(listePtr->size()));
        }

        hataFirlat(satir, "Liste için bilinmeyen metod: '" + metodAdi + "'");
    }

    // Sözlük metodları: anahtarlar, degerler, sil (+ çağrılabilir alanlar)
    if (std::holds_alternative<OrhunDegeri::SozlukTipi>(hedef.veri)) {
        const auto& sozlukPtr = std::get<OrhunDegeri::SozlukTipi>(hedef.veri);
        if (!sozlukPtr) {
            hataFirlat(satir, "Sözlük metodu boş sözlük üzerinde çağrılamaz.");
        }

        const auto uye = sozlukPtr->find(metodAdi);
        if (uye != sozlukPtr->end()) {
            std::string gercekAd;
            if (islevReferansiCoz(uye->second, gercekAd)) {
                // Modül işlevleri modül içi sabitlere erişebilsin diye sözlüğü scope'a koy.
                DegiskenTablosu modulKapsami;
                for (const auto& [anahtar, deger] : *sozlukPtr) {
                    std::string dummy;
                    if (!islevReferansiCoz(deger, dummy)) {
                        modulKapsami[anahtar] = deger;
                    }
                }

                yerelKapsamYigini_.push_back(std::move(modulKapsami));
                try {
                    OrhunDegeri sonuc = islevCagirAdaGore(gercekAd, argumanlar, satir);
                    yerelKapsamYigini_.pop_back();
                    return sonuc;
                } catch (...) {
                    yerelKapsamYigini_.pop_back();
                    throw;
                }
            }
        }

        if (metodAdi == "anahtarlar") {
            if (!argumanlar.empty()) {
                hataFirlat(satir, "sozluk.anahtarlar() argüman almaz.");
            }
            OrhunDegeri::ListeVeri sonuc;
            sonuc.reserve(sozlukPtr->size());
            for (const auto& kv : *sozlukPtr) {
                sonuc.emplace_back(kv.first);
            }
            return OrhunDegeri(std::move(sonuc));
        }

        if (metodAdi == "degerler") {
            if (!argumanlar.empty()) {
                hataFirlat(satir, "sozluk.degerler() argüman almaz.");
            }
            OrhunDegeri::ListeVeri sonuc;
            sonuc.reserve(sozlukPtr->size());
            for (const auto& kv : *sozlukPtr) {
                sonuc.push_back(kv.second);
            }
            return OrhunDegeri(std::move(sonuc));
        }

        if (metodAdi == "sil") {
            if (argumanlar.size() != 1 ||
                !std::holds_alternative<std::string>(argumanlar[0].veri)) {
                hataFirlat(satir, "sozluk.sil(anahtar) için metin anahtar bekleniyor.");
            }
            const std::string& anahtar = std::get<std::string>(argumanlar[0].veri);
            const auto silinen = sozlukPtr->erase(anahtar);
            if (silinen == 0) {
                hataFirlat(satir, "'" + anahtar + "' anahtarı sözlükte bulunamadı!");
            }
            return hedef;
        }

        if (metodAdi == "uzunluk") {
            if (!argumanlar.empty()) {
                hataFirlat(satir, "sozluk.uzunluk() argüman almaz.");
            }
            return OrhunDegeri(static_cast<int>(sozlukPtr->size()));
        }

        hataFirlat(satir, "Sözlük için bilinmeyen metod: '" + metodAdi + "'");
    }

    // Metin metodları: buyuk, kucuk, parcala, uzunluk
    if (std::holds_alternative<OrhunDegeri::NesneTipi>(hedef.veri)) {
        const auto& nesne = std::get<OrhunDegeri::NesneTipi>(hedef.veri);
        if (!nesne) {
            hataFirlat(satir, "Geçersiz nesne üzerinde metod çağrısı yapılamaz.");
        }

        const auto metod = nesne->metodlar.find(metodAdi);
        if (metod == nesne->metodlar.end()) {
            hataFirlat(satir, "'" + nesne->sinifAdi +
                                  "' nesnesinde '" + metodAdi +
                                  "' adlı metod bulunamadı.");
        }

        return kullaniciIslevCalistir(metod->second.dugum, argumanlar, satir,
                                      &hedef,
                                      &metod->second.tanimlayanSinif, false);
    }

    // Metin metodları: buyuk, kucuk, parcala, uzunluk
    if (std::holds_alternative<std::string>(hedef.veri)) {
        const std::string metin = std::get<std::string>(hedef.veri);

        if (metodAdi == "buyuk") {
            if (!argumanlar.empty()) {
                hataFirlat(satir, "metin.buyuk() argüman almaz.");
            }
            std::string sonuc = metin;
            std::transform(sonuc.begin(), sonuc.end(), sonuc.begin(), [](unsigned char c) {
                return static_cast<char>(std::toupper(c));
            });
            return OrhunDegeri(std::move(sonuc));
        }

        if (metodAdi == "kucuk") {
            if (!argumanlar.empty()) {
                hataFirlat(satir, "metin.kucuk() argüman almaz.");
            }
            std::string sonuc = metin;
            std::transform(sonuc.begin(), sonuc.end(), sonuc.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return OrhunDegeri(std::move(sonuc));
        }

        if (metodAdi == "parcala") {
            if (argumanlar.size() != 1 ||
                !std::holds_alternative<std::string>(argumanlar[0].veri)) {
                hataFirlat(satir, "metin.parcala(ayirici) için metin ayırıcı bekleniyor.");
            }
            const std::string ayirici = std::get<std::string>(argumanlar[0].veri);
            OrhunDegeri::ListeVeri sonuc;

            if (ayirici.empty()) {
                sonuc.reserve(metin.size());
                for (char c : metin) {
                    sonuc.emplace_back(std::string(1, c));
                }
                return OrhunDegeri(std::move(sonuc));
            }

            std::size_t bas = 0;
            while (true) {
                const std::size_t konum = metin.find(ayirici, bas);
                if (konum == std::string::npos) {
                    sonuc.emplace_back(metin.substr(bas));
                    break;
                }
                sonuc.emplace_back(metin.substr(bas, konum - bas));
                bas = konum + ayirici.size();
            }
            return OrhunDegeri(std::move(sonuc));
        }

        if (metodAdi == "uzunluk") {
            if (!argumanlar.empty()) {
                hataFirlat(satir, "metin.uzunluk() argüman almaz.");
            }
            return OrhunDegeri(static_cast<int>(metin.size()));
        }

        hataFirlat(satir, "Metin için bilinmeyen metod: '" + metodAdi + "'");
    }

    hataFirlat(satir, "Bu tipte nesne metodu çağrılamaz: '" + metodAdi + "'");
}

Interpreter::DegiskenTablosu& Interpreter::aktifKapsam() {
    if (!yerelKapsamYigini_.empty()) {
        return yerelKapsamYigini_.back();
    }
    return globalHafiza_;
}

OrhunDegeri& Interpreter::degiskenBulYazilabilir(const std::string& ad,
                                                 std::size_t satir) {
    for (auto it = yerelKapsamYigini_.rbegin(); it != yerelKapsamYigini_.rend();
         ++it) {
        const auto bulunan = it->find(ad);
        if (bulunan != it->end()) {
            return bulunan->second;
        }
    }

    const auto global = globalHafiza_.find(ad);
    if (global != globalHafiza_.end()) {
        return global->second;
    }

    hataFirlat(satir, "'" + ad + "' değişkeni bulunamadı.");
}

OrhunDegeri& Interpreter::atananHedefYazilabilir(const ASTNode* hedef,
                                                 std::size_t satir,
                                                 bool sonHedef) {
    if (const auto* kimlik = dynamic_cast<const KimlikNode*>(hedef)) {
        if (sonHedef) {
            return aktifKapsam()[kimlik->ad()];
        }
        return degiskenBulYazilabilir(kimlik->ad(), satir);
    }

    if (const auto* benim = dynamic_cast<const BenimErisimNode*>(hedef)) {
        OrhunDegeri& benimDegeri = degiskenBulYazilabilir("benim", satir);
        if (!std::holds_alternative<OrhunDegeri::NesneTipi>(benimDegeri.veri)) {
            hataFirlat(satir, "'benim' yalnızca nesne metodu içinde kullanılabilir.");
        }
        const auto& nesne = std::get<OrhunDegeri::NesneTipi>(benimDegeri.veri);
        if (!nesne || !nesne->alanlar) {
            hataFirlat(satir, "Geçersiz nesne üzerinde alan ataması yapılamaz.");
        }
        return (*nesne->alanlar)[benim->alanAdi()];
    }

    if (const auto* alan = dynamic_cast<const AlanErisimNode*>(hedef)) {
        OrhunDegeri& taban =
            atananHedefYazilabilir(alan->hedef(), satir, false);

        if (std::holds_alternative<OrhunDegeri::SozlukTipi>(taban.veri)) {
            const auto& sozluk = std::get<OrhunDegeri::SozlukTipi>(taban.veri);
            if (!sozluk) {
                hataFirlat(satir, "Boş sözlük üzerinde alan ataması yapılamaz.");
            }
            return (*sozluk)[alan->alanAdi()];
        }

        if (std::holds_alternative<OrhunDegeri::NesneTipi>(taban.veri)) {
            const auto& nesne = std::get<OrhunDegeri::NesneTipi>(taban.veri);
            if (!nesne || !nesne->alanlar) {
                hataFirlat(satir, "Geçersiz nesne üzerinde alan ataması yapılamaz.");
            }
            return (*nesne->alanlar)[alan->alanAdi()];
        }

        hataFirlat(satir, "Nokta ataması yalnızca sözlük veya nesne üzerinde yapılabilir.");
    }

    if (const auto* indeks = dynamic_cast<const IndeksErisimNode*>(hedef)) {
        OrhunDegeri& taban =
            atananHedefYazilabilir(indeks->hedef(), satir, false);
        const OrhunDegeri anahtar = ifadeHesapla(indeks->indeks());

        if (std::holds_alternative<OrhunDegeri::ListeTipi>(taban.veri)) {
            const auto& liste = std::get<OrhunDegeri::ListeTipi>(taban.veri);
            if (!liste) {
                hataFirlat(satir, "Boş liste üzerinde indeks ataması yapılamaz.");
            }
            const std::size_t idx =
                listeIndeksiCevir(anahtar, satir, "liste indeksi");
            if (idx >= liste->size()) {
                hataFirlat(satir, "Liste indeksi sınır dışında.");
            }
            return (*liste)[idx];
        }

        if (std::holds_alternative<OrhunDegeri::SozlukTipi>(taban.veri)) {
            if (!std::holds_alternative<std::string>(anahtar.veri)) {
                hataFirlat(satir, "Sözlük anahtarı metin olmalıdır.");
            }
            const auto& sozluk = std::get<OrhunDegeri::SozlukTipi>(taban.veri);
            if (!sozluk) {
                hataFirlat(satir, "Boş sözlük üzerinde indeks ataması yapılamaz.");
            }
            return (*sozluk)[std::get<std::string>(anahtar.veri)];
        }

        if (std::holds_alternative<OrhunDegeri::NesneTipi>(taban.veri)) {
            if (!std::holds_alternative<std::string>(anahtar.veri)) {
                hataFirlat(satir, "Nesne alan anahtarı metin olmalıdır.");
            }
            const auto& nesne = std::get<OrhunDegeri::NesneTipi>(taban.veri);
            if (!nesne || !nesne->alanlar) {
                hataFirlat(satir, "Geçersiz nesne üzerinde indeks ataması yapılamaz.");
            }
            return (*nesne->alanlar)[std::get<std::string>(anahtar.veri)];
        }

        hataFirlat(satir,
                   "İndeks ataması yalnızca liste, sözlük veya nesne üzerinde yapılabilir.");
    }

    hataFirlat(satir, "Geçersiz atama hedefi.");
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

    throw OrhunHatasi("Satır " + std::to_string(satir) + ": '" + ad + "' değişkeni bulunamadı.");
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
        const auto& liste = std::get<OrhunDegeri::ListeTipi>(deger.veri);
        return liste && !liste->empty();
    }
    if (std::holds_alternative<OrhunDegeri::SozlukTipi>(deger.veri)) {
        const auto& sozluk = std::get<OrhunDegeri::SozlukTipi>(deger.veri);
        return sozluk && !sozluk->empty();
    }
    const auto& nesne = std::get<OrhunDegeri::NesneTipi>(deger.veri);
    return nesne != nullptr;
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
        const auto& listePtr = std::get<OrhunDegeri::ListeTipi>(deger.veri);
        const OrhunDegeri::ListeVeri bosListe;
        const auto& liste = listePtr ? *listePtr : bosListe;
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

    if (std::holds_alternative<OrhunDegeri::SozlukTipi>(deger.veri)) {
        const auto& sozlukPtr = std::get<OrhunDegeri::SozlukTipi>(deger.veri);
        const OrhunDegeri::SozlukVeri bosSozluk;
        const auto& sozluk = sozlukPtr ? *sozlukPtr : bosSozluk;
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

    const auto& nesne = std::get<OrhunDegeri::NesneTipi>(deger.veri);
    if (!nesne) {
        return "<bos_nesne>";
    }
    std::string sonuc = "<" + nesne->sinifAdi + " ";
    if (nesne->alanlar) {
        bool ilk = true;
        for (const auto& [anahtar, alanDegeri] : *nesne->alanlar) {
            if (!ilk) {
                sonuc += ", ";
            }
            ilk = false;
            sonuc += anahtar + "=" + metneCevir(alanDegeri);
        }
    }
    sonuc += ">";
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
        throw OrhunHatasi("Hata: " + baglam + " için sayı bekleniyordu.");
    }
    throw OrhunHatasi("Satır " + std::to_string(satir) + ": " + baglam + " için sayı bekleniyordu.");
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
        throw OrhunHatasi("Hata: " + mesaj);
    }
    throw OrhunHatasi("Satır " + std::to_string(satir) + ": " + mesaj);
}
