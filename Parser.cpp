#include "Parser.h"
#include "Yardimci.h"

#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace {
// Kimlik ve nokta erişim zincirini "modul.islev" biçimine dönüştürür.
bool cagrilabilirAdCoz(const ASTNode *dugum, std::string &ad) {
  if (const auto *kimlik = dynamic_cast<const KimlikNode *>(dugum)) {
    ad = kimlik->ad();
    return true;
  }

  if (const auto *ust = dynamic_cast<const UstErisimNode *>(dugum)) {
    ad = "ust." + ust->metodAdi();
    return true;
  }

  if (const auto *benim = dynamic_cast<const BenimErisimNode *>(dugum)) {
    ad = "benim." + benim->alanAdi();
    return true;
  }

  if (const auto *alan = dynamic_cast<const AlanErisimNode *>(dugum)) {
    std::string kok;
    if (!cagrilabilirAdCoz(alan->hedef(), kok)) {
      return false;
    }
    ad = kok + "." + alan->alanAdi();
    return true;
  }

  return false;
}

const std::vector<std::string> &orhunAnahtarKelimeleri() {
  static const std::vector<std::string> anahtarlar = {
      "yaz",       "yazdır",    "olsun",    "eğer",   "ise",     "değilse",
      "doğru",     "yanlış",    "tekrarla", "kez",    "sor",     "oku",
      "işlev",     "döndür",
      "dış_işlev", "dis_islev", "dahil_et", "sürece", "eşit",    "eşit_değil",
      "büyük",     "küçük",     "ve",       "veya",   "değil",   "tip",
      "yeni",      "benim",     "deneme",   "yakala", "kır",     "devam",
      "ust",       "için",      "içinde",   "paralel", "yap",    "aralik",
      "aralık",    "ilk",       "son",      "bos_mu", "boş_mu",  "dolu_mu",
      "uzunluk"};
  return anahtarlar;
}

std::vector<std::string>
olasiOneriAdaylariniTopla(const std::vector<OrhunToken> &tokenlar) {
  std::vector<std::string> adaylar = orhunAnahtarKelimeleri();
  std::unordered_set<std::string> gorulen(adaylar.begin(), adaylar.end());

  for (const OrhunToken &token : tokenlar) {
    if ((token.tur == TokenTuru::KIMLIK ||
         token.tur == TokenTuru::ANAHTAR_KELIME) &&
        !token.deger.empty() && gorulen.insert(token.deger).second) {
      adaylar.push_back(token.deger);
    }
  }

  return adaylar;
}

std::optional<std::string>
akilliOneriBul(const OrhunToken &token,
               const std::vector<OrhunToken> &tokenlar) {
  if (token.tur != TokenTuru::KIMLIK || token.deger.empty()) {
    return std::nullopt;
  }

  const auto adaylar = olasiOneriAdaylariniTopla(tokenlar);
  const std::size_t maxMesafe =
      utf8KodNoktalarinaCevir(token.deger).size() >= 7 ? 3 : 2;
  return enYakinOneri(token.deger, adaylar, maxMesafe);
}
} // namespace

Parser::Parser(std::vector<OrhunToken> tokenlar)
    : tokenlar_(std::move(tokenlar)) {
  if (tokenlar_.empty()) {
    tokenlar_.push_back({TokenTuru::DOSYA_SONU, "", 1});
  }
}

std::unique_ptr<ProgramNode> Parser::parse() {
  auto program = std::make_unique<ProgramNode>(tokenlar_.front().satir);

  yeniSatirlariAtla();

  while (!dosyaSonu()) {
    if (kontrol(TokenTuru::HATA)) {
      syntaxError(bak(), bak().deger);
    }
    if (kontrol(TokenTuru::DOSYA_SONU)) {
      break;
    }
    if (kontrol(TokenTuru::CIKINTI)) {
      syntaxError(bak(), "Beklenmeyen blok kapanışı (CIKINTI).");
    }
    if (kontrol(TokenTuru::GIRINTI)) {
      syntaxError(bak(), "Beklenmeyen girinti (GIRINTI).");
    }

    program->komutEkle(parseKomut());
    yeniSatirlariAtla();
  }

  return program;
}

