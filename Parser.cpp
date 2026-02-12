#include "Parser.h"

#include <stdexcept>
#include <utility>

Parser::Parser(std::vector<Token> tokenlar) : tokenlar_(std::move(tokenlar)) {
    if (tokenlar_.empty()) {
        tokenlar_.push_back({TokenType::DOSYA_SONU, "", 1});
    }
}

std::unique_ptr<ProgramNode> Parser::parse() {
    auto program = std::make_unique<ProgramNode>(tokenlar_.front().satir);

    yeniSatirlariAtla();

    while (!dosyaSonu()) {
        if (kontrol(TokenType::HATA)) {
            syntaxError(bak(), bak().deger);
        }
        if (kontrol(TokenType::DOSYA_SONU)) {
            break;
        }
        if (kontrol(TokenType::CIKINTI)) {
            syntaxError(bak(), "Beklenmeyen blok kapanışı (CIKINTI).");
        }
        if (kontrol(TokenType::GIRINTI)) {
            syntaxError(bak(), "Beklenmeyen girinti (GIRINTI).");
        }

        program->komutEkle(parseKomut());
        yeniSatirlariAtla();
    }

    return program;
}

std::unique_ptr<ASTNode> Parser::parseKomut() {
    if (kontrol(TokenType::KIMLIK) && bakIleri(1).tur == TokenType::ANAHTAR_KELIME &&
        bakIleri(1).deger == "olsun") {
        return parseAtama();
    }

    if (kontrol(TokenType::ANAHTAR_KELIME, "yazdır")) {
        return parseYazdir();
    }
    if (kontrol(TokenType::ANAHTAR_KELIME, "eğer")) {
        return parseEger();
    }
    if (kontrol(TokenType::ANAHTAR_KELIME, "tekrarla")) {
        return parseTekrarla();
    }
    if (kontrol(TokenType::ANAHTAR_KELIME, "işlev")) {
        return parseIslevTanim();
    }
    if (kontrol(TokenType::ANAHTAR_KELIME, "döndür")) {
        return parseDondur();
    }
    if (kontrol(TokenType::ANAHTAR_KELIME, "dahil_et")) {
        return parseDahilEt();
    }

    if (ifadeBaslangiciMi(bak())) {
        const Token baslangic = bak();
        return std::make_unique<IfadeKomutNode>(parseIfade(), baslangic.satir);
    }

    syntaxError(bak(), "Komut bekleniyordu.");
}

std::unique_ptr<ASTNode> Parser::parseAtama() {
    const Token adToken = tuket(TokenType::KIMLIK, "Atama için değişken adı bekleniyor.");
    tuket(TokenType::ANAHTAR_KELIME, "olsun", "Değişken adından sonra 'olsun' bekleniyor.");

    if (kontrol(TokenType::YENI_SATIR) || kontrol(TokenType::DOSYA_SONU)) {
        syntaxError(bak(), "'olsun' komutundan sonra bir değer bekleniyor.");
    }

    return std::make_unique<AtamaNode>(adToken.deger, parseIfade(), adToken.satir);
}

std::unique_ptr<ASTNode> Parser::parseYazdir() {
    const Token token = tuket(TokenType::ANAHTAR_KELIME, "yazdır", "'yazdır' komutu bekleniyor.");

    if (kontrol(TokenType::YENI_SATIR) || kontrol(TokenType::DOSYA_SONU)) {
        syntaxError(bak(), "'yazdır' komutundan sonra bir ifade bekleniyor.");
    }

    return std::make_unique<YazdirNode>(parseIfade(), token.satir);
}

