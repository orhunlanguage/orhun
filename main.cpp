#include "Interpreter.h"
#include "Lexer.h"
#include "Parser.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::string dosyaOku(const std::string &dosyaYolu) {
    std::ifstream dosya(dosyaYolu, std::ios::binary);
    if (!dosya.is_open()) {
        throw std::runtime_error("Hata: '" + dosyaYolu + "' dosyası açılamadı.");
    }

    std::ostringstream tampon;
    tampon << dosya.rdbuf();
    return tampon.str();
}

void dosyaYaz(const std::string &dosyaYolu, const std::string &icerik) {
    std::ofstream dosya(dosyaYolu, std::ios::binary | std::ios::trunc);
    if (!dosya.is_open()) {
        throw std::runtime_error("Hata: '" + dosyaYolu + "' dosyasına yazılamadı.");
    }
    dosya << icerik;
}

std::string sagaBoslukKirp(std::string metin) {
    while (!metin.empty() && (metin.back() == ' ' || metin.back() == '\t' ||
                              metin.back() == '\r' || metin.back() == '\n')) {
        metin.pop_back();
    }
    return metin;
}

bool satirBlokBaslatir(const std::string &satir) {
    const std::string kirpilmis = sagaBoslukKirp(satir);
    return !kirpilmis.empty() && kirpilmis.back() == ':';
}

bool kodCalistir(const std::string &kaynakKod, Interpreter &yorumlayici,
                 std::string *hataMesaji = nullptr) {
    try {
        Lexer lexer(kaynakKod);
        std::vector<Token> tokenlar = lexer.tokenize();

        Parser parser(std::move(tokenlar));
        std::unique_ptr<ProgramNode> program = parser.parse();
        yorumlayici.calistir(program.get());
        return true;
    } catch (const std::exception &ex) {
        if (hataMesaji != nullptr) {
            *hataMesaji = ex.what();
            return false;
        }
        throw;
    }
}

int replCalistir() {
    // REPL boyunca tek yorumlayıcı kullanıyoruz; değişkenler satırlar arasında
    // yaşamaya devam eder.
    Interpreter yorumlayici;
    std::string tampon;
    bool blokToplaniyor = false;

    std::cout << "Orhun REPL basladi. Cikmak icin 'cikis' yazin.\n";
    std::cout << "Blok komutlarinda (':' ile biten satirlar) calistirmak icin bos satir girin.\n";

    while (true) {
        std::cout << (tampon.empty() ? "orhun> " : "....> ");
        std::string satir;
        if (!std::getline(std::cin, satir)) {
            std::cout << '\n';
            break;
        }

        const std::string kirpilmis = sagaBoslukKirp(satir);
        if (tampon.empty() &&
            (kirpilmis == "cikis" || kirpilmis == "çıkış" ||
             kirpilmis == "exit" || kirpilmis == "quit")) {
            break;
        }

        // Boş satır, blok toplamayı bitirip yürütmek için kullanılır.
        if (satir.empty()) {
            if (tampon.empty()) {
                continue;
            }

            std::string hata;
            if (!kodCalistir(tampon, yorumlayici, &hata)) {
                std::cerr << hata << '\n';
            }
            tampon.clear();
            blokToplaniyor = false;
            continue;
        }

        tampon += satir;
        tampon.push_back('\n');

        if (satirBlokBaslatir(satir)) {
            blokToplaniyor = true;
            continue;
        }

        if (blokToplaniyor) {
            continue;
        }

        std::string hata;
        if (!kodCalistir(tampon, yorumlayici, &hata)) {
            std::cerr << hata << '\n';
        }
        tampon.clear();
    }

    // EOF ile çıkılırken tamponda kod kaldıysa son kez çalıştır.
    if (!tampon.empty()) {
        std::string hata;
        if (!kodCalistir(tampon, yorumlayici, &hata)) {
            std::cerr << hata << '\n';
        }
    }

    return 0;
}

std::string tokeniYaziyaCevir(const Token &token) {
    if (token.tur != TokenType::METIN) {
        return token.deger;
    }

    std::string sonuc = "\"";
    for (const char c : token.deger) {
        switch (c) {
        case '\\':
            sonuc += "\\\\";
            break;
        case '"':
            sonuc += "\\\"";
            break;
        case '\n':
            sonuc += "\\n";
            break;
        case '\t':
            sonuc += "\\t";
            break;
        default:
            sonuc.push_back(c);
            break;
        }
    }
    sonuc += "\"";
    return sonuc;
}

bool noktalamaKapanisMi(const Token &token) {
    return token.tur == TokenType::ISLEM &&
           (token.deger == ")" || token.deger == "]" || token.deger == "}" ||
            token.deger == "," || token.deger == ":" || token.deger == ".");
}

bool acilisNoktalamasiMi(const Token &token) {
    return token.tur == TokenType::ISLEM &&
           (token.deger == "(" || token.deger == "[" || token.deger == "{" ||
            token.deger == ".");
}