std::unique_ptr<ASTNode> Parser::parseKomut() {
  // Çoklu atama: a, b, c olsun ifade
  if (kontrol(TokenTuru::KIMLIK)) {
    const std::size_t kayit = konum_;
    const std::size_t satir = bak().satir;

    std::vector<std::string> hedefler;
    hedefler.push_back(ilerle().deger);
    bool virgulGoruldu = false;
    while (eslesir(TokenTuru::ISLEM, ",")) {
      virgulGoruldu = true;
      hedefler.push_back(
          tuket(TokenTuru::KIMLIK,
                "Virgülden sonra çoklu atama hedef adı bekleniyor.")
              .deger);
    }

    bool bildirimMi = false;
    bool cokluAtamaMi = false;
    if (virgulGoruldu && eslesir(TokenTuru::ANAHTAR_KELIME, "olsun")) {
      bildirimMi = true;
      cokluAtamaMi = true;
    } else if (virgulGoruldu && eslesir(TokenTuru::ISLEM, "=")) {
      cokluAtamaMi = true;
    }

    if (cokluAtamaMi) {
      return parseCokluAtama(std::move(hedefler), satir, bildirimMi);
    }

    konum_ = kayit;
  }

  // Genel atama: hedef olsun ifade
  // Hedef; kimlik, benim.alan, alan erişimi veya indeks erişimi olabilir.
  if (kontrol(TokenTuru::KIMLIK) ||
      kontrol(TokenTuru::ANAHTAR_KELIME, "benim")) {
    const std::size_t kayit = konum_;
    std::unique_ptr<ASTNode> hedef = parsePostfix();
    bool bildirimMi = false;
    bool atamaMi = false;
    if (eslesir(TokenTuru::ANAHTAR_KELIME, "olsun")) {
      bildirimMi = true;
      atamaMi = true;
    } else if (eslesir(TokenTuru::ISLEM, "=")) {
      atamaMi = true;
    }

    if (atamaMi) {
      if (!atanabilirHedefMi(hedef.get())) {
        syntaxError(bak(), "Atama hedefi yalnızca değişken, alan veya indeks "
                           "erişimi olabilir.");
      }
      const std::size_t satir = hedef->satir();
      return parseAtama(std::move(hedef), satir, bildirimMi);
    }
    konum_ = kayit;
  }

  if (kontrol(TokenTuru::ANAHTAR_KELIME, "yazdır") ||
      kontrol(TokenTuru::KIMLIK, "yaz")) {
    return parseYazdir();
  }

  if (kontrol(TokenTuru::KIMLIK)) {
    const OrhunToken &adayKomut = bak();
    const auto oneri = akilliOneriBul(adayKomut, tokenlar_);
    const OrhunToken &sonraki = bakIleri(1);
    const bool komutGibiBaslangic =
        sonraki.tur == TokenTuru::KIMLIK || sonraki.tur == TokenTuru::METIN ||
        sonraki.tur == TokenTuru::SAYI || sonraki.tur == TokenTuru::ONDALIK ||
        (sonraki.tur == TokenTuru::ANAHTAR_KELIME && sonraki.deger != "olsun");
    if (oneri.has_value() && komutGibiBaslangic) {
      syntaxError(adayKomut, "Tanınmayan komut: '" + adayKomut.deger + "'.");
    }
  }

  if (kontrol(TokenTuru::ANAHTAR_KELIME, "eğer")) {
    return parseEger();
  }
  if (kontrol(TokenTuru::ANAHTAR_KELIME, "tekrarla")) {
    return parseTekrarla();
  }
  if (kontrol(TokenTuru::ANAHTAR_KELIME, "sürece")) {
    const OrhunToken token = tuket(TokenTuru::ANAHTAR_KELIME, "sürece",
                                   "'sürece' komutu bekleniyor.");
    if (kontrol(TokenTuru::YENI_SATIR) || kontrol(TokenTuru::DOSYA_SONU)) {
      syntaxError(bak(), "'sürece' komutundan sonra koşul bekleniyor.");
    }

    std::unique_ptr<ASTNode> kosul = parseIfade();
    tuket(TokenTuru::ISLEM, ":", "'sürece' koşulundan sonra ':' bekleniyor.");
    return std::make_unique<SureceNode>(
        std::move(kosul), parseBlokVeyaTekKomut("sürece", false), token.satir);
  }
  if (kontrol(TokenTuru::ANAHTAR_KELIME, "işlev")) {
    return parseIslevTanim();
  }
  if (kontrol(TokenTuru::ANAHTAR_KELIME, "dış_işlev") ||
      kontrol(TokenTuru::ANAHTAR_KELIME, "dis_islev")) {
    return parseDisIslevTanim();
  }
  if (kontrol(TokenTuru::ANAHTAR_KELIME, "tip")) {
    return parseSinifTanim();
  }
  if (kontrol(TokenTuru::ANAHTAR_KELIME, "deneme")) {
    return parseDenemeYakala();
  }
  if (kontrol(TokenTuru::ANAHTAR_KELIME, "kır")) {
    return parseKir();
  }
  if (kontrol(TokenTuru::ANAHTAR_KELIME, "devam")) {
    return parseDevam();
  }
  if (kontrol(TokenTuru::ANAHTAR_KELIME, "döndür")) {
    return parseDondur();
  }
  if (kontrol(TokenTuru::ANAHTAR_KELIME, "dahil_et")) {
    return parseDahilEt();
  }

  if (ifadeBaslangiciMi(bak())) {
    const OrhunToken baslangic = bak();
    return std::make_unique<IfadeKomutNode>(parseIfade(), baslangic.satir);
  }

  syntaxError(bak(), "Komut bekleniyordu.");
}

std::unique_ptr<ASTNode> Parser::parseAtama(std::unique_ptr<ASTNode> hedef,
                                            std::size_t satir,
                                            bool bildirimMi) {
  if (kontrol(TokenTuru::YENI_SATIR) || kontrol(TokenTuru::DOSYA_SONU)) {
    syntaxError(bak(),
                "'olsun' veya '=' atamasından sonra bir değer bekleniyor.");
  }

  return std::make_unique<AtamaNode>(std::move(hedef), parseIfade(), satir,
                                     bildirimMi);
}

std::unique_ptr<ASTNode>
Parser::parseCokluAtama(std::vector<std::string> hedefler, std::size_t satir,
                        bool bildirimMi) {
  if (kontrol(TokenTuru::YENI_SATIR) || kontrol(TokenTuru::DOSYA_SONU)) {
    syntaxError(bak(), "Çoklu atamadan sonra bir değer bekleniyor.");
  }
  return std::make_unique<CokluAtamaNode>(std::move(hedefler), parseIfade(),
                                          satir, bildirimMi);
}

std::unique_ptr<ASTNode> Parser::parseYazdir() {
  const OrhunToken token = bak();
  if ((token.tur == TokenTuru::ANAHTAR_KELIME && token.deger == "yazdır") ||
      (token.tur == TokenTuru::KIMLIK && token.deger == "yaz")) {
    ilerle();
  } else {
    tuket(TokenTuru::ANAHTAR_KELIME, "yazdır",
          "'yazdır' komutu bekleniyor.");
  }

  if (kontrol(TokenTuru::YENI_SATIR) || kontrol(TokenTuru::DOSYA_SONU)) {
    syntaxError(bak(),
                "'" + token.deger + "' komutundan sonra bir ifade bekleniyor.");
  }

  return std::make_unique<YazdirNode>(parseIfade(), token.satir);
}