std::unique_ptr<ASTNode> Parser::parseEger() {
    const Token token = tuket(TokenType::ANAHTAR_KELIME, "eğer", "'eğer' komutu bekleniyor.");

    if (kontrol(TokenType::YENI_SATIR) || kontrol(TokenType::DOSYA_SONU)) {
        syntaxError(bak(), "'eğer' komutundan sonra koşul ifadesi bekleniyor.");
    }

    std::unique_ptr<ASTNode> kosul = parseIfade();
    tuket(TokenType::ANAHTAR_KELIME, "ise", "Koşuldan sonra 'ise' bekleniyor.");

    std::unique_ptr<BlockNode> dogruBlok = parseBlokVeyaTekKomut("eğer", false);

    std::unique_ptr<BlockNode> yanlisBlok;
    if (eslesir(TokenType::ANAHTAR_KELIME, "değilse")) {
        yanlisBlok = parseBlokVeyaTekKomut("değilse", false);
    }

    return std::make_unique<EgerNode>(std::move(kosul), std::move(dogruBlok), std::move(yanlisBlok), token.satir);
}

std::unique_ptr<ASTNode> Parser::parseTekrarla() {
    const Token token = tuket(TokenType::ANAHTAR_KELIME, "tekrarla", "'tekrarla' komutu bekleniyor.");

    if (kontrol(TokenType::YENI_SATIR) || kontrol(TokenType::DOSYA_SONU)) {
        syntaxError(bak(), "'tekrarla' komutundan sonra tekrar sayısı bekleniyor.");
    }

    std::unique_ptr<ASTNode> kacKez = parseIfade();
    tuket(TokenType::ANAHTAR_KELIME, "kez", "Tekrar sayısından sonra 'kez' bekleniyor.");

    return std::make_unique<TekrarlaNode>(std::move(kacKez), parseBlokVeyaTekKomut("tekrarla", false), token.satir);
}

std::unique_ptr<ASTNode> Parser::parseIslevTanim() {
    const Token token = tuket(TokenType::ANAHTAR_KELIME, "işlev", "'işlev' komutu bekleniyor.");
    const Token adToken = tuket(TokenType::KIMLIK, "'işlev' ifadesinden sonra işlev adı bekleniyor.");

    tuket(TokenType::ISLEM, "(", "İşlev adından sonra '(' bekleniyor.");

    std::vector<std::string> parametreler;
    if (!kontrol(TokenType::ISLEM, ")")) {
        parametreler.push_back(tuket(TokenType::KIMLIK, "Parametre adı bekleniyor.").deger);
        while (eslesir(TokenType::ISLEM, ",")) {
            parametreler.push_back(tuket(TokenType::KIMLIK, "Virgülden sonra parametre adı bekleniyor.").deger);
        }
    }

    tuket(TokenType::ISLEM, ")", "Parametre listesinin sonunda ')' bekleniyor.");
    tuket(TokenType::ISLEM, ":", "İşlev başlığından sonra ':' bekleniyor.");

    return std::make_unique<IslevTanimNode>(adToken.deger,
                                            std::move(parametreler),
                                            parseBlokVeyaTekKomut("işlev", true),
                                            token.satir);
}

std::unique_ptr<ASTNode> Parser::parseDondur() {
    const Token token = tuket(TokenType::ANAHTAR_KELIME, "döndür", "'döndür' komutu bekleniyor.");

    if (kontrol(TokenType::YENI_SATIR) || kontrol(TokenType::DOSYA_SONU)) {
        syntaxError(bak(), "'döndür' komutundan sonra bir ifade bekleniyor.");
    }

    return std::make_unique<DondurNode>(parseIfade(), token.satir);
}

std::unique_ptr<ASTNode> Parser::parseDahilEt() {
    const Token token = tuket(TokenType::ANAHTAR_KELIME, "dahil_et", "'dahil_et' komutu bekleniyor.");
    const Token dosya = tuket(TokenType::METIN, "'dahil_et' komutundan sonra dosya adı bekleniyor.");
    return std::make_unique<DahilEtNode>(dosya.deger, token.satir);
}