bool boslukGerekliMi(const Token &onceki, const Token &simdiki) {
    if (simdiki.tur == TokenType::ISLEM && simdiki.deger == ".") {
        return false;
    }
    if (onceki.tur == TokenType::ISLEM && onceki.deger == ".") {
        return false;
    }
    if (noktalamaKapanisMi(simdiki)) {
        return false;
    }
    if (acilisNoktalamasiMi(onceki)) {
        // ':' sonrasında komut aynı satırdaysa bir boşluk bırakalım.
        return onceki.deger == ":";
    }
    if (simdiki.tur == TokenType::ISLEM && simdiki.deger == "(") {
        if (onceki.tur == TokenType::KIMLIK || onceki.tur == TokenType::ANAHTAR_KELIME) {
            return false;
        }
        if (onceki.tur == TokenType::ISLEM &&
            (onceki.deger == ")" || onceki.deger == "]")) {
            return false;
        }
    }
    if (simdiki.tur == TokenType::ISLEM && simdiki.deger == "[") {
        if (onceki.tur == TokenType::KIMLIK || onceki.tur == TokenType::ANAHTAR_KELIME) {
            return false;
        }
        if (onceki.tur == TokenType::ISLEM &&
            (onceki.deger == ")" || onceki.deger == "]")) {
            return false;
        }
    }
    if (onceki.tur == TokenType::ISLEM && onceki.deger == ",") {
        return true;
    }
    return true;
}

void sondakiBosluklariTemizle(std::string &satir) {
    while (!satir.empty() && (satir.back() == ' ' || satir.back() == '\t')) {
        satir.pop_back();
    }
}

std::string bicimlendir(const std::vector<Token> &tokenlar) {
    std::string sonuc;
    int girintiSeviyesi = 0;
    bool satirBasi = true;
    bool oncekiTokenVar = false;
    Token oncekiToken{};

    for (const Token &token : tokenlar) {
        if (token.tur == TokenType::DOSYA_SONU) {
            break;
        }
        if (token.tur == TokenType::HATA) {
            throw std::runtime_error("Satır " + std::to_string(token.satir) +
                                     ": Biçimlendirme sırasında lexer hatası: " +
                                     token.deger);
        }
        if (token.tur == TokenType::GIRINTI) {
            ++girintiSeviyesi;
            continue;
        }
        if (token.tur == TokenType::CIKINTI) {
            girintiSeviyesi = std::max(0, girintiSeviyesi - 1);
            continue;
        }
        if (token.tur == TokenType::YENI_SATIR) {
            sondakiBosluklariTemizle(sonuc);
            if (sonuc.empty() || sonuc.back() != '\n') {
                sonuc.push_back('\n');
            }
            satirBasi = true;
            oncekiTokenVar = false;
            continue;
        }

        if (satirBasi) {
            sonuc.append(static_cast<std::size_t>(girintiSeviyesi) * 4, ' ');
            satirBasi = false;
        }

        if (oncekiTokenVar && boslukGerekliMi(oncekiToken, token)) {
            sonuc.push_back(' ');
        }

        sonuc += tokeniYaziyaCevir(token);
        oncekiToken = token;
        oncekiTokenVar = true;
    }

    sondakiBosluklariTemizle(sonuc);
    if (!sonuc.empty() && sonuc.back() != '\n') {
        sonuc.push_back('\n');
    }
    return sonuc;
}

int komutFmt(const std::string &dosyaYolu) {
    const std::string kaynakKod = dosyaOku(dosyaYolu);
    Lexer lexer(kaynakKod);
    const std::vector<Token> tokenlar = lexer.tokenize();
    const std::string yeniIcerik = bicimlendir(tokenlar);
    dosyaYaz(dosyaYolu, yeniIcerik);

    std::cout << "Biçimlendirildi: " << dosyaYolu << "\n";
    return 0;
}

int komutPaketYeni(const std::string &projeAdi) {
    namespace fs = std::filesystem;
    if (projeAdi.empty()) {
        throw std::runtime_error("Hata: Proje adı boş olamaz.");
    }

    const fs::path kok = fs::current_path() / projeAdi;
    if (fs::exists(kok)) {
        throw std::runtime_error("Hata: '" + kok.string() + "' zaten mevcut.");
    }

    fs::create_directories(kok / "lib");

    const std::string anaDosya =
        "# " + projeAdi + "\n"
        "yazdır \"Merhaba Orhun!\"\n";
    dosyaYaz((kok / "main.oh").string(), anaDosya);

    const std::string yapilandirma =
        "ad: \"" + projeAdi + "\"\n"
        "surum: \"0.1.0\"\n"
        "ana_dosya: \"main.oh\"\n";
    dosyaYaz((kok / "orhun.yap").string(), yapilandirma);

    std::cout << "Paket iskeleti oluşturuldu: " << kok.string() << "\n";
    return 0;
}

int dosyaCalistir(const std::string &dosyaYolu) {
    if (dosyaYolu.size() < 3 || dosyaYolu.substr(dosyaYolu.size() - 3) != ".oh") {
        throw std::runtime_error("Hata: Orhun kaynak dosyası .oh uzantılı olmalıdır.");
    }

    Interpreter yorumlayici;
    kodCalistir(dosyaOku(dosyaYolu), yorumlayici);
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
#ifdef _WIN32
    // Windows konsolunda Türkçe UTF-8 çıktının bozulmaması için
    // giriş/çıkış kod sayfasını UTF-8'e zorla.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    try {
        if (argc < 2) {
            return replCalistir();
        }

        const std::string komut = argv[1];
        if (komut == "fmt") {
            if (argc < 3) {
                throw std::runtime_error("Hata: fmt komutu için dosya adı bekleniyor.");
            }
            return komutFmt(argv[2]);
        }

        if (komut == "paket") {
            if (argc < 4 || std::string(argv[2]) != "yeni") {
                throw std::runtime_error(
                    "Hata: paket komutu için 'paket yeni <proje_adi>' kullanın.");
            }
            return komutPaketYeni(argv[3]);
        }

        return dosyaCalistir(komut);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