std::unique_ptr<ASTNode> Parser::parseEger() {
  const OrhunToken token =
      tuket(TokenTuru::ANAHTAR_KELIME, "eğer", "'eğer' komutu bekleniyor.");

  if (kontrol(TokenTuru::YENI_SATIR) || kontrol(TokenTuru::DOSYA_SONU)) {
    syntaxError(bak(), "'eğer' komutundan sonra koşul ifadesi bekleniyor.");
  }

  std::unique_ptr<ASTNode> kosul = parseIfade();
  tuket(TokenTuru::ANAHTAR_KELIME, "ise", "Koşuldan sonra 'ise' bekleniyor.");
  if (kontrol(TokenTuru::ISLEM, ":")) {
    ilerle();
  }

  std::unique_ptr<BlockNode> dogruBlok = parseBlokVeyaTekKomut("eğer", false);

  std::unique_ptr<BlockNode> yanlisBlok;
  if (eslesir(TokenTuru::ANAHTAR_KELIME, "değilse")) {
    if (kontrol(TokenTuru::ISLEM, ":")) {
      ilerle();
    }
    yanlisBlok = parseBlokVeyaTekKomut("değilse", false);
  }

  return std::make_unique<EgerNode>(std::move(kosul), std::move(dogruBlok),
                                    std::move(yanlisBlok), token.satir);
}

std::unique_ptr<ASTNode> Parser::parseTekrarla() {
  const OrhunToken token = tuket(TokenTuru::ANAHTAR_KELIME, "tekrarla",
                                 "'tekrarla' komutu bekleniyor.");

  if (kontrol(TokenTuru::YENI_SATIR) || kontrol(TokenTuru::DOSYA_SONU)) {
    syntaxError(bak(), "'tekrarla' komutundan sonra tekrar sayısı bekleniyor.");
  }

  std::unique_ptr<ASTNode> kacKez = parseIfade();
  tuket(TokenTuru::ANAHTAR_KELIME, "kez",
        "Tekrar sayısından sonra 'kez' bekleniyor.");
  if (kontrol(TokenTuru::ISLEM, ":")) {
    ilerle();
  }

  return std::make_unique<TekrarlaNode>(
      std::move(kacKez), parseBlokVeyaTekKomut("tekrarla", false), token.satir);
}

std::unique_ptr<ASTNode> Parser::parseIslevTanim() {
  const OrhunToken token =
      tuket(TokenTuru::ANAHTAR_KELIME, "işlev", "'işlev' komutu bekleniyor.");
  const OrhunToken adToken = tuket(
      TokenTuru::KIMLIK, "'işlev' ifadesinden sonra işlev adı bekleniyor.");

  tuket(TokenTuru::ISLEM, "(", "İşlev adından sonra '(' bekleniyor.");
  ++ifadeAyracDerinligi_;

  std::vector<std::string> parametreler;
  std::vector<std::unique_ptr<ASTNode>> varsayilanlar;
  std::size_t girintiDerinligi = 0;
  ayracDuzeniniAtla(girintiDerinligi);
  if (!kontrol(TokenTuru::ISLEM, ")")) {
    parseIslevParametreleri(parametreler, varsayilanlar, girintiDerinligi);
  }

  tuket(TokenTuru::ISLEM, ")", "Parametre listesinin sonunda ')' bekleniyor.");
  --ifadeAyracDerinligi_;
  tuket(TokenTuru::ISLEM, ":", "İşlev başlığından sonra ':' bekleniyor.");

  return std::make_unique<IslevTanimNode>(
      adToken.deger, std::move(parametreler), std::move(varsayilanlar),
      parseBlokVeyaTekKomut("işlev", true), token.satir);
}

void Parser::parseIslevParametreleri(
    std::vector<std::string> &parametreler,
    std::vector<std::unique_ptr<ASTNode>> &varsayilanlar,
    std::size_t &girintiDerinligi) {
  bool varsayilanGoruldu = false;
  while (true) {
    parametreler.push_back(
        tuket(TokenTuru::KIMLIK, "Parametre adı bekleniyor.").deger);

    std::unique_ptr<ASTNode> varsayilanDeger;
    if (eslesir(TokenTuru::ANAHTAR_KELIME, "olsun")) {
      ayracDuzeniniAtla(girintiDerinligi);
      if (kontrol(TokenTuru::ISLEM, ")") || kontrol(TokenTuru::ISLEM, ",") ||
          kontrol(TokenTuru::DOSYA_SONU)) {
        syntaxError(bak(), "'olsun' ifadesinden sonra varsayılan değer "
                           "ifadesi bekleniyor.");
      }
      varsayilanDeger = parseIfade();
      varsayilanGoruldu = true;
    } else if (varsayilanGoruldu) {
      syntaxError(bak(), "Varsayılan değerli parametrelerden sonra zorunlu "
                         "parametre gelemez.");
    }

    varsayilanlar.push_back(std::move(varsayilanDeger));
    ayracDuzeniniAtla(girintiDerinligi);
    if (!eslesir(TokenTuru::ISLEM, ",")) {
      break;
    }
    ayracDuzeniniAtla(girintiDerinligi);
    if (kontrol(TokenTuru::ISLEM, ")")) {
      break;
    }
  }
}

std::unique_ptr<ASTNode> Parser::parseDisIslevTanim() {
  const OrhunToken token = bak();
  if (!eslesir(TokenTuru::ANAHTAR_KELIME, "dış_işlev") &&
      !eslesir(TokenTuru::ANAHTAR_KELIME, "dis_islev")) {
    syntaxError(token, "'dış_işlev' komutu bekleniyor.");
  }
  const OrhunToken adToken = tuket(
      TokenTuru::KIMLIK, "'dış_işlev' ifadesinden sonra işlev adı bekleniyor.");
  const OrhunToken kutuphaneToken = tuket(
      TokenTuru::METIN,
      "'dış_işlev' ifadesinde işlev adından sonra kütüphane metni bekleniyor.");

  tuket(TokenTuru::ISLEM, "(", "Dış işlev bildiriminde '(' bekleniyor.");

  std::vector<std::string> parametreAdlari;
  std::vector<std::string> parametreTipleri;
  std::size_t girintiDerinligi = 0;
  ayracDuzeniniAtla(girintiDerinligi);
  if (!kontrol(TokenTuru::ISLEM, ")")) {
    while (true) {
      const OrhunToken paramAdi =
          tuket(TokenTuru::KIMLIK,
                "Dış işlev parametrelerinde parametre adı bekleniyor.");
      tuket(TokenTuru::ISLEM, ":",
            "Dış işlev parametre adından sonra ':' bekleniyor.");

      const OrhunToken tipToken = bak();
      if (tipToken.tur != TokenTuru::KIMLIK &&
          tipToken.tur != TokenTuru::ANAHTAR_KELIME) {
        syntaxError(
            tipToken,
            "Dış işlev parametre tipi bekleniyor (örn: tam, metin, double).");
      }
      ilerle();

      parametreAdlari.push_back(paramAdi.deger);
      parametreTipleri.push_back(tipToken.deger);

      ayracDuzeniniAtla(girintiDerinligi);
      if (!eslesir(TokenTuru::ISLEM, ",")) {
        break;
      }
      ayracDuzeniniAtla(girintiDerinligi);
      if (kontrol(TokenTuru::ISLEM, ")")) {
        break;
      }
    }
  }

  tuket(TokenTuru::ISLEM, ")",
        "Dış işlev parametre listesi kapanırken ')' bekleniyor.");
  tuket(TokenTuru::ISLEM, "-", "Dış işlev bildiriminde '->' bekleniyor.");
  tuket(TokenTuru::ISLEM, ">", "Dış işlev bildiriminde '->' bekleniyor.");

  const OrhunToken donusTipiToken = bak();
  if (donusTipiToken.tur != TokenTuru::KIMLIK &&
      donusTipiToken.tur != TokenTuru::ANAHTAR_KELIME) {
    syntaxError(donusTipiToken,
                "Dış işlev bildiriminde dönüş tipi bekleniyor.");
  }
  ilerle();

  return std::make_unique<DisIslevTanimNode>(
      adToken.deger, kutuphaneToken.deger, std::move(parametreAdlari),
      std::move(parametreTipleri), donusTipiToken.deger, token.satir);
}