std::unique_ptr<BlockNode> Parser::parseBlokVeyaTekKomut(const std::string& baglam, bool girintiZorunlu) {
    auto blok = std::make_unique<BlockNode>(bak().satir);

    if (eslesir(TokenType::YENI_SATIR)) {
        yeniSatirlariAtla();
        if (!eslesir(TokenType::GIRINTI)) {
            syntaxError(bak(), "'" + baglam + "' için girintili blok bekleniyor.");
        }

        yeniSatirlariAtla();

        while (!dosyaSonu() && !kontrol(TokenType::CIKINTI)) {
            if (kontrol(TokenType::HATA)) {
                syntaxError(bak(), bak().deger);
            }
            blok->komutEkle(parseKomut());
            yeniSatirlariAtla();
        }

        tuket(TokenType::CIKINTI, "'" + baglam + "' bloğu kapanmadı.");
        return blok;
    }

    if (girintiZorunlu) {
        syntaxError(bak(), "'" + baglam + "' bloğu ':' sonrasında yeni satır ve girinti gerektirir.");
    }

    if (kontrol(TokenType::DOSYA_SONU) || kontrol(TokenType::YENI_SATIR)) {
        syntaxError(bak(), "'" + baglam + "' komutundan sonra bir komut bekleniyor.");
    }

    blok->komutEkle(parseKomut());
    return blok;
}

std::unique_ptr<ASTNode> Parser::parseIfade() {
    return parseVeya();
}

std::unique_ptr<ASTNode> Parser::parseVeya() {
    std::unique_ptr<ASTNode> sol = parseVe();

    while (kontrol(TokenType::ANAHTAR_KELIME, "veya")) {
        const Token op = ilerle();
        std::unique_ptr<ASTNode> sag = parseVe();
        sol = std::make_unique<IkiliIslemNode>(std::move(sol), op.deger, std::move(sag), op.satir);
    }

    return sol;
}

std::unique_ptr<ASTNode> Parser::parseVe() {
    std::unique_ptr<ASTNode> sol = parseKarsilastirma();

    while (kontrol(TokenType::ANAHTAR_KELIME, "ve")) {
        const Token op = ilerle();
        std::unique_ptr<ASTNode> sag = parseKarsilastirma();
        sol = std::make_unique<IkiliIslemNode>(std::move(sol), op.deger, std::move(sag), op.satir);
    }

    return sol;
}

std::unique_ptr<ASTNode> Parser::parseKarsilastirma() {
    std::unique_ptr<ASTNode> sol = parseToplama();

    while (kontrol(TokenType::ANAHTAR_KELIME, "eşit") || kontrol(TokenType::ANAHTAR_KELIME, "eşit_değil") ||
           kontrol(TokenType::ANAHTAR_KELIME, "büyük") || kontrol(TokenType::ANAHTAR_KELIME, "küçük")) {
        const Token op = ilerle();
        std::unique_ptr<ASTNode> sag = parseToplama();
        sol = std::make_unique<IkiliIslemNode>(std::move(sol), op.deger, std::move(sag), op.satir);
    }

    return sol;
}

std::unique_ptr<ASTNode> Parser::parseToplama() {
    std::unique_ptr<ASTNode> sol = parseCarpma();

    while (kontrol(TokenType::ISLEM, "+") || kontrol(TokenType::ISLEM, "-")) {
        const Token op = ilerle();
        std::unique_ptr<ASTNode> sag = parseCarpma();
        sol = std::make_unique<IkiliIslemNode>(std::move(sol), op.deger, std::move(sag), op.satir);
    }

    return sol;
}

std::unique_ptr<ASTNode> Parser::parseCarpma() {
    std::unique_ptr<ASTNode> sol = parseTekli();

    while (kontrol(TokenType::ISLEM, "*") || kontrol(TokenType::ISLEM, "/")) {
        const Token op = ilerle();
        std::unique_ptr<ASTNode> sag = parseTekli();
        sol = std::make_unique<IkiliIslemNode>(std::move(sol), op.deger, std::move(sag), op.satir);
    }

    return sol;
}

std::unique_ptr<ASTNode> Parser::parseTekli() {
    if (kontrol(TokenType::ANAHTAR_KELIME, "değil")) {
        const Token op = ilerle();
        return std::make_unique<TekliIslemNode>(op.deger, parseTekli(), op.satir);
    }

    if (kontrol(TokenType::ISLEM, "-")) {
        const Token op = ilerle();
        return std::make_unique<TekliIslemNode>(op.deger, parseTekli(), op.satir);
    }

    return parsePostfix();
}