std::unique_ptr<ASTNode> Parser::parseSinifTanim() {
  const OrhunToken token =
      tuket(TokenTuru::ANAHTAR_KELIME, "tip", "'tip' komutu bekleniyor.");
  const OrhunToken adToken =
      tuket(TokenTuru::KIMLIK, "'tip' ifadesinden sonra sınıf adı bekleniyor.");
  std::string ebeveynAdi;
  if (eslesir(TokenTuru::ISLEM, "(")) {
    ebeveynAdi = tuket(TokenTuru::KIMLIK,
                       "Miras tanımında ebeveyn sınıf adı bekleniyor.")
                     .deger;
    tuket(TokenTuru::ISLEM, ")",
          "Miras tanımında ebeveyn sınıfından sonra ')' bekleniyor.");
  }
  tuket(TokenTuru::ISLEM, ":", "Sınıf adından sonra ':' bekleniyor.");

  return std::make_unique<SinifTanimNode>(adToken.deger, std::move(ebeveynAdi),
                                          parseBlokVeyaTekKomut("tip", true),
                                          token.satir);
}

std::unique_ptr<ASTNode> Parser::parseDenemeYakala() {
  const OrhunToken token =
      tuket(TokenTuru::ANAHTAR_KELIME, "deneme", "'deneme' komutu bekleniyor.");
  tuket(TokenTuru::ISLEM, ":", "'deneme' ifadesinden sonra ':' bekleniyor.");
  std::unique_ptr<BlockNode> denemeBlok = parseBlokVeyaTekKomut("deneme", true);

  yeniSatirlariAtla();
  tuket(TokenTuru::ANAHTAR_KELIME, "yakala",
        "'deneme' bloğundan sonra 'yakala' bekleniyor.");
  const OrhunToken hataToken =
      tuket(TokenTuru::KIMLIK,
            "'yakala' ifadesinden sonra hata değişkeni bekleniyor.");
  tuket(TokenTuru::ISLEM, ":", "'yakala' ifadesinden sonra ':' bekleniyor.");
  std::unique_ptr<BlockNode> yakalaBlok = parseBlokVeyaTekKomut("yakala", true);

  return std::make_unique<DenemeYakalaNode>(std::move(denemeBlok),
                                            hataToken.deger,
                                            std::move(yakalaBlok), token.satir);
}

std::unique_ptr<ASTNode> Parser::parseKir() {
  const OrhunToken token =
      tuket(TokenTuru::ANAHTAR_KELIME, "kır", "'kır' komutu bekleniyor.");
  return std::make_unique<KirNode>(token.satir);
}

std::unique_ptr<ASTNode> Parser::parseDevam() {
  const OrhunToken token =
      tuket(TokenTuru::ANAHTAR_KELIME, "devam", "'devam' komutu bekleniyor.");
  return std::make_unique<DevamNode>(token.satir);
}

std::unique_ptr<ASTNode> Parser::parseDondur() {
  const OrhunToken token =
      tuket(TokenTuru::ANAHTAR_KELIME, "döndür", "'döndür' komutu bekleniyor.");

  if (kontrol(TokenTuru::YENI_SATIR) || kontrol(TokenTuru::DOSYA_SONU)) {
    syntaxError(bak(), "'döndür' komutundan sonra bir ifade bekleniyor.");
  }

  return std::make_unique<DondurNode>(parseIfade(), token.satir);
}

std::unique_ptr<ASTNode> Parser::parseDahilEt() {
  const OrhunToken token = tuket(TokenTuru::ANAHTAR_KELIME, "dahil_et",
                                 "'dahil_et' komutu bekleniyor.");
  const OrhunToken dosya = tuket(
      TokenTuru::METIN, "'dahil_et' komutundan sonra dosya adı bekleniyor.");
  return std::make_unique<DahilEtNode>(dosya.deger, token.satir);
}

std::unique_ptr<BlockNode>
Parser::parseBlokVeyaTekKomut(const std::string &baglam, bool girintiZorunlu) {
  auto blok = std::make_unique<BlockNode>(bak().satir);

  if (eslesir(TokenTuru::YENI_SATIR)) {
    yeniSatirlariAtla();
    if (!eslesir(TokenTuru::GIRINTI)) {
      syntaxError(bak(), "'" + baglam + "' için girintili blok bekleniyor.");
    }

    yeniSatirlariAtla();

    while (!dosyaSonu() && !kontrol(TokenTuru::CIKINTI)) {
      if (kontrol(TokenTuru::HATA)) {
        syntaxError(bak(), bak().deger);
      }
      blok->komutEkle(parseKomut());
      yeniSatirlariAtla();
    }

    tuket(TokenTuru::CIKINTI, "'" + baglam + "' bloğu kapanmadı.");
    return blok;
  }

  if (girintiZorunlu) {
    syntaxError(bak(),
                "'" + baglam +
                    "' bloğu ':' sonrasında yeni satır ve girinti gerektirir.");
  }

  if (kontrol(TokenTuru::DOSYA_SONU) || kontrol(TokenTuru::YENI_SATIR)) {
    syntaxError(bak(),
                "'" + baglam + "' komutundan sonra bir komut bekleniyor.");
  }

  blok->komutEkle(parseKomut());
  return blok;
}

std::unique_ptr<ASTNode> Parser::parseIfade() { return parseVeya(); }

std::unique_ptr<ASTNode> Parser::parseVeya() {
  std::unique_ptr<ASTNode> sol = parseVe();

  while (kontrol(TokenTuru::ANAHTAR_KELIME, "veya")) {
    const OrhunToken op = ilerle();
    ifadeDevamDuzeniniAtla();
    std::unique_ptr<ASTNode> sag = parseVe();
    sol = std::make_unique<IkiliIslemNode>(std::move(sol), op.deger,
                                           std::move(sag), op.satir);
  }

  return sol;
}

std::unique_ptr<ASTNode> Parser::parseVe() {
  std::unique_ptr<ASTNode> sol = parseKarsilastirma();

  while (kontrol(TokenTuru::ANAHTAR_KELIME, "ve")) {
    const OrhunToken op = ilerle();
    ifadeDevamDuzeniniAtla();
    std::unique_ptr<ASTNode> sag = parseKarsilastirma();
    sol = std::make_unique<IkiliIslemNode>(std::move(sol), op.deger,
                                           std::move(sag), op.satir);
  }

  return sol;
}

std::unique_ptr<ASTNode> Parser::parseKarsilastirma() {
  std::unique_ptr<ASTNode> sol = parseToplama();

  while (kontrol(TokenTuru::ANAHTAR_KELIME, "eşit") ||
         kontrol(TokenTuru::ANAHTAR_KELIME, "eşit_değil") ||
         kontrol(TokenTuru::ANAHTAR_KELIME, "büyük") ||
         kontrol(TokenTuru::ANAHTAR_KELIME, "küçük")) {
    const OrhunToken op = ilerle();
    ifadeDevamDuzeniniAtla();
    std::unique_ptr<ASTNode> sag = parseToplama();
    sol = std::make_unique<IkiliIslemNode>(std::move(sol), op.deger,
                                           std::move(sag), op.satir);
  }

  return sol;
}

std::unique_ptr<ASTNode> Parser::parseToplama() {
  std::unique_ptr<ASTNode> sol = parseCarpma();

  while (kontrol(TokenTuru::ISLEM, "+") || kontrol(TokenTuru::ISLEM, "-")) {
    const OrhunToken op = ilerle();
    ifadeDevamDuzeniniAtla();
    std::unique_ptr<ASTNode> sag = parseCarpma();
    sol = std::make_unique<IkiliIslemNode>(std::move(sol), op.deger,
                                           std::move(sag), op.satir);
  }

  return sol;
}

std::unique_ptr<ASTNode> Parser::parseCarpma() {
  std::unique_ptr<ASTNode> sol = parseTekli();

  while (kontrol(TokenTuru::ISLEM, "*") || kontrol(TokenTuru::ISLEM, "/") ||
         kontrol(TokenTuru::ISLEM, "%")) {
    const OrhunToken op = ilerle();
    ifadeDevamDuzeniniAtla();
    std::unique_ptr<ASTNode> sag = parseTekli();
    sol = std::make_unique<IkiliIslemNode>(std::move(sol), op.deger,
                                           std::move(sag), op.satir);
  }

  return sol;
}

std::unique_ptr<ASTNode> Parser::parseTekli() {
  if (kontrol(TokenTuru::ANAHTAR_KELIME, "değil")) {
    const OrhunToken op = ilerle();
    ifadeDevamDuzeniniAtla();
    return std::make_unique<TekliIslemNode>(op.deger, parseTekli(), op.satir);
  }

  if (kontrol(TokenTuru::ISLEM, "-")) {
    const OrhunToken op = ilerle();
    ifadeDevamDuzeniniAtla();
    return std::make_unique<TekliIslemNode>(op.deger, parseTekli(), op.satir);
  }

  return parsePostfix();
}