std::unique_ptr<ASTNode> Parser::parsePostfix() {
    std::unique_ptr<ASTNode> ifade = parseBirincil();

    while (true) {
        if (eslesir(TokenType::ISLEM, "(")) {
            const Token acilisParantezi = onceki();
            std::vector<std::unique_ptr<ASTNode>> argumanlar;

            if (!kontrol(TokenType::ISLEM, ")")) {
                argumanlar.push_back(parseIfade());
                while (eslesir(TokenType::ISLEM, ",")) {
                    argumanlar.push_back(parseIfade());
                }
            }

            tuket(TokenType::ISLEM, ")", "Argüman listesinin sonunda ')' bekleniyor.");

            auto* kimlik = dynamic_cast<KimlikNode*>(ifade.get());
            if (kimlik == nullptr) {
                syntaxError(acilisParantezi, "Sadece kimlikler işlev gibi çağrılabilir.");
            }

            ifade = std::make_unique<IslevCagriNode>(kimlik->ad(), std::move(argumanlar), kimlik->satir());
            continue;
        }

        if (eslesir(TokenType::ISLEM, "[")) {
            const Token acilisKose = onceki();
            std::unique_ptr<ASTNode> indeks = parseIfade();
            tuket(TokenType::ISLEM, "]", "İndeks erişiminde ']' bekleniyor.");
            ifade = std::make_unique<IndeksErisimNode>(std::move(ifade), std::move(indeks), acilisKose.satir);
            continue;
        }

        if (eslesir(TokenType::ISLEM, ".")) {
            const Token noktaToken = onceki();
            const Token alanToken = tuket(TokenType::KIMLIK, "Nokta erişiminden sonra alan adı bekleniyor.");
            ifade = std::make_unique<AlanErisimNode>(std::move(ifade), alanToken.deger, noktaToken.satir);
            continue;
        }

        break;
    }

    return ifade;
}

std::unique_ptr<ASTNode> Parser::parseBirincil() {
    if (eslesir(TokenType::SAYI)) {
        return std::make_unique<SayiNode>(onceki().deger, onceki().satir);
    }

    if (eslesir(TokenType::METIN)) {
        return std::make_unique<MetinNode>(onceki().deger, onceki().satir);
    }

    if (eslesir(TokenType::KIMLIK)) {
        return std::make_unique<KimlikNode>(onceki().deger, onceki().satir);
    }

    if (eslesir(TokenType::ANAHTAR_KELIME, "doğru")) {
        return std::make_unique<MantikNode>(true, onceki().satir);
    }

    if (eslesir(TokenType::ANAHTAR_KELIME, "yanlış")) {
        return std::make_unique<MantikNode>(false, onceki().satir);
    }

    if (eslesir(TokenType::ANAHTAR_KELIME, "sor")) {
        const Token token = onceki();
        if (kontrol(TokenType::YENI_SATIR) || kontrol(TokenType::DOSYA_SONU)) {
            syntaxError(bak(), "'sor' komutundan sonra bir soru ifadesi bekleniyor.");
        }
        return std::make_unique<SorNode>(parseIfade(), token.satir);
    }

    if (eslesir(TokenType::ISLEM, "[")) {
        const Token token = onceki();
        std::vector<std::unique_ptr<ASTNode>> ogeler;

        if (!kontrol(TokenType::ISLEM, "]")) {
            ogeler.push_back(parseIfade());
            while (eslesir(TokenType::ISLEM, ",")) {
                ogeler.push_back(parseIfade());
            }
        }

        tuket(TokenType::ISLEM, "]", "Liste ifadesinin sonunda ']' bekleniyor.");
        return std::make_unique<ListeNode>(std::move(ogeler), token.satir);
    }

    if (eslesir(TokenType::ISLEM, "{")) {
        const Token token = onceki();
        std::vector<SozlukNode::OgeTipi> ogeler;

        if (!kontrol(TokenType::ISLEM, "}")) {
            while (true) {
                const std::string anahtar = parseSozlukAnahtari();
                tuket(TokenType::ISLEM, ":", "Sözlükte anahtardan sonra ':' bekleniyor.");
                std::unique_ptr<ASTNode> deger = parseIfade();
                ogeler.push_back({anahtar, std::move(deger)});

                if (!eslesir(TokenType::ISLEM, ",")) {
                    break;
                }
            }
        }

        tuket(TokenType::ISLEM, "}", "Sözlük ifadesinin sonunda '}' bekleniyor.");
        return std::make_unique<SozlukNode>(std::move(ogeler), token.satir);
    }

    if (eslesir(TokenType::ISLEM, "(")) {
        std::unique_ptr<ASTNode> ic = parseIfade();
        tuket(TokenType::ISLEM, ")", "Parantezli ifade kapanırken ')' bekleniyor.");
        return ic;
    }

    syntaxError(bak(), "İfade bekleniyordu (sayı, metin, kimlik, liste, sözlük veya parantez).");
}