std::unique_ptr<ASTNode> Parser::parsePostfix() {
  std::unique_ptr<ASTNode> ifade = parseBirincil();

  while (true) {
    if (eslesir(TokenTuru::ISLEM, "(")) {
      const OrhunToken acilisParantezi = onceki();
      std::vector<std::unique_ptr<ASTNode>> argumanlar;
      std::size_t girintiDerinligi = 0;
      ++ifadeAyracDerinligi_;
      ayracDuzeniniAtla(girintiDerinligi);

      if (!kontrol(TokenTuru::ISLEM, ")")) {
        while (true) {
          argumanlar.push_back(parseIfade());
          ayracDuzeniniAtla(girintiDerinligi);
          if (!eslesir(TokenTuru::ISLEM, ",")) {
            break;
          }
          ayracDuzeniniAtla(girintiDerinligi);
          if (kontrol(TokenTuru::ISLEM, ")")) {
            break;
          }
        }
      }

      tuket(TokenTuru::ISLEM, ")",
            "Argüman listesinin sonunda ')' bekleniyor.");
      --ifadeAyracDerinligi_;

      std::string cagriAdi;
      if (!cagrilabilirAdCoz(ifade.get(), cagriAdi)) {
        syntaxError(acilisParantezi,
                    "Sadece kimlikler veya alan erişimleri çağrılabilir.");
      }

      ifade = std::make_unique<IslevCagriNode>(
          std::move(cagriAdi), std::move(argumanlar), acilisParantezi.satir);
      continue;
    }

    if (eslesir(TokenTuru::ISLEM, "[")) {
      const OrhunToken acilisKose = onceki();
      std::unique_ptr<ASTNode> ilkParca;
      bool ilkParcaVar = false;
      std::size_t girintiDerinligi = 0;
      ++ifadeAyracDerinligi_;
      ayracDuzeniniAtla(girintiDerinligi);

      if (!kontrol(TokenTuru::ISLEM, ":")) {
        if (kontrol(TokenTuru::ISLEM, "]")) {
          syntaxError(bak(), "İndeks veya dilim ifadesi bekleniyor.");
        }
        ilkParca = parseIfade();
        ilkParcaVar = true;
        ayracDuzeniniAtla(girintiDerinligi);
      }

      if (eslesir(TokenTuru::ISLEM, ":")) {
        std::unique_ptr<ASTNode> baslangic;
        if (ilkParcaVar) {
          baslangic = std::move(ilkParca);
        }

        ayracDuzeniniAtla(girintiDerinligi);
        std::unique_ptr<ASTNode> bitis;
        if (!kontrol(TokenTuru::ISLEM, "]")) {
          bitis = parseIfade();
          ayracDuzeniniAtla(girintiDerinligi);
        }

        tuket(TokenTuru::ISLEM, "]", "Dilim erişiminde ']' bekleniyor.");
        --ifadeAyracDerinligi_;
        ifade = std::make_unique<DilimErisimNode>(
            std::move(ifade), std::move(baslangic), std::move(bitis),
            acilisKose.satir);
      } else {
        if (!ilkParcaVar) {
          syntaxError(bak(), "İndeks ifadesi bekleniyor.");
        }
        tuket(TokenTuru::ISLEM, "]", "İndeks erişiminde ']' bekleniyor.");
        --ifadeAyracDerinligi_;
        ifade = std::make_unique<IndeksErisimNode>(
            std::move(ifade), std::move(ilkParca), acilisKose.satir);
      }
      continue;
    }

    if (eslesir(TokenTuru::ISLEM, "?")) {
      const OrhunToken soruToken = onceki();
      tuket(TokenTuru::ISLEM, ".",
            "Guvenli erisim operatorunde '?' sonrasinda '.' bekleniyor.");
      const OrhunToken alanToken =
          tuket(TokenTuru::KIMLIK,
                "Guvenli erisimden sonra alan adi bekleniyor.");
      ifade = std::make_unique<GuvenliAlanErisimNode>(
          std::move(ifade), alanToken.deger, soruToken.satir);
      continue;
    }

    if (eslesir(TokenTuru::ISLEM, ".")) {
      const OrhunToken noktaToken = onceki();
      const OrhunToken alanToken = tuket(
          TokenTuru::KIMLIK, "Nokta erişiminden sonra alan adı bekleniyor.");
      if (const auto *kimlik = dynamic_cast<const KimlikNode *>(ifade.get());
          kimlik != nullptr) {
        if (kimlik->ad() == "benim") {
          ifade = std::make_unique<BenimErisimNode>(alanToken.deger,
                                                    noktaToken.satir);
        } else if (kimlik->ad() == "ust") {
          ifade = std::make_unique<UstErisimNode>(alanToken.deger,
                                                  noktaToken.satir);
        } else {
          ifade = std::make_unique<AlanErisimNode>(
              std::move(ifade), alanToken.deger, noktaToken.satir);
        }
      } else {
        ifade = std::make_unique<AlanErisimNode>(
            std::move(ifade), alanToken.deger, noktaToken.satir);
      }
      continue;
    }

    break;
  }

  return ifade;
}

std::unique_ptr<ASTNode> Parser::parseBirincil() {
  if (eslesir(TokenTuru::SAYI)) {
    return std::make_unique<SayiNode>(onceki().deger, onceki().satir);
  }
  if (eslesir(TokenTuru::ONDALIK)) {
    return std::make_unique<SayiNode>(onceki().deger, onceki().satir);
  }

  if (eslesir(TokenTuru::METIN)) {
    return std::make_unique<MetinNode>(onceki().deger, onceki().satir);
  }

  if (eslesir(TokenTuru::KIMLIK)) {
    return std::make_unique<KimlikNode>(onceki().deger, onceki().satir);
  }

  if (eslesir(TokenTuru::ANAHTAR_KELIME, "benim")) {
    return std::make_unique<KimlikNode>("benim", onceki().satir);
  }

  if (eslesir(TokenTuru::ANAHTAR_KELIME, "ust")) {
    return std::make_unique<KimlikNode>("ust", onceki().satir);
  }

  if (eslesir(TokenTuru::ANAHTAR_KELIME, "doğru")) {
    return std::make_unique<MantikNode>(true, onceki().satir);
  }

  if (eslesir(TokenTuru::ANAHTAR_KELIME, "yanlış")) {
    return std::make_unique<MantikNode>(false, onceki().satir);
  }

  if (eslesir(TokenTuru::ANAHTAR_KELIME, "sor")) {
    const OrhunToken token = onceki();
    if (kontrol(TokenTuru::YENI_SATIR) || kontrol(TokenTuru::DOSYA_SONU)) {
      syntaxError(bak(), "'sor' komutundan sonra bir soru ifadesi bekleniyor.");
    }
    return std::make_unique<SorNode>(parseIfade(), token.satir);
  }

  if (eslesir(TokenTuru::ANAHTAR_KELIME, "dahil_et")) {
    const OrhunToken token = onceki();
    const OrhunToken dosya = tuket(
        TokenTuru::METIN, "'dahil_et' ifadesinden sonra dosya adı bekleniyor.");
    return std::make_unique<DahilEtNode>(dosya.deger, token.satir);
  }

  if (eslesir(TokenTuru::ANAHTAR_KELIME, "işlev")) {
    const OrhunToken token = onceki();
    tuket(TokenTuru::ISLEM, "(",
          "Anonim işlev ifadesinde 'işlev' sonrası '(' bekleniyor.");
    ++ifadeAyracDerinligi_;

    std::vector<std::string> parametreler;
    std::vector<std::unique_ptr<ASTNode>> varsayilanlar;
    std::size_t girintiDerinligi = 0;
    ayracDuzeniniAtla(girintiDerinligi);
    if (!kontrol(TokenTuru::ISLEM, ")")) {
      parseIslevParametreleri(parametreler, varsayilanlar, girintiDerinligi);
    }

    tuket(TokenTuru::ISLEM, ")",
          "Anonim işlev parametre listesinin sonunda ')' bekleniyor.");
    --ifadeAyracDerinligi_;
    tuket(TokenTuru::ISLEM, ":",
          "Anonim işlev başlığından sonra ':' bekleniyor.");
    if (kontrol(TokenTuru::YENI_SATIR) || kontrol(TokenTuru::DOSYA_SONU)) {
      syntaxError(
          bak(),
          "Anonim işlev ':' sonrasında tek satırlı bir ifade döndürmelidir.");
    }

    auto govde = std::make_unique<BlockNode>(token.satir);
    govde->komutEkle(std::make_unique<DondurNode>(parseIfade(), token.satir));
    return std::make_unique<IsimsizIslevNode>(std::move(parametreler),
                                              std::move(varsayilanlar),
                                              std::move(govde), token.satir);
  }

  if (eslesir(TokenTuru::ANAHTAR_KELIME, "paralel")) {
    const OrhunToken token = onceki();
    tuket(TokenTuru::ANAHTAR_KELIME, "yap",
          "'paralel' ifadesinden sonra 'yap' bekleniyor.");
    tuket(TokenTuru::ISLEM, ":",
          "'paralel yap' ifadesinden sonra ':' bekleniyor.");
    return std::make_unique<ParalelYapNode>(
        parseBlokVeyaTekKomut("paralel yap", false), token.satir);
  }

  if (eslesir(TokenTuru::ANAHTAR_KELIME, "yeni")) {
    const OrhunToken token = onceki();
    const OrhunToken sinifAdi = tuket(
        TokenTuru::KIMLIK, "'yeni' ifadesinden sonra sınıf adı bekleniyor.");

    std::vector<std::unique_ptr<ASTNode>> argumanlar;
    if (eslesir(TokenTuru::ISLEM, "(")) {
      std::size_t girintiDerinligi = 0;
      ++ifadeAyracDerinligi_;
      ayracDuzeniniAtla(girintiDerinligi);
      if (!kontrol(TokenTuru::ISLEM, ")")) {
        while (true) {
          argumanlar.push_back(parseIfade());
          ayracDuzeniniAtla(girintiDerinligi);
          if (!eslesir(TokenTuru::ISLEM, ",")) {
            break;
          }
          ayracDuzeniniAtla(girintiDerinligi);
          if (kontrol(TokenTuru::ISLEM, ")")) {
            break;
          }
        }
      }
      tuket(TokenTuru::ISLEM, ")",
            "Kurucu argüman listesinin sonunda ')' bekleniyor.");
      --ifadeAyracDerinligi_;
    }

    return std::make_unique<YeniNesneNode>(sinifAdi.deger,
                                           std::move(argumanlar), token.satir);
  }

  if (eslesir(TokenTuru::ISLEM, "[")) {
    const OrhunToken token = onceki();
    std::size_t girintiDerinligi = 0;
    ++ifadeAyracDerinligi_;
    ayracDuzeniniAtla(girintiDerinligi);
    if (eslesir(TokenTuru::ISLEM, "]")) {
      --ifadeAyracDerinligi_;
      return std::make_unique<ListeNode>(
          std::vector<std::unique_ptr<ASTNode>>{}, token.satir);
    }

    std::unique_ptr<ASTNode> ilkIfade = parseIfade();
    ayracDuzeniniAtla(girintiDerinligi);
    if (eslesir(TokenTuru::ANAHTAR_KELIME, "için")) {
      const OrhunToken degisken = tuket(
          TokenTuru::KIMLIK, "Liste üretecinde döngü değişkeni bekleniyor.");
      tuket(TokenTuru::ANAHTAR_KELIME, "içinde",
            "Liste üretecinde 'içinde' anahtar kelimesi bekleniyor.");
      std::unique_ptr<ASTNode> kaynakListe = parseIfade();
      ayracDuzeniniAtla(girintiDerinligi);

      std::unique_ptr<ASTNode> kosul;
      if (eslesir(TokenTuru::ANAHTAR_KELIME, "eğer")) {
        kosul = parseIfade();
      }

      ayracDuzeniniAtla(girintiDerinligi);
      tuket(TokenTuru::ISLEM, "]", "Liste üretecinin sonunda ']' bekleniyor.");
      --ifadeAyracDerinligi_;
      return std::make_unique<ListeUretecNode>(
          std::move(ilkIfade), degisken.deger, std::move(kaynakListe),
          std::move(kosul), token.satir);
    }

    std::vector<std::unique_ptr<ASTNode>> ogeler;
    ogeler.push_back(std::move(ilkIfade));
    while (eslesir(TokenTuru::ISLEM, ",")) {
      ayracDuzeniniAtla(girintiDerinligi);
      if (kontrol(TokenTuru::ISLEM, "]")) {
        break;
      }
      ogeler.push_back(parseIfade());
      ayracDuzeniniAtla(girintiDerinligi);
    }
    tuket(TokenTuru::ISLEM, "]", "Liste ifadesinin sonunda ']' bekleniyor.");
    --ifadeAyracDerinligi_;
    return std::make_unique<ListeNode>(std::move(ogeler), token.satir);
  }

  if (eslesir(TokenTuru::ISLEM, "{")) {
    const OrhunToken token = onceki();
    std::vector<SozlukNode::OgeTipi> ogeler;
    std::size_t girintiDerinligi = 0;
    ++ifadeAyracDerinligi_;
    ayracDuzeniniAtla(girintiDerinligi);

    if (!kontrol(TokenTuru::ISLEM, "}")) {
      while (true) {
        const std::string anahtar = parseSozlukAnahtari();
        tuket(TokenTuru::ISLEM, ":",
              "Sözlükte anahtardan sonra ':' bekleniyor.");
        std::unique_ptr<ASTNode> deger = parseIfade();
        ogeler.push_back({anahtar, std::move(deger)});
        ayracDuzeniniAtla(girintiDerinligi);

        if (!eslesir(TokenTuru::ISLEM, ",")) {
          break;
        }
        ayracDuzeniniAtla(girintiDerinligi);
        if (kontrol(TokenTuru::ISLEM, "}")) {
          break;
        }
      }
    }

    tuket(TokenTuru::ISLEM, "}", "Sözlük ifadesinin sonunda '}' bekleniyor.");
    --ifadeAyracDerinligi_;
    return std::make_unique<SozlukNode>(std::move(ogeler), token.satir);
  }

  if (eslesir(TokenTuru::ISLEM, "(")) {
    std::size_t girintiDerinligi = 0;
    ++ifadeAyracDerinligi_;
    ayracDuzeniniAtla(girintiDerinligi);
    std::unique_ptr<ASTNode> ic = parseIfade();
    ayracDuzeniniAtla(girintiDerinligi);
    tuket(TokenTuru::ISLEM, ")", "Parantezli ifade kapanırken ')' bekleniyor.");
    --ifadeAyracDerinligi_;
    return ic;
  }

  syntaxError(
      bak(),
      "İfade bekleniyordu (sayı, metin, kimlik, liste, sözlük veya parantez).");
}