std::string Parser::parseSozlukAnahtari() {
    if (eslesir(TokenType::KIMLIK) || eslesir(TokenType::METIN) || eslesir(TokenType::ANAHTAR_KELIME)) {
        return onceki().deger;
    }
    syntaxError(bak(), "Sözlük anahtarı bekleniyor (kimlik veya metin).");
}

bool Parser::dosyaSonu() const {
    return konum_ >= tokenlar_.size() || tokenlar_[konum_].tur == TokenType::DOSYA_SONU;
}

const Token& Parser::bak() const {
    if (konum_ >= tokenlar_.size()) {
        return tokenlar_.back();
    }
    return tokenlar_[konum_];
}

const Token& Parser::bakIleri(std::size_t uzaklik) const {
    const std::size_t hedef = konum_ + uzaklik;
    if (hedef >= tokenlar_.size()) {
        return tokenlar_.back();
    }
    return tokenlar_[hedef];
}

const Token& Parser::onceki() const {
    if (konum_ == 0) {
        return tokenlar_.front();
    }
    return tokenlar_[konum_ - 1];
}

const Token& Parser::ilerle() {
    if (!dosyaSonu()) {
        ++konum_;
    }
    return onceki();
}

bool Parser::kontrol(TokenType tur) const {
    if (konum_ >= tokenlar_.size()) {
        return false;
    }
    return bak().tur == tur;
}

bool Parser::kontrol(TokenType tur, const std::string& deger) const {
    return kontrol(tur) && bak().deger == deger;
}

bool Parser::eslesir(TokenType tur) {
    if (!kontrol(tur)) {
        return false;
    }
    ilerle();
    return true;
}

bool Parser::eslesir(TokenType tur, const std::string& deger) {
    if (!kontrol(tur, deger)) {
        return false;
    }
    ilerle();
    return true;
}

const Token& Parser::tuket(TokenType tur, const std::string& hataMesaji) {
    if (kontrol(tur)) {
        return ilerle();
    }
    syntaxError(bak(), hataMesaji);
}

const Token& Parser::tuket(TokenType tur, const std::string& deger, const std::string& hataMesaji) {
    if (kontrol(tur, deger)) {
        return ilerle();
    }
    syntaxError(bak(), hataMesaji);
}

void Parser::yeniSatirlariAtla() {
    while (eslesir(TokenType::YENI_SATIR)) {
        // Bilerek boş.
    }
}

bool Parser::ifadeBaslangiciMi(const Token& token) const {
    if (token.tur == TokenType::SAYI || token.tur == TokenType::METIN || token.tur == TokenType::KIMLIK) {
        return true;
    }

    if (token.tur == TokenType::ISLEM &&
        (token.deger == "(" || token.deger == "[" || token.deger == "{" || token.deger == "-")) {
        return true;
    }

    if (token.tur == TokenType::ANAHTAR_KELIME &&
        (token.deger == "sor" || token.deger == "doğru" || token.deger == "yanlış" || token.deger == "değil")) {
        return true;
    }

    return false;
}

[[noreturn]] void Parser::syntaxError(const Token& token, const std::string& mesaj) const {
    const std::string gorunen = token.deger.empty() ? "<boş>" : token.deger;
    throw std::runtime_error("Satır " + std::to_string(token.satir) + ": " + mesaj + " [token: " + gorunen + "]");
}