std::string Parser::parseSozlukAnahtari() {
  if (eslesir(TokenTuru::KIMLIK) || eslesir(TokenTuru::METIN) ||
      eslesir(TokenTuru::ANAHTAR_KELIME)) {
    return onceki().deger;
  }
  syntaxError(bak(), "Sözlük anahtarı bekleniyor (kimlik veya metin).");
}

bool Parser::dosyaSonu() const {
  return konum_ >= tokenlar_.size() ||
         tokenlar_[konum_].tur == TokenTuru::DOSYA_SONU;
}

const OrhunToken &Parser::bak() const {
  if (konum_ >= tokenlar_.size()) {
    return tokenlar_.back();
  }
  return tokenlar_[konum_];
}

const OrhunToken &Parser::bakIleri(std::size_t uzaklik) const {
  const std::size_t hedef = konum_ + uzaklik;
  if (hedef >= tokenlar_.size()) {
    return tokenlar_.back();
  }
  return tokenlar_[hedef];
}

const OrhunToken &Parser::onceki() const {
  if (konum_ == 0) {
    return tokenlar_.front();
  }
  return tokenlar_[konum_ - 1];
}

const OrhunToken &Parser::ilerle() {
  if (!dosyaSonu()) {
    ++konum_;
  }
  return onceki();
}

bool Parser::kontrol(TokenTuru tur) const {
  if (konum_ >= tokenlar_.size()) {
    return false;
  }
  return bak().tur == tur;
}

bool Parser::kontrol(TokenTuru tur, const std::string &deger) const {
  return kontrol(tur) && bak().deger == deger;
}

bool Parser::eslesir(TokenTuru tur) {
  if (!kontrol(tur)) {
    return false;
  }
  ilerle();
  return true;
}

bool Parser::eslesir(TokenTuru tur, const std::string &deger) {
  if (!kontrol(tur, deger)) {
    return false;
  }
  ilerle();
  return true;
}

const OrhunToken &Parser::tuket(TokenTuru tur, const std::string &hataMesaji) {
  if (kontrol(tur)) {
    return ilerle();
  }
  syntaxError(bak(), hataMesaji);
}

const OrhunToken &Parser::tuket(TokenTuru tur, const std::string &deger,
                                const std::string &hataMesaji) {
  if (kontrol(tur, deger)) {
    return ilerle();
  }
  syntaxError(bak(), hataMesaji);
}

void Parser::yeniSatirlariAtla() {
  while (eslesir(TokenTuru::YENI_SATIR)) {
    // Bilerek boş.
  }
}

void Parser::ayracDuzeniniAtla(std::size_t &girintiDerinligi) {
  while (true) {
    if (eslesir(TokenTuru::YENI_SATIR)) {
      continue;
    }
    if (eslesir(TokenTuru::GIRINTI)) {
      ++girintiDerinligi;
      continue;
    }
    if (girintiDerinligi > 0 && eslesir(TokenTuru::CIKINTI)) {
      --girintiDerinligi;
      continue;
    }
    break;
  }
}

void Parser::ifadeDevamDuzeniniAtla() {
  if (ifadeAyracDerinligi_ == 0) {
    return;
  }
  std::size_t girintiDerinligi = 0;
  ayracDuzeniniAtla(girintiDerinligi);
}

bool Parser::ifadeBaslangiciMi(const OrhunToken &token) const {
  if (token.tur == TokenTuru::SAYI || token.tur == TokenTuru::ONDALIK ||
      token.tur == TokenTuru::METIN || token.tur == TokenTuru::KIMLIK) {
    return true;
  }

  if (token.tur == TokenTuru::ISLEM &&
      (token.deger == "(" || token.deger == "[" || token.deger == "{" ||
       token.deger == "-")) {
    return true;
  }

  if (token.tur == TokenTuru::ANAHTAR_KELIME &&
      (token.deger == "sor" || token.deger == "doğru" ||
       token.deger == "yanlış" || token.deger == "değil" ||
       token.deger == "dahil_et" || token.deger == "yeni" ||
       token.deger == "benim" || token.deger == "ust" ||
       token.deger == "paralel" ||
       token.deger == "işlev")) {
    return true;
  }

  return false;
}

bool Parser::atanabilirHedefMi(const ASTNode *dugum) const {
  return dynamic_cast<const KimlikNode *>(dugum) != nullptr ||
         dynamic_cast<const AlanErisimNode *>(dugum) != nullptr ||
         dynamic_cast<const BenimErisimNode *>(dugum) != nullptr ||
         dynamic_cast<const IndeksErisimNode *>(dugum) != nullptr;
}

[[noreturn]] void Parser::syntaxError(const OrhunToken &token,
                                      const std::string &mesaj) const {
  const std::string gorunen = token.deger.empty() ? "<boş>" : token.deger;
  std::string detayliMesaj = mesaj;
  if (const auto oneri = akilliOneriBul(token, tokenlar_); oneri.has_value()) {
    detayliMesaj += " Bunu mu demek istediniz: '" + oneri.value() + "'?";
  }

  throw std::runtime_error("Satır " + std::to_string(token.satir) + ", Sütun " +
                           std::to_string(token.sutun) + ": " + detayliMesaj +
                           " [token: " + gorunen + "]");
}
