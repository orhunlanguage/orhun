#include "Compiler.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

bool opEsitMi(const std::string &sol, const char *sag) { return sol == sag; }

std::vector<std::string> noktaylaBol(const std::string &ad) {
  std::vector<std::string> parcali;
  std::string parca;
  for (char c : ad) {
    if (c == '.') {
      if (!parca.empty()) {
        parcali.push_back(parca);
        parca.clear();
      }
    } else {
      parca.push_back(c);
    }
  }
  if (!parca.empty()) {
    parcali.push_back(parca);
  }
  return parcali;
}

bool yerTutucuYoluGecerliMi(const std::string &yol) {
  if (yol.empty()) {
    return false;
  }
  for (unsigned char c : yol) {
    // UTF-8 baytlarini da kabul et (>=128), boylece Turkce adlar engellenmez.
    if (!(std::isalnum(c) || c == '_' || c == '.' || c >= 128)) {
      return false;
    }
  }
  return true;
}

std::string kirpilmisKopya(const std::string &metin) {
  std::size_t sol = 0;
  while (sol < metin.size() &&
         std::isspace(static_cast<unsigned char>(metin[sol]))) {
    ++sol;
  }
  std::size_t sag = metin.size();
  while (sag > sol &&
         std::isspace(static_cast<unsigned char>(metin[sag - 1]))) {
    --sag;
  }
  return metin.substr(sol, sag - sol);
}

bool sabitSayiCoz(const ASTNode *dugum, double *sonuc) {
  const auto *sayi = dynamic_cast<const SayiNode *>(dugum);
  if (sayi == nullptr || sonuc == nullptr) {
    return false;
  }
  char *bitis = nullptr;
  const double deger = std::strtod(sayi->deger().c_str(), &bitis);
  if (bitis == sayi->deger().c_str() || (bitis != nullptr && *bitis != '\0')) {
    return false;
  }
  *sonuc = deger;
  return true;
}

bool sabitMantikCoz(const ASTNode *dugum, bool *sonuc) {
  const auto *mantik = dynamic_cast<const MantikNode *>(dugum);
  if (mantik == nullptr || sonuc == nullptr) {
    return false;
  }
  *sonuc = mantik->deger();
  return true;
}

bool sabitMetinCoz(const ASTNode *dugum, std::string *sonuc) {
  const auto *metin = dynamic_cast<const MetinNode *>(dugum);
  if (metin == nullptr || sonuc == nullptr) {
    return false;
  }
  *sonuc = metin->deger();
  return true;
}

bool sabitDogrulukCoz(const ASTNode *dugum, bool *sonuc) {
  if (dugum == nullptr || sonuc == nullptr) {
    return false;
  }
  bool mantik = false;
  if (sabitMantikCoz(dugum, &mantik)) {
    *sonuc = mantik;
    return true;
  }
  double sayi = 0.0;
  if (sabitSayiCoz(dugum, &sayi)) {
    *sonuc = std::abs(sayi) > std::numeric_limits<double>::epsilon();
    return true;
  }
  std::string metin;
  if (sabitMetinCoz(dugum, &metin)) {
    *sonuc = !metin.empty();
    return true;
  }
  if (const auto *liste = dynamic_cast<const ListeNode *>(dugum)) {
    *sonuc = !liste->ogeler().empty();
    return true;
  }
  if (const auto *sozluk = dynamic_cast<const SozlukNode *>(dugum)) {
    *sonuc = !sozluk->ogeler().empty();
    return true;
  }
  return false;
}

std::size_t opcodeUzunlugu(const BytecodeChunk &chunk, std::size_t ip) {
  if (ip >= chunk.kod.size()) {
    return 0;
  }
  const OpCode op = static_cast<OpCode>(chunk.kod[ip]);
  switch (op) {
  case OpCode::OP_SABIT:
  case OpCode::OP_GET_LOCAL:
  case OpCode::OP_SET_LOCAL:
  case OpCode::OP_DEFINE_LOCAL:
  case OpCode::OP_GET_GLOBAL:
  case OpCode::OP_SET_GLOBAL:
  case OpCode::OP_ALAN_AL:
  case OpCode::OP_GUVENLI_ALAN_AL:
  case OpCode::OP_ALAN_YAZ:
  case OpCode::OP_METOD_YAZ:
  case OpCode::OP_LISTE_OLUSTUR:
  case OpCode::OP_SOZLUK_OLUSTUR:
  case OpCode::OP_CAGIR:
  case OpCode::OP_SINIF:
  case OpCode::OP_ATLA:
  case OpCode::OP_ATLA_EGER_YANLIS:
  case OpCode::OP_DONGU:
  case OpCode::OP_TRY_BASLA:
    return 3;
  case OpCode::OP_ISLEV_OLUSTUR: {
    if (ip + 14 >= chunk.kod.size()) {
      return 0;
    }
    const std::uint16_t localAdSayisi = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(chunk.kod[ip + 13]) << 8) |
        static_cast<std::uint16_t>(chunk.kod[ip + 14]));
    return 15 + static_cast<std::size_t>(localAdSayisi) * 4;
  }
  default:
    return 1;
  }
}

std::uint16_t koddanU16(const std::vector<std::uint8_t> &kod, std::size_t ip) {
  if (ip + 1 >= kod.size()) {
    return 0;
  }
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(kod[ip]) << 8) |
                                    static_cast<std::uint16_t>(kod[ip + 1]));
}

std::string dugumTipiAdi(const ASTNode *dugum) {
  if (dugum == nullptr) {
    return "bos";
  }
  if (dynamic_cast<const AtamaNode *>(dugum) != nullptr)
    return "AtamaNode";
  if (dynamic_cast<const CokluAtamaNode *>(dugum) != nullptr)
    return "CokluAtamaNode";
  if (dynamic_cast<const YazdirNode *>(dugum) != nullptr)
    return "YazdirNode";
  if (dynamic_cast<const EgerNode *>(dugum) != nullptr)
    return "EgerNode";
  if (dynamic_cast<const SureceNode *>(dugum) != nullptr)
    return "SureceNode";
  if (dynamic_cast<const TekrarlaNode *>(dugum) != nullptr)
    return "TekrarlaNode";
  if (dynamic_cast<const IslevTanimNode *>(dugum) != nullptr)
    return "IslevTanimNode";
  if (dynamic_cast<const SinifTanimNode *>(dugum) != nullptr)
    return "SinifTanimNode";
  if (dynamic_cast<const DenemeYakalaNode *>(dugum) != nullptr)
    return "DenemeYakalaNode";
  if (dynamic_cast<const IslevCagriNode *>(dugum) != nullptr)
    return "IslevCagriNode";
  if (dynamic_cast<const IsimsizIslevNode *>(dugum) != nullptr)
    return "IsimsizIslevNode";
  if (dynamic_cast<const ParalelYapNode *>(dugum) != nullptr)
    return "ParalelYapNode";
  if (dynamic_cast<const IkiliIslemNode *>(dugum) != nullptr)
    return "IkiliIslemNode";
  if (dynamic_cast<const TekliIslemNode *>(dugum) != nullptr)
    return "TekliIslemNode";
  if (dynamic_cast<const KimlikNode *>(dugum) != nullptr)
    return "KimlikNode";
  if (dynamic_cast<const MetinNode *>(dugum) != nullptr)
    return "MetinNode";
  if (dynamic_cast<const SayiNode *>(dugum) != nullptr)
    return "SayiNode";
  if (dynamic_cast<const MantikNode *>(dugum) != nullptr)
    return "MantikNode";
  if (dynamic_cast<const ListeNode *>(dugum) != nullptr)
    return "ListeNode";
  if (dynamic_cast<const SozlukNode *>(dugum) != nullptr)
    return "SozlukNode";
  if (dynamic_cast<const AlanErisimNode *>(dugum) != nullptr)
    return "AlanErisimNode";
  if (dynamic_cast<const GuvenliAlanErisimNode *>(dugum) != nullptr)
    return "GuvenliAlanErisimNode";
  if (dynamic_cast<const IndeksErisimNode *>(dugum) != nullptr)
    return "IndeksErisimNode";
  if (dynamic_cast<const DilimErisimNode *>(dugum) != nullptr)
    return "DilimErisimNode";
  if (dynamic_cast<const BenimErisimNode *>(dugum) != nullptr)
    return "BenimErisimNode";
  if (dynamic_cast<const UstErisimNode *>(dugum) != nullptr)
    return "UstErisimNode";
  if (dynamic_cast<const DahilEtNode *>(dugum) != nullptr)
    return "DahilEtNode";
  if (dynamic_cast<const SorNode *>(dugum) != nullptr)
    return "SorNode";
  return "BilinmeyenNode";
}

} // namespace

BytecodeChunk Compiler::derle(const ProgramNode *program) {
  if (program == nullptr) {
    throw std::runtime_error("Derleyiciye bos program verildi.");
  }

  chunk_ = BytecodeChunk{};
  for (const auto &komut : program->komutlar()) {
    komutDerle(komut.get());
  }

  opcodeYaz(OpCode::OP_BOS, program->satir());
  opcodeYaz(OpCode::OP_DON, program->satir());
  peepholeOptimize();
  return chunk_;
}

void Compiler::komutDerle(const ASTNode *dugum) {
  if (dugum == nullptr) {
    return;
  }

  if (const auto *atama = dynamic_cast<const AtamaNode *>(dugum)) {
    atamaDerle(atama);
    return;
  }
  if (const auto *cokluAtama = dynamic_cast<const CokluAtamaNode *>(dugum)) {
    cokluAtamaDerle(cokluAtama);
    return;
  }
  if (const auto *yazdir = dynamic_cast<const YazdirNode *>(dugum)) {
    yazdirDerle(yazdir);
    return;
  }
  if (const auto *eger = dynamic_cast<const EgerNode *>(dugum)) {
    egerDerle(eger);
    return;
  }
  if (const auto *surece = dynamic_cast<const SureceNode *>(dugum)) {
    sureceDerle(surece);
    return;
  }
  if (const auto *tekrarla = dynamic_cast<const TekrarlaNode *>(dugum)) {
    tekrarlaDerle(tekrarla);
    return;
  }
  if (const auto *kir = dynamic_cast<const KirNode *>(dugum)) {
    kirDerle(kir);
    return;
  }
  if (const auto *devam = dynamic_cast<const DevamNode *>(dugum)) {
    devamDerle(devam);
    return;
  }
  if (const auto *deneme = dynamic_cast<const DenemeYakalaNode *>(dugum)) {
    denemeYakalaDerle(deneme);
    return;
  }
  if (const auto *blok = dynamic_cast<const BlockNode *>(dugum)) {
    blokDerle(blok);
    return;
  }
  if (const auto *ifadeKomut = dynamic_cast<const IfadeKomutNode *>(dugum)) {
    ifadeKomutDerle(ifadeKomut);
    return;
  }
  if (const auto *islev = dynamic_cast<const IslevTanimNode *>(dugum)) {
    islevTanimDerle(islev);
    return;
  }
  if (const auto *disIslev = dynamic_cast<const DisIslevTanimNode *>(dugum)) {
    disIslevTanimDerle(disIslev);
    return;
  }
  if (const auto *sinif = dynamic_cast<const SinifTanimNode *>(dugum)) {
    sinifTanimDerle(sinif);
    return;
  }
  if (const auto *dondur = dynamic_cast<const DondurNode *>(dugum)) {
    dondurDerle(dondur);
    return;
  }
  if (const auto *dahil = dynamic_cast<const DahilEtNode *>(dugum)) {
    // Komut formu: "dahil_et \"modul.oh\""
    ifadeDerle(dahil);
    opcodeYaz(OpCode::OP_POP, dahil->satir());
    return;
  }

  derlemeHatasi(
      dugum->satir(),
      "ORH-COMP-001: VM bu komutu derleyemedi (dugum=" + dugumTipiAdi(dugum) +
          "). Gecici fallback icin ORHUN_VM_FALLBACK=1 acik olmali.");
}

void Compiler::blokDerle(const BlockNode *dugum) {
  if (dugum == nullptr) {
    return;
  }
  for (const auto &komut : dugum->komutlar()) {
    komutDerle(komut.get());
  }
}

void Compiler::ifadeDerle(const ASTNode *dugum) {
  if (dugum == nullptr) {
    derlemeHatasi(0, "Bos ifade derlenemiyor.");
  }

  if (const auto *sayi = dynamic_cast<const SayiNode *>(dugum)) {
    char *bitis = nullptr;
    const double deger = std::strtod(sayi->deger().c_str(), &bitis);
    if (bitis == sayi->deger().c_str() ||
        (bitis != nullptr && *bitis != '\0')) {
      derlemeHatasi(sayi->satir(),
                    "Sayi sabiti parse edilemedi: '" + sayi->deger() + "'.");
    }
    sabitYaz(SabitDeger(deger), sayi->satir());
    return;
  }

  if (const auto *metin = dynamic_cast<const MetinNode *>(dugum)) {
    const std::string &s = metin->deger();
    if (s.find('{') == std::string::npos && s.find('}') == std::string::npos) {
      sabitYaz(SabitDeger(s), metin->satir());
      return;
    }

    // Birikimli string: "" + parcalar...
    sabitYaz(SabitDeger(std::string("")), metin->satir());
    const auto metinParcasiEkle = [&](std::size_t bas, std::size_t bit) {
      if (bit <= bas) {
        return;
      }
      sabitYaz(SabitDeger(s.substr(bas, bit - bas)), metin->satir());
      opcodeYaz(OpCode::OP_TOPLA, metin->satir());
    };

    std::size_t parcaBaslangic = 0;
    std::size_t i = 0;
    while (i < s.size()) {
      if (s[i] == '{' && i + 1 < s.size() && s[i + 1] == '{') {
        metinParcasiEkle(parcaBaslangic, i);
        sabitYaz(SabitDeger(std::string("{")), metin->satir());
        opcodeYaz(OpCode::OP_TOPLA, metin->satir());
        i += 2;
        parcaBaslangic = i;
        continue;
      }
      if (s[i] == '}' && i + 1 < s.size() && s[i + 1] == '}') {
        metinParcasiEkle(parcaBaslangic, i);
        sabitYaz(SabitDeger(std::string("}")), metin->satir());
        opcodeYaz(OpCode::OP_TOPLA, metin->satir());
        i += 2;
        parcaBaslangic = i;
        continue;
      }

      if (s[i] == '{') {
        const std::size_t kapanis = s.find('}', i + 1);
        if (kapanis != std::string::npos) {
          const std::string yol =
              kirpilmisKopya(s.substr(i + 1, kapanis - (i + 1)));
          if (yerTutucuYoluGecerliMi(yol)) {
            metinParcasiEkle(parcaBaslangic, i);
            cagrilanAdYukle(yol, metin->satir());
            opcodeYaz(OpCode::OP_TOPLA, metin->satir());
            i = kapanis + 1;
            parcaBaslangic = i;
            continue;
          }
        }
      }
      ++i;
    }

    metinParcasiEkle(parcaBaslangic, s.size());
    return;
  }

  if (const auto *mantik = dynamic_cast<const MantikNode *>(dugum)) {
    opcodeYaz(mantik->deger() ? OpCode::OP_DOGRU : OpCode::OP_YANLIS,
              mantik->satir());
    return;
  }

  if (const auto *kimlik = dynamic_cast<const KimlikNode *>(dugum)) {
    if (const std::uint16_t *local = localBul(kimlik->ad())) {
      chunk_.yazOpCode(OpCode::OP_GET_LOCAL, kimlik->satir());
      chunk_.yazU16(*local, kimlik->satir());
    } else {
      globalOperandYaz(OpCode::OP_GET_GLOBAL, kimlik->ad(), kimlik->satir());
    }
    return;
  }

  if (const auto *tekli = dynamic_cast<const TekliIslemNode *>(dugum)) {
    double sabitSayi = 0.0;
    bool sabitDogruluk = false;
    if (opEsitMi(tekli->op(), "-") &&
        sabitSayiCoz(tekli->ifade(), &sabitSayi)) {
      sabitYaz(SabitDeger(-sabitSayi), tekli->satir());
      return;
    }
    if ((opEsitMi(tekli->op(), "değil") || opEsitMi(tekli->op(), "degil")) &&
        sabitDogrulukCoz(tekli->ifade(), &sabitDogruluk)) {
      opcodeYaz(sabitDogruluk ? OpCode::OP_YANLIS : OpCode::OP_DOGRU,
                tekli->satir());
      return;
    }

    ifadeDerle(tekli->ifade());
    if (opEsitMi(tekli->op(), "-")) {
      opcodeYaz(OpCode::OP_NEGATE, tekli->satir());
      return;
    }
    if (opEsitMi(tekli->op(), "değil") || opEsitMi(tekli->op(), "degil")) {
      opcodeYaz(OpCode::OP_NOT, tekli->satir());
      return;
    }
    derlemeHatasi(tekli->satir(),
                  "Desteklenmeyen tekli operator: '" + tekli->op() + "'.");
  }

  if (const auto *ikili = dynamic_cast<const IkiliIslemNode *>(dugum)) {
    double solSayi = 0.0;
    double sagSayi = 0.0;
    bool solMantik = false;
    bool sagMantik = false;
    std::string solMetin;
    std::string sagMetin;
    const std::string &op = ikili->op();
    if (sabitSayiCoz(ikili->sol(), &solSayi) &&
        sabitSayiCoz(ikili->sag(), &sagSayi)) {
      if (opEsitMi(op, "+")) {
        sabitYaz(SabitDeger(solSayi + sagSayi), ikili->satir());
        return;
      }
      if (opEsitMi(op, "-")) {
        sabitYaz(SabitDeger(solSayi - sagSayi), ikili->satir());
        return;
      }
      if (opEsitMi(op, "*")) {
        sabitYaz(SabitDeger(solSayi * sagSayi), ikili->satir());
        return;
      }
      if (opEsitMi(op, "/") && sagSayi != 0.0) {
        sabitYaz(SabitDeger(solSayi / sagSayi), ikili->satir());
        return;
      }
      if (opEsitMi(op, "%") && sagSayi != 0.0) {
        sabitYaz(SabitDeger(std::fmod(solSayi, sagSayi)), ikili->satir());
        return;
      }
      if (opEsitMi(op, "eşit") || opEsitMi(op, "esit") || opEsitMi(op, "==")) {
        opcodeYaz((solSayi == sagSayi) ? OpCode::OP_DOGRU : OpCode::OP_YANLIS,
                  ikili->satir());
        return;
      }
      if (opEsitMi(op, "eşit_değil") || opEsitMi(op, "esit_degil") ||
          opEsitMi(op, "!=")) {
        opcodeYaz((solSayi != sagSayi) ? OpCode::OP_DOGRU : OpCode::OP_YANLIS,
                  ikili->satir());
        return;
      }
      if (opEsitMi(op, "büyük") || opEsitMi(op, "buyuk") || opEsitMi(op, ">")) {
        opcodeYaz((solSayi > sagSayi) ? OpCode::OP_DOGRU : OpCode::OP_YANLIS,
                  ikili->satir());
        return;
      }
      if (opEsitMi(op, "küçük") || opEsitMi(op, "kucuk") || opEsitMi(op, "<")) {
        opcodeYaz((solSayi < sagSayi) ? OpCode::OP_DOGRU : OpCode::OP_YANLIS,
                  ikili->satir());
        return;
      }
    }
    if (sabitMetinCoz(ikili->sol(), &solMetin) &&
        sabitMetinCoz(ikili->sag(), &sagMetin)) {
      if (opEsitMi(op, "+")) {
        sabitYaz(SabitDeger(solMetin + sagMetin), ikili->satir());
        return;
      }
      if (opEsitMi(op, "eşit") || opEsitMi(op, "esit") || opEsitMi(op, "==")) {
        opcodeYaz((solMetin == sagMetin) ? OpCode::OP_DOGRU
                                         : OpCode::OP_YANLIS,
                  ikili->satir());
        return;
      }
      if (opEsitMi(op, "eşit_değil") || opEsitMi(op, "esit_degil") ||
          opEsitMi(op, "!=")) {
        opcodeYaz((solMetin != sagMetin) ? OpCode::OP_DOGRU
                                         : OpCode::OP_YANLIS,
                  ikili->satir());
        return;
      }
    }
    if (sabitMantikCoz(ikili->sol(), &solMantik) &&
        sabitMantikCoz(ikili->sag(), &sagMantik)) {
      if (opEsitMi(op, "ve")) {
        opcodeYaz((solMantik && sagMantik) ? OpCode::OP_DOGRU
                                           : OpCode::OP_YANLIS,
                  ikili->satir());
        return;
      }
      if (opEsitMi(op, "veya")) {
        opcodeYaz((solMantik || sagMantik) ? OpCode::OP_DOGRU
                                           : OpCode::OP_YANLIS,
                  ikili->satir());
        return;
      }
      if (opEsitMi(op, "eşit") || opEsitMi(op, "esit") || opEsitMi(op, "==")) {
        opcodeYaz((solMantik == sagMantik) ? OpCode::OP_DOGRU
                                           : OpCode::OP_YANLIS,
                  ikili->satir());
        return;
      }
      if (opEsitMi(op, "eşit_değil") || opEsitMi(op, "esit_degil") ||
          opEsitMi(op, "!=")) {
        opcodeYaz((solMantik != sagMantik) ? OpCode::OP_DOGRU
                                           : OpCode::OP_YANLIS,
                  ikili->satir());
        return;
      }
    }

    if (opEsitMi(op, "ve")) {
      ifadeDerle(ikili->sol());
      const std::size_t solYanlisaAtla =
          atlaYaz(OpCode::OP_ATLA_EGER_YANLIS, ikili->satir());
      opcodeYaz(OpCode::OP_POP, ikili->satir());
      ifadeDerle(ikili->sag());
      opcodeYaz(OpCode::OP_DOGRU, ikili->satir());
      opcodeYaz(OpCode::OP_VE, ikili->satir());
      const std::size_t sonaAtla = atlaYaz(OpCode::OP_ATLA, ikili->satir());
      atlaYamala(solYanlisaAtla);
      opcodeYaz(OpCode::OP_POP, ikili->satir());
      opcodeYaz(OpCode::OP_YANLIS, ikili->satir());
      atlaYamala(sonaAtla);
      return;
    }

    if (opEsitMi(op, "veya")) {
      ifadeDerle(ikili->sol());
      const std::size_t sagTarafaAtla =
          atlaYaz(OpCode::OP_ATLA_EGER_YANLIS, ikili->satir());
      opcodeYaz(OpCode::OP_POP, ikili->satir());
      opcodeYaz(OpCode::OP_DOGRU, ikili->satir());
      const std::size_t sonaAtla = atlaYaz(OpCode::OP_ATLA, ikili->satir());
      atlaYamala(sagTarafaAtla);
      opcodeYaz(OpCode::OP_POP, ikili->satir());
      ifadeDerle(ikili->sag());
      opcodeYaz(OpCode::OP_YANLIS, ikili->satir());
      opcodeYaz(OpCode::OP_VEYA, ikili->satir());
      atlaYamala(sonaAtla);
      return;
    }

    ifadeDerle(ikili->sol());
    ifadeDerle(ikili->sag());

    if (opEsitMi(op, "+")) {
      opcodeYaz(OpCode::OP_TOPLA, ikili->satir());
      return;
    }
    if (opEsitMi(op, "-")) {
      opcodeYaz(OpCode::OP_CIKAR, ikili->satir());
      return;
    }
    if (opEsitMi(op, "*")) {
      opcodeYaz(OpCode::OP_CARP, ikili->satir());
      return;
    }
    if (opEsitMi(op, "/")) {
      opcodeYaz(OpCode::OP_BOL, ikili->satir());
      return;
    }
    if (opEsitMi(op, "%")) {
      opcodeYaz(OpCode::OP_MOD, ikili->satir());
      return;
    }
    if (opEsitMi(op, "eşit") || opEsitMi(op, "esit") || opEsitMi(op, "==")) {
      opcodeYaz(OpCode::OP_ESIT, ikili->satir());
      return;
    }
    if (opEsitMi(op, "eşit_değil") || opEsitMi(op, "esit_degil") ||
        opEsitMi(op, "!=")) {
      opcodeYaz(OpCode::OP_ESIT, ikili->satir());
      opcodeYaz(OpCode::OP_NOT, ikili->satir());
      return;
    }
    if (opEsitMi(op, "büyük") || opEsitMi(op, "buyuk") || opEsitMi(op, ">")) {
      opcodeYaz(OpCode::OP_BUYUK, ikili->satir());
      return;
    }
    if (opEsitMi(op, "küçük") || opEsitMi(op, "kucuk") || opEsitMi(op, "<")) {
      opcodeYaz(OpCode::OP_KUCUK, ikili->satir());
      return;
    }
    if (opEsitMi(op, ">=")) {
      opcodeYaz(OpCode::OP_KUCUK, ikili->satir());
      opcodeYaz(OpCode::OP_NOT, ikili->satir());
      return;
    }
    if (opEsitMi(op, "<=")) {
      opcodeYaz(OpCode::OP_BUYUK, ikili->satir());
      opcodeYaz(OpCode::OP_NOT, ikili->satir());
      return;
    }
    derlemeHatasi(ikili->satir(),
                  "Desteklenmeyen ikili operator: '" + ikili->op() + "'.");
  }

  if (const auto *alan = dynamic_cast<const AlanErisimNode *>(dugum)) {
    ifadeDerle(alan->hedef());
    const std::uint16_t alanSabit =
        chunk_.sabitEkle(SabitDeger(alan->alanAdi()));
    chunk_.yazOpCode(OpCode::OP_ALAN_AL, alan->satir());
    chunk_.yazU16(alanSabit, alan->satir());
    return;
  }

  if (const auto *guvenliAlan =
          dynamic_cast<const GuvenliAlanErisimNode *>(dugum)) {
    ifadeDerle(guvenliAlan->hedef());
    const std::uint16_t alanSabit =
        chunk_.sabitEkle(SabitDeger(guvenliAlan->alanAdi()));
    chunk_.yazOpCode(OpCode::OP_GUVENLI_ALAN_AL, guvenliAlan->satir());
    chunk_.yazU16(alanSabit, guvenliAlan->satir());
    return;
  }

  if (const auto *benim = dynamic_cast<const BenimErisimNode *>(dugum)) {
    if (const std::uint16_t *local = localBul("benim")) {
      chunk_.yazOpCode(OpCode::OP_GET_LOCAL, benim->satir());
      chunk_.yazU16(*local, benim->satir());
    } else {
      globalOperandYaz(OpCode::OP_GET_GLOBAL, "benim", benim->satir());
    }
    const std::uint16_t alanSabit =
        chunk_.sabitEkle(SabitDeger(benim->alanAdi()));
    chunk_.yazOpCode(OpCode::OP_ALAN_AL, benim->satir());
    chunk_.yazU16(alanSabit, benim->satir());
    return;
  }

  if (const auto *ust = dynamic_cast<const UstErisimNode *>(dugum)) {
    globalOperandYaz(OpCode::OP_GET_GLOBAL, "ust", ust->satir());
    const std::uint16_t alanSabit =
        chunk_.sabitEkle(SabitDeger(ust->metodAdi()));
    chunk_.yazOpCode(OpCode::OP_ALAN_AL, ust->satir());
    chunk_.yazU16(alanSabit, ust->satir());
    return;
  }

  if (const auto *indeks = dynamic_cast<const IndeksErisimNode *>(dugum)) {
    ifadeDerle(indeks->hedef());
    ifadeDerle(indeks->indeks());
    opcodeYaz(OpCode::OP_INDEKS_AL, indeks->satir());
    return;
  }

  if (const auto *dilim = dynamic_cast<const DilimErisimNode *>(dugum)) {
    globalOperandYaz(OpCode::OP_GET_GLOBAL, "dilim_al", dilim->satir());
    ifadeDerle(dilim->hedef());
    if (dilim->baslangic() != nullptr) {
      ifadeDerle(dilim->baslangic());
    } else {
      opcodeYaz(OpCode::OP_BOS, dilim->satir());
    }
    if (dilim->bitis() != nullptr) {
      ifadeDerle(dilim->bitis());
    } else {
      opcodeYaz(OpCode::OP_BOS, dilim->satir());
    }
    chunk_.yazOpCode(OpCode::OP_CAGIR, dilim->satir());
    chunk_.yazU16(3, dilim->satir());
    return;
  }

  if (const auto *liste = dynamic_cast<const ListeNode *>(dugum)) {
    for (const auto &oge : liste->ogeler()) {
      ifadeDerle(oge.get());
    }
    chunk_.yazOpCode(OpCode::OP_LISTE_OLUSTUR, liste->satir());
    chunk_.yazU16(static_cast<std::uint16_t>(liste->ogeler().size()),
                  liste->satir());
    return;
  }

  if (const auto *uretec = dynamic_cast<const ListeUretecNode *>(dugum)) {
    listeUretecDerle(uretec);
    return;
  }

  if (const auto *sozluk = dynamic_cast<const SozlukNode *>(dugum)) {
    for (const auto &[anahtar, deger] : sozluk->ogeler()) {
      sabitYaz(SabitDeger(anahtar), sozluk->satir());
      ifadeDerle(deger.get());
    }
    chunk_.yazOpCode(OpCode::OP_SOZLUK_OLUSTUR, sozluk->satir());
    chunk_.yazU16(static_cast<std::uint16_t>(sozluk->ogeler().size()),
                  sozluk->satir());
    return;
  }

  if (const auto *cagri = dynamic_cast<const IslevCagriNode *>(dugum)) {
    cagrilanAdYukle(cagri->ad(), cagri->satir());
    for (const auto &arg : cagri->argumanlar()) {
      ifadeDerle(arg.get());
    }
    chunk_.yazOpCode(OpCode::OP_CAGIR, cagri->satir());
    chunk_.yazU16(static_cast<std::uint16_t>(cagri->argumanlar().size()),
                  cagri->satir());
    return;
  }

  if (const auto *isimsiz = dynamic_cast<const IsimsizIslevNode *>(dugum)) {
    isimsizIslevDerle(isimsiz);
    return;
  }

  if (const auto *paralel = dynamic_cast<const ParalelYapNode *>(dugum)) {
    const auto &komutlar = paralel->govde()->komutlar();
    if (komutlar.empty()) {
      derlemeHatasi(
          paralel->satir(),
          "paralel yap ifadesi en az bir komut icermelidir.");
    }

    if (komutlar.size() > std::numeric_limits<std::uint16_t>::max()) {
      derlemeHatasi(
          paralel->satir(),
          "paralel yap ifadesinde cok fazla komut var (maks 65535).");
    }

    cagrilanAdYukle("gorev.baslat_plan", paralel->satir());

    for (const auto &komut : komutlar) {
      const auto *ifadeKomut = dynamic_cast<const IfadeKomutNode *>(komut.get());
      if (ifadeKomut == nullptr) {
        derlemeHatasi(paralel->satir(),
                      "paralel yap yalnizca cagrilardan olusan komut "
                      "satirlarini destekler.");
      }

      const auto *cagri = dynamic_cast<const IslevCagriNode *>(ifadeKomut->ifade());
      if (cagri == nullptr) {
        derlemeHatasi(paralel->satir(),
                      "paralel yap bloklari yalnizca bekle(...) veya "
                      "sistem.komut(...) cagrilari icerebilir.");
      }

      if (cagri->argumanlar().size() != 1) {
        derlemeHatasi(
            paralel->satir(),
            "paralel yap icindeki cagrilar simdilik tam 1 arguman "
            "almalidir.");
      }

      std::string adimTipi;
      if (cagri->ad() == "bekle" || cagri->ad() == "gorev.baslat_bekle") {
        adimTipi = "bekle";
      } else if (cagri->ad() == "sistem.komut" ||
                 cagri->ad() == "gorev.baslat_komut") {
        adimTipi = "komut";
      } else {
        derlemeHatasi(paralel->satir(),
                      "paralel yap bloklari yalnizca bekle(...) veya "
                      "sistem.komut(...) cagrilarini destekliyor.");
      }

      // Her komut icin {"tur": "...", "arg": ...} adim sozlugu olustur.
      sabitYaz(SabitDeger("tur"), paralel->satir());
      sabitYaz(SabitDeger(adimTipi), paralel->satir());
      sabitYaz(SabitDeger("arg"), paralel->satir());
      ifadeDerle(cagri->argumanlar().front().get());
      chunk_.yazOpCode(OpCode::OP_SOZLUK_OLUSTUR, paralel->satir());
      chunk_.yazU16(2, paralel->satir());
    }

    chunk_.yazOpCode(OpCode::OP_LISTE_OLUSTUR, paralel->satir());
    chunk_.yazU16(static_cast<std::uint16_t>(komutlar.size()), paralel->satir());
    chunk_.yazOpCode(OpCode::OP_CAGIR, paralel->satir());
    chunk_.yazU16(1, paralel->satir());
    return;
  }

  if (const auto *yeni = dynamic_cast<const YeniNesneNode *>(dugum)) {
    globalOperandYaz(OpCode::OP_GET_GLOBAL, yeni->sinifAdi(), yeni->satir());
    for (const auto &arg : yeni->argumanlar()) {
      ifadeDerle(arg.get());
    }
    chunk_.yazOpCode(OpCode::OP_CAGIR, yeni->satir());
    chunk_.yazU16(static_cast<std::uint16_t>(yeni->argumanlar().size()),
                  yeni->satir());
    return;
  }

  if (const auto *dahil = dynamic_cast<const DahilEtNode *>(dugum)) {
    globalOperandYaz(OpCode::OP_GET_GLOBAL, "dahil_et", dahil->satir());
    sabitYaz(SabitDeger(dahil->dosyaAdi()), dahil->satir());
    chunk_.yazOpCode(OpCode::OP_CAGIR, dahil->satir());
    chunk_.yazU16(1, dahil->satir());
    return;
  }
  if (const auto *sor = dynamic_cast<const SorNode *>(dugum)) {
    globalOperandYaz(OpCode::OP_GET_GLOBAL, "sor", sor->satir());
    ifadeDerle(sor->soruIfadesi());
    chunk_.yazOpCode(OpCode::OP_CAGIR, sor->satir());
    chunk_.yazU16(1, sor->satir());
    return;
  }

  derlemeHatasi(dugum->satir(),
                "ORH-COMP-002: VM bu ifadeyi derleyemedi (dugum=" +
                    dugumTipiAdi(dugum) + ").");
}

void Compiler::listeUretecDerle(const ListeUretecNode *dugum) {
  const std::size_t satir = dugum->satir();
  const std::string sonucAd = geciciAdUret("__vm_uretec_sonuc");
  const std::string kaynakAd = geciciAdUret("__vm_uretec_kaynak");
  const std::string indeksAd = geciciAdUret("__vm_uretec_indeks");

  // sonuc = []
  chunk_.yazOpCode(OpCode::OP_LISTE_OLUSTUR, satir);
  chunk_.yazU16(0, satir);
  degiskenYaz(sonucAd, satir);
  opcodeYaz(OpCode::OP_POP, satir);

  // kaynak = <kaynakListe>
  ifadeDerle(dugum->kaynakListe());
  degiskenYaz(kaynakAd, satir);
  opcodeYaz(OpCode::OP_POP, satir);

  if (dugum->kosul() == nullptr) {
    // Hot-path: filtre yoksa sonuc listesini kaynak uzunluğuna göre reserve et.
    degiskenYukle(sonucAd, satir);
    degiskenYukle(kaynakAd, satir);
    chunk_.yazOpCode(OpCode::OP_UZUNLUK, satir);
    chunk_.yazOpCode(OpCode::OP_LISTE_REZERVE, satir);
    opcodeYaz(OpCode::OP_POP, satir);
  }

  // indeks = 0
  sabitYaz(SabitDeger(0.0), satir);
  degiskenYaz(indeksAd, satir);
  opcodeYaz(OpCode::OP_POP, satir);

  const std::size_t loopBaslangic = chunk_.kod.size();

  // indeks < kaynak.uzunluk()
  degiskenYukle(indeksAd, satir);
  degiskenYukle(kaynakAd, satir);
  // Hot-path: uzunluk() native cagrisi yerine dogrudan OP_UZUNLUK.
  chunk_.yazOpCode(OpCode::OP_UZUNLUK, satir);
  opcodeYaz(OpCode::OP_KUCUK, satir);

  const std::size_t cikisAtlamasi = atlaYaz(OpCode::OP_ATLA_EGER_YANLIS, satir);
  opcodeYaz(OpCode::OP_POP, satir); // true kosul bool'unu temizle

  // donguDegiskeni = kaynak[indeks]
  degiskenYukle(kaynakAd, satir);
  degiskenYukle(indeksAd, satir);
  opcodeYaz(OpCode::OP_INDEKS_AL, satir);
  degiskenYaz(dugum->donguDegiskeni(), satir);
  opcodeYaz(OpCode::OP_POP, satir);

  std::size_t kosulYanlisaAtla = 0;
  std::size_t kosulSonunaAtla = 0;
  if (dugum->kosul() != nullptr) {
    ifadeDerle(dugum->kosul());
    kosulYanlisaAtla = atlaYaz(OpCode::OP_ATLA_EGER_YANLIS, satir);
    opcodeYaz(OpCode::OP_POP, satir); // true kosul bool
  }

  // sonuc.push(<ifade>) hot-path: metot cagrisi yerine dogrudan opcode.
  degiskenYukle(sonucAd, satir);
  ifadeDerle(dugum->ifade());
  chunk_.yazOpCode(OpCode::OP_LISTE_PUSH, satir);
  opcodeYaz(OpCode::OP_POP, satir); // liste.ekle donus degeri

  if (dugum->kosul() != nullptr) {
    kosulSonunaAtla = atlaYaz(OpCode::OP_ATLA, satir);
    atlaYamala(kosulYanlisaAtla);
    opcodeYaz(OpCode::OP_POP, satir); // false kosul bool
    atlaYamala(kosulSonunaAtla);
  }

  // indeks = indeks + 1
  degiskenYukle(indeksAd, satir);
  sabitYaz(SabitDeger(1.0), satir);
  opcodeYaz(OpCode::OP_TOPLA, satir);
  degiskenYaz(indeksAd, satir);
  opcodeYaz(OpCode::OP_POP, satir);

  donguYaz(loopBaslangic, satir);

  atlaYamala(cikisAtlamasi);
  opcodeYaz(OpCode::OP_POP, satir); // false kosul bool'unu temizle

  // Ifadenin sonucu olarak uretilen listeyi stack'e birak.
  degiskenYukle(sonucAd, satir);
}

void Compiler::isimsizIslevDerle(const IsimsizIslevNode *dugum) {
  const std::string kayitAdi = geciciAdUret("__anonim_islev");
  islevLiteralDerleOrtak(dugum->parametreler(), dugum->varsayilanlar(),
                         dugum->govde(), dugum->satir(), false, false, kayitAdi,
                         "");
}

void Compiler::atamaDerle(const AtamaNode *dugum) {
  const auto *kimlik = dynamic_cast<const KimlikNode *>(dugum->hedef());
  if (kimlik != nullptr) {
    ifadeDerle(dugum->ifade());
    atamaHedefiYaz(kimlik->ad(), dugum->satir(), dugum->bildirimMi());
    opcodeYaz(OpCode::OP_POP, dugum->satir());
    return;
  }

  if (const auto *alan = dynamic_cast<const AlanErisimNode *>(dugum->hedef())) {
    ifadeDerle(alan->hedef());
    ifadeDerle(dugum->ifade());
    const std::uint16_t alanSabit =
        chunk_.sabitEkle(SabitDeger(alan->alanAdi()));
    chunk_.yazOpCode(OpCode::OP_ALAN_YAZ, dugum->satir());
    chunk_.yazU16(alanSabit, dugum->satir());
    opcodeYaz(OpCode::OP_POP, dugum->satir());
    return;
  }

  if (const auto *benim =
          dynamic_cast<const BenimErisimNode *>(dugum->hedef())) {
    if (const std::uint16_t *local = localBul("benim")) {
      chunk_.yazOpCode(OpCode::OP_GET_LOCAL, dugum->satir());
      chunk_.yazU16(*local, dugum->satir());
    } else {
      globalOperandYaz(OpCode::OP_GET_GLOBAL, "benim", dugum->satir());
    }
    ifadeDerle(dugum->ifade());
    const std::uint16_t alanSabit =
        chunk_.sabitEkle(SabitDeger(benim->alanAdi()));
    chunk_.yazOpCode(OpCode::OP_ALAN_YAZ, dugum->satir());
    chunk_.yazU16(alanSabit, dugum->satir());
    opcodeYaz(OpCode::OP_POP, dugum->satir());
    return;
  }

  if (const auto *indeks =
          dynamic_cast<const IndeksErisimNode *>(dugum->hedef())) {
    ifadeDerle(indeks->hedef());
    ifadeDerle(indeks->indeks());
    ifadeDerle(dugum->ifade());
    opcodeYaz(OpCode::OP_INDEKS_YAZ, dugum->satir());
    opcodeYaz(OpCode::OP_POP, dugum->satir());
    return;
  }

  derlemeHatasi(dugum->satir(),
                "ORH-COMP-003: Atama hedefi desteklenmiyor (hedef=" +
                    dugumTipiAdi(dugum->hedef()) + ").");
}

void Compiler::cokluAtamaDerle(const CokluAtamaNode *dugum) {
  ifadeDerle(dugum->ifade());
  for (std::size_t i = 0; i < dugum->hedefler().size(); ++i) {
    opcodeYaz(OpCode::OP_KOPYA, dugum->satir());
    sabitYaz(SabitDeger(static_cast<double>(i)), dugum->satir());
    opcodeYaz(OpCode::OP_INDEKS_AL, dugum->satir());

    atamaHedefiYaz(dugum->hedefler()[i], dugum->satir(),
                   dugum->bildirimMi());
    opcodeYaz(OpCode::OP_POP, dugum->satir());
  }
  opcodeYaz(OpCode::OP_POP, dugum->satir());
}

void Compiler::yazdirDerle(const YazdirNode *dugum) {
  ifadeDerle(dugum->ifade());
  opcodeYaz(OpCode::OP_YAZDIR, dugum->satir());
}

void Compiler::egerDerle(const EgerNode *dugum) {
  bool sabitKosul = false;
  if (sabitDogrulukCoz(dugum->kosul(), &sabitKosul)) {
    if (sabitKosul) {
      blokDerle(dugum->dogruBlok());
    } else if (dugum->yanlisBlok() != nullptr) {
      blokDerle(dugum->yanlisBlok());
    }
    return;
  }

  ifadeDerle(dugum->kosul());
  const std::size_t yanlisaAtla =
      atlaYaz(OpCode::OP_ATLA_EGER_YANLIS, dugum->satir());
  opcodeYaz(OpCode::OP_POP, dugum->satir());
  blokDerle(dugum->dogruBlok());

  if (dugum->yanlisBlok() != nullptr) {
    const std::size_t sonaAtla = atlaYaz(OpCode::OP_ATLA, dugum->satir());
    atlaYamala(yanlisaAtla);
    opcodeYaz(OpCode::OP_POP, dugum->satir());
    blokDerle(dugum->yanlisBlok());
    atlaYamala(sonaAtla);
    return;
  }

  const std::size_t sonaAtla = atlaYaz(OpCode::OP_ATLA, dugum->satir());
  atlaYamala(yanlisaAtla);
  opcodeYaz(OpCode::OP_POP, dugum->satir());
  atlaYamala(sonaAtla);
}

void Compiler::sureceDerle(const SureceNode *dugum) {
  bool sabitKosul = false;
  const bool kosulSabit = sabitDogrulukCoz(dugum->kosul(), &sabitKosul);
  if (kosulSabit && !sabitKosul) {
    // sürece yanlış: hiç kod üretme
    return;
  }
  const bool kosulHepDogru = kosulSabit && sabitKosul;

  const std::size_t loopBaslangic = chunk_.kod.size();
  Loop loop;
  loop.loopStart = loopBaslangic;
  loop.continueDest = loopBaslangic;
  loop.continueHazir = true;
  loopStack_.push_back(loop);

  std::size_t cikisAtlamasi = 0;
  if (!kosulHepDogru) {
    ifadeDerle(dugum->kosul());
    cikisAtlamasi = atlaYaz(OpCode::OP_ATLA_EGER_YANLIS, dugum->satir());
    opcodeYaz(OpCode::OP_POP, dugum->satir());
  }

  blokDerle(dugum->govde());
  donguYaz(loopBaslangic, dugum->satir());

  if (!kosulHepDogru) {
    atlaYamala(cikisAtlamasi);
    opcodeYaz(OpCode::OP_POP, dugum->satir());
  }

  const std::size_t breakHedefi = chunk_.kod.size();
  for (const std::size_t kirAtlamasi : loopStack_.back().breakJumps) {
    atlaYamala(kirAtlamasi);
  }
  for (const std::size_t devamAtlamasi : loopStack_.back().continueJumps) {
    (void)devamAtlamasi;
    derlemeHatasi(
        dugum->satir(),
        "Ic hata: surece dongusunde yamanmamis 'devam' atlamasi kaldi.");
  }
  (void)breakHedefi;
  loopStack_.pop_back();
}

void Compiler::tekrarlaDerle(const TekrarlaNode *dugum) {
  if (const auto *sayi =
          dynamic_cast<const SayiNode *>(dugum->kacKezIfadesi())) {
    char *bitis = nullptr;
    const double adet = std::strtod(sayi->deger().c_str(), &bitis);
    if (bitis != sayi->deger().c_str() &&
        (bitis == nullptr || *bitis == '\0') && adet <= 0.0) {
      // tekrarla 0 kez: kod üretme
      return;
    }
  }

  // Sayac degeri dongu boyunca stack'te tutulur.
  ifadeDerle(dugum->kacKezIfadesi());
  const std::size_t loopKontrol = chunk_.kod.size();

  Loop loop;
  loop.loopStart = loopKontrol;
  loop.continueDest = 0;
  loop.continueHazir = false;
  loopStack_.push_back(loop);

  // sayac > 0 ?
  opcodeYaz(OpCode::OP_KOPYA, dugum->satir());
  sabitYaz(SabitDeger(0.0), dugum->satir());
  opcodeYaz(OpCode::OP_BUYUK, dugum->satir());
  const std::size_t cikisAtlamasi =
      atlaYaz(OpCode::OP_ATLA_EGER_YANLIS, dugum->satir());
  opcodeYaz(OpCode::OP_POP, dugum->satir()); // kosul bool'unu temizle

  blokDerle(dugum->govde());

  // devam hedefi: sayaci azaltma bolumu
  const std::size_t devamHedefi = chunk_.kod.size();
  Loop &aktifLoop = loopStack_.back();
  aktifLoop.continueDest = devamHedefi;
  aktifLoop.continueHazir = true;
  for (const std::size_t devamAtlamasi : aktifLoop.continueJumps) {
    atlaYamala(devamAtlamasi);
  }
  aktifLoop.continueJumps.clear();

  sabitYaz(SabitDeger(1.0), dugum->satir());
  opcodeYaz(OpCode::OP_CIKAR, dugum->satir()); // sayac = sayac - 1
  donguYaz(loopKontrol, dugum->satir());

  // Kosul false oldugunda stack: [sayac, false]
  atlaYamala(cikisAtlamasi);
  opcodeYaz(OpCode::OP_POP, dugum->satir()); // false bool
  const std::size_t breakHedefi = chunk_.kod.size();
  for (const std::size_t kirAtlamasi : aktifLoop.breakJumps) {
    atlaYamala(kirAtlamasi);
  }
  opcodeYaz(OpCode::OP_POP, dugum->satir()); // sayac
  (void)breakHedefi;
  loopStack_.pop_back();
}

void Compiler::kirDerle(const KirNode *dugum) {
  if (loopStack_.empty()) {
    derlemeHatasi(dugum->satir(), "'kır' komutu dongu disinda kullanilamaz.");
  }
  const std::size_t atlama = atlaYaz(OpCode::OP_ATLA, dugum->satir());
  loopStack_.back().breakJumps.push_back(atlama);
}

void Compiler::devamDerle(const DevamNode *dugum) {
  if (loopStack_.empty()) {
    derlemeHatasi(dugum->satir(), "'devam' komutu dongu disinda kullanilamaz.");
  }

  Loop &loop = loopStack_.back();
  if (!loop.continueHazir) {
    const std::size_t atlama = atlaYaz(OpCode::OP_ATLA, dugum->satir());
    loop.continueJumps.push_back(atlama);
    return;
  }

  if (loop.continueDest <= chunk_.kod.size()) {
    donguYaz(loop.continueDest, dugum->satir());
    return;
  }

  const std::size_t ileriAtlama = atlaYaz(OpCode::OP_ATLA, dugum->satir());
  loop.continueJumps.push_back(ileriAtlama);
}

void Compiler::denemeYakalaDerle(const DenemeYakalaNode *dugum) {
  // deneme baslangici: runtime hatasi olursa yakala bloguna atlayacak isaret.
  const std::size_t yakalaAtlama =
      atlaYaz(OpCode::OP_TRY_BASLA, dugum->satir());

  blokDerle(dugum->denemeBlogu());
  opcodeYaz(OpCode::OP_TRY_BITIR, dugum->satir());

  // deneme basariliysa yakala blogunu atla.
  const std::size_t sonAtlama = atlaYaz(OpCode::OP_ATLA, dugum->satir());

  // Hata yakalama hedefi.
  atlaYamala(yakalaAtlama);

  // VM, yakalanan hata metnini stack'e birakir.
  degiskenYaz(dugum->hataDegiskeni(), dugum->satir());
  opcodeYaz(OpCode::OP_POP, dugum->satir());

  blokDerle(dugum->yakalaBlogu());
  atlaYamala(sonAtlama);
}

void Compiler::ifadeKomutDerle(const IfadeKomutNode *dugum) {
  ifadeDerle(dugum->ifade());
  opcodeYaz(OpCode::OP_POP, dugum->satir());
}

void Compiler::islevTanimDerle(const IslevTanimNode *dugum) {
  islevLiteralDerle(dugum, false, false, dugum->ad(), "");
  globalOperandYaz(OpCode::OP_SET_GLOBAL, dugum->ad(), dugum->satir());
  opcodeYaz(OpCode::OP_POP, dugum->satir());
}

void Compiler::disIslevTanimDerle(const DisIslevTanimNode *dugum) {
  // ffi_dis_islev_tanimla(ad, kutuphane, donusTipi, [argTipleri])
  globalOperandYaz(OpCode::OP_GET_GLOBAL, "ffi_dis_islev_tanimla",
                   dugum->satir());
  sabitYaz(SabitDeger(dugum->ad()), dugum->satir());
  sabitYaz(SabitDeger(dugum->kutuphaneYolu()), dugum->satir());
  sabitYaz(SabitDeger(dugum->donusTipi()), dugum->satir());

  for (const std::string &tip : dugum->parametreTipleri()) {
    sabitYaz(SabitDeger(tip), dugum->satir());
  }
  chunk_.yazOpCode(OpCode::OP_LISTE_OLUSTUR, dugum->satir());
  chunk_.yazU16(static_cast<std::uint16_t>(dugum->parametreTipleri().size()),
                dugum->satir());

  chunk_.yazOpCode(OpCode::OP_CAGIR, dugum->satir());
  chunk_.yazU16(4, dugum->satir());
  opcodeYaz(OpCode::OP_POP, dugum->satir());
}

void Compiler::sinifTanimDerle(const SinifTanimNode *dugum) {
  const std::uint16_t adSabit = chunk_.sabitEkle(SabitDeger(dugum->ad()));
  chunk_.yazOpCode(OpCode::OP_SINIF, dugum->satir());
  chunk_.yazU16(adSabit, dugum->satir());
  globalOperandYaz(OpCode::OP_SET_GLOBAL, dugum->ad(), dugum->satir());
  opcodeYaz(OpCode::OP_POP, dugum->satir());

  if (!dugum->ebeveynAdi().empty()) {
    globalOperandYaz(OpCode::OP_GET_GLOBAL, dugum->ad(), dugum->satir());
    globalOperandYaz(OpCode::OP_GET_GLOBAL, dugum->ebeveynAdi(),
                     dugum->satir());
    opcodeYaz(OpCode::OP_MIRAS_AL, dugum->satir());
    opcodeYaz(OpCode::OP_POP, dugum->satir());
  }

  for (const auto &komut : dugum->govde()->komutlar()) {
    if (const auto *metod = dynamic_cast<const IslevTanimNode *>(komut.get())) {
      globalOperandYaz(OpCode::OP_GET_GLOBAL, dugum->ad(), metod->satir());
      islevLiteralDerle(metod, true, !dugum->ebeveynAdi().empty(), metod->ad(),
                        dugum->ad());
      const std::uint16_t ad = chunk_.sabitEkle(SabitDeger(metod->ad()));
      chunk_.yazOpCode(OpCode::OP_METOD_YAZ, metod->satir());
      chunk_.yazU16(ad, metod->satir());
      opcodeYaz(OpCode::OP_POP, metod->satir());
      continue;
    }

    if (const auto *atama = dynamic_cast<const AtamaNode *>(komut.get())) {
      if (const auto *hedefKimlik =
              dynamic_cast<const KimlikNode *>(atama->hedef());
          hedefKimlik != nullptr) {
        globalOperandYaz(OpCode::OP_GET_GLOBAL, dugum->ad(), atama->satir());
        ifadeDerle(atama->ifade());
        const std::uint16_t ad =
            chunk_.sabitEkle(SabitDeger(hedefKimlik->ad()));
        chunk_.yazOpCode(OpCode::OP_ALAN_YAZ, atama->satir());
        chunk_.yazU16(ad, atama->satir());
        opcodeYaz(OpCode::OP_POP, atama->satir());
        continue;
      }
    }

    derlemeHatasi(komut->satir(),
                  "Sinif govdesinde yalnizca alan atamasi ve islev tanimi "
                  "VM Faz 2'de destekleniyor.");
  }
}

void Compiler::dondurDerle(const DondurNode *dugum) {
  if (!islevIcindeyim()) {
    derlemeHatasi(dugum->satir(),
                  "'dondur' yalnizca islev icinde kullanilabilir.");
  }
  ifadeDerle(dugum->ifade());
  opcodeYaz(OpCode::OP_DON, dugum->satir());
}

void Compiler::islevLiteralDerleOrtak(
    const std::vector<std::string> &parametreler,
    const std::vector<std::unique_ptr<ASTNode>> &varsayilanlar,
    const BlockNode *govde, std::size_t satir, bool metodMu, bool ustGerekiyor,
    const std::string &kayitAdi, const std::string &sinifAdi) {
  const std::size_t atlaIndex = atlaYaz(OpCode::OP_ATLA, satir);
  const std::size_t giris = chunk_.kod.size();
  if (giris > std::numeric_limits<std::uint16_t>::max()) {
    derlemeHatasi(satir, "Islev giris adresi limiti asildi.");
  }

  IslevBaglami baglam;
  baglam.metodMu = metodMu;
  const auto localKaydet = [&](const std::string &ad) -> std::uint16_t {
    const std::uint16_t indeks = baglam.sonrakiLocal++;
    baglam.localIndeksler[ad] = indeks;
    if (baglam.localAdlari.size() <= indeks) {
      baglam.localAdlari.resize(static_cast<std::size_t>(indeks) + 1);
    }
    baglam.localAdlari[indeks] = ad;
    return indeks;
  };
  if (metodMu) {
    localKaydet("benim");
  }
  if (ustGerekiyor) {
    localKaydet("ust");
  }
  for (const std::string &param : parametreler) {
    if (baglam.localIndeksler.find(param) != baglam.localIndeksler.end()) {
      derlemeHatasi(satir, "Yinelenen parametre: " + param);
    }
    localKaydet(param);
  }
  islevYigini_.push_back(std::move(baglam));

  bool varsayilanGoruldu = false;
  std::size_t zorunluParametre = 0;
  for (std::size_t i = 0; i < parametreler.size(); ++i) {
    const bool varsayilanVar =
        i < varsayilanlar.size() && varsayilanlar[i] != nullptr;
    if (varsayilanVar) {
      varsayilanGoruldu = true;
    } else if (varsayilanGoruldu) {
      derlemeHatasi(
          satir,
          "Varsayilan parametrelerden sonra zorunlu parametre derlenemez.");
    } else {
      ++zorunluParametre;
    }

    if (!varsayilanVar) {
      continue;
    }

    const std::uint16_t local = *localBul(parametreler[i]);
    chunk_.yazOpCode(OpCode::OP_GET_LOCAL, satir);
    chunk_.yazU16(local, satir);
    opcodeYaz(OpCode::OP_BOS, satir);
    opcodeYaz(OpCode::OP_ESIT, satir);

    const std::size_t varsayilanAtla =
        atlaYaz(OpCode::OP_ATLA_EGER_YANLIS, satir);
    opcodeYaz(OpCode::OP_POP, satir);

    ifadeDerle(varsayilanlar[i].get());
    chunk_.yazOpCode(OpCode::OP_SET_LOCAL, satir);
    chunk_.yazU16(local, satir);
    opcodeYaz(OpCode::OP_POP, satir);

    const std::size_t blokSonunaAtla = atlaYaz(OpCode::OP_ATLA, satir);
    atlaYamala(varsayilanAtla);
    opcodeYaz(OpCode::OP_POP, satir);
    atlaYamala(blokSonunaAtla);
  }

  blokDerle(govde);
  opcodeYaz(OpCode::OP_BOS, satir);
  opcodeYaz(OpCode::OP_DON, satir);

  const IslevBaglami kapanan = islevYigini_.back();
  islevYigini_.pop_back();

  atlaYamala(atlaIndex);

  std::string tamAd = kayitAdi;
  if (!sinifAdi.empty()) {
    tamAd = sinifAdi + "." + kayitAdi;
  }
  const std::uint16_t adSabit = chunk_.sabitEkle(SabitDeger(tamAd));
  const std::uint16_t baglamArg =
      static_cast<std::uint16_t>((metodMu ? 1 : 0) + (ustGerekiyor ? 1 : 0));
  const std::uint16_t minArity =
      static_cast<std::uint16_t>(zorunluParametre + baglamArg);
  const std::uint16_t maxArity =
      static_cast<std::uint16_t>(parametreler.size() + baglamArg);
  const std::uint16_t girisU16 = static_cast<std::uint16_t>(giris);
  const std::uint16_t localSayisi = kapanan.sonrakiLocal;

  chunk_.yazOpCode(OpCode::OP_ISLEV_OLUSTUR, satir);
  chunk_.yazU16(adSabit, satir);
  chunk_.yazU16(minArity, satir);
  chunk_.yazU16(maxArity, satir);
  chunk_.yazU16(girisU16, satir);
  chunk_.yazU16(localSayisi, satir);
  chunk_.yazU16(baglamArg, satir);
  std::vector<std::pair<std::uint16_t, std::uint16_t>> localAdlari;
  for (std::size_t i = 0; i < kapanan.localAdlari.size(); ++i) {
    if (kapanan.localAdlari[i].empty()) {
      continue;
    }
    const std::uint16_t adSabit =
        chunk_.sabitEkle(SabitDeger(kapanan.localAdlari[i]));
    localAdlari.push_back(
        {static_cast<std::uint16_t>(i), static_cast<std::uint16_t>(adSabit)});
  }
  if (localAdlari.size() > std::numeric_limits<std::uint16_t>::max()) {
    derlemeHatasi(satir, "Islev local adi limiti asildi.");
  }
  chunk_.yazU16(static_cast<std::uint16_t>(localAdlari.size()), satir);
  for (const auto &[indeks, adSabit] : localAdlari) {
    chunk_.yazU16(indeks, satir);
    chunk_.yazU16(adSabit, satir);
  }
}

void Compiler::islevLiteralDerle(const IslevTanimNode *dugum, bool metodMu,
                                 bool ustGerekiyor, const std::string &kayitAdi,
                                 const std::string &sinifAdi) {
  islevLiteralDerleOrtak(dugum->parametreler(), dugum->varsayilanlar(),
                         dugum->govde(), dugum->satir(), metodMu, ustGerekiyor,
                         kayitAdi, sinifAdi);
}

void Compiler::cagrilanAdYukle(const std::string &ad, std::size_t satir) {
  const std::vector<std::string> parcali = noktaylaBol(ad);
  if (parcali.empty()) {
    derlemeHatasi(satir, "Bos cagri adi derlenemiyor.");
  }

  if (const std::uint16_t *local = localBul(parcali.front())) {
    chunk_.yazOpCode(OpCode::OP_GET_LOCAL, satir);
    chunk_.yazU16(*local, satir);
  } else {
    globalOperandYaz(OpCode::OP_GET_GLOBAL, parcali.front(), satir);
  }
  for (std::size_t i = 1; i < parcali.size(); ++i) {
    const std::uint16_t alanSabit = chunk_.sabitEkle(SabitDeger(parcali[i]));
    chunk_.yazOpCode(OpCode::OP_ALAN_AL, satir);
    chunk_.yazU16(alanSabit, satir);
  }
}

void Compiler::opcodeYaz(OpCode op, std::size_t satir) {
  chunk_.yazOpCode(op, satir);
}

void Compiler::sabitYaz(const SabitDeger &deger, std::size_t satir) {
  const std::uint16_t sabitIndeksi = chunk_.sabitEkle(deger);
  chunk_.yazOpCode(OpCode::OP_SABIT, satir);
  chunk_.yazU16(sabitIndeksi, satir);
}

void Compiler::degiskenYukle(const std::string &ad, std::size_t satir) {
  if (const std::uint16_t *local = localBul(ad)) {
    chunk_.yazOpCode(OpCode::OP_GET_LOCAL, satir);
    chunk_.yazU16(*local, satir);
    return;
  }
  globalOperandYaz(OpCode::OP_GET_GLOBAL, ad, satir);
}

void Compiler::degiskenYaz(const std::string &ad, std::size_t satir) {
  if (islevIcindeyim()) {
    const std::uint16_t local = localAlVeyaOlustur(ad);
    chunk_.yazOpCode(OpCode::OP_SET_LOCAL, satir);
    chunk_.yazU16(local, satir);
    return;
  }
  globalOperandYaz(OpCode::OP_SET_GLOBAL, ad, satir);
}

void Compiler::atamaHedefiYaz(const std::string &ad, std::size_t satir,
                              bool bildirimMi) {
  if (!islevIcindeyim()) {
    globalOperandYaz(OpCode::OP_SET_GLOBAL, ad, satir);
    return;
  }

  if (bildirimMi) {
    const std::uint16_t local = localAlVeyaOlustur(ad);
    chunk_.yazOpCode(OpCode::OP_DEFINE_LOCAL, satir);
    chunk_.yazU16(local, satir);
    return;
  }

  if (const std::uint16_t *local = localBul(ad)) {
    chunk_.yazOpCode(OpCode::OP_SET_LOCAL, satir);
    chunk_.yazU16(*local, satir);
    return;
  }

  globalOperandYaz(OpCode::OP_SET_GLOBAL, ad, satir);
}

std::string Compiler::geciciAdUret(const std::string &onEk) {
  return onEk + "_" + std::to_string(geciciSayac_++);
}

void Compiler::globalOperandYaz(OpCode op, const std::string &ad,
                                std::size_t satir) {
  const std::uint16_t sabitIndeksi = chunk_.sabitEkle(SabitDeger(ad));
  chunk_.yazOpCode(op, satir);
  chunk_.yazU16(sabitIndeksi, satir);
}

std::size_t Compiler::atlaYaz(OpCode op, std::size_t satir) {
  chunk_.yazOpCode(op, satir);
  chunk_.yazByte(0xFF, satir);
  chunk_.yazByte(0xFF, satir);
  return chunk_.kod.size() - 2;
}

void Compiler::atlaYamala(std::size_t ofsetIndeksi) {
  if (ofsetIndeksi + 1 >= chunk_.kod.size()) {
    throw std::runtime_error("Ic hata: yamalanacak atlama ofseti gecersiz.");
  }

  const std::size_t gecisMesafesi = chunk_.kod.size() - ofsetIndeksi - 2;
  if (gecisMesafesi > std::numeric_limits<std::uint16_t>::max()) {
    throw std::runtime_error("Derleme hatasi: atlama mesafesi cok buyuk.");
  }

  chunk_.kod[ofsetIndeksi] =
      static_cast<std::uint8_t>((gecisMesafesi >> 8) & 0xFF);
  chunk_.kod[ofsetIndeksi + 1] =
      static_cast<std::uint8_t>(gecisMesafesi & 0xFF);
}

void Compiler::donguYaz(std::size_t loopBaslangic, std::size_t satir) {
  const std::size_t komutBaslangici = chunk_.kod.size();
  chunk_.yazOpCode(OpCode::OP_DONGU, satir);

  const std::size_t ofset = komutBaslangici + 3 - loopBaslangic;
  if (ofset > std::numeric_limits<std::uint16_t>::max()) {
    throw std::runtime_error("Derleme hatasi: dongu geri atlamasi cok buyuk.");
  }
  chunk_.yazU16(static_cast<std::uint16_t>(ofset), satir);
}

void Compiler::peepholeOptimize() {
  if (chunk_.kod.empty()) {
    return;
  }

  std::vector<unsigned char> atlamaHedefi(chunk_.kod.size(), 0);
  for (std::size_t ip = 0; ip < chunk_.kod.size();) {
    const std::size_t uzunluk = opcodeUzunlugu(chunk_, ip);
    if (uzunluk == 0 || ip + uzunluk > chunk_.kod.size()) {
      break;
    }
    const OpCode op = static_cast<OpCode>(chunk_.kod[ip]);
    if (op == OpCode::OP_ATLA || op == OpCode::OP_ATLA_EGER_YANLIS ||
        op == OpCode::OP_TRY_BASLA) {
      const std::uint16_t ofset = koddanU16(chunk_.kod, ip + 1);
      const std::size_t hedef = ip + uzunluk + ofset;
      if (hedef < atlamaHedefi.size()) {
        atlamaHedefi[hedef] = 1;
      }
    } else if (op == OpCode::OP_DONGU) {
      const std::uint16_t ofset = koddanU16(chunk_.kod, ip + 1);
      if (ip + uzunluk >= ofset) {
        const std::size_t hedef = ip + uzunluk - ofset;
        if (hedef < atlamaHedefi.size()) {
          atlamaHedefi[hedef] = 1;
        }
      }
    }
    ip += uzunluk;
  }

  const auto nopaDonustur = [&](std::size_t bas, std::size_t uzunluk) {
    for (std::size_t i = 0; i < uzunluk && bas + i < chunk_.kod.size(); ++i) {
      chunk_.kod[bas + i] = static_cast<std::uint8_t>(OpCode::OP_NOP);
    }
  };

  for (std::size_t ip = 0; ip < chunk_.kod.size();) {
    const std::size_t uzunluk = opcodeUzunlugu(chunk_, ip);
    if (uzunluk == 0 || ip + uzunluk > chunk_.kod.size()) {
      break;
    }
    const std::size_t sonrakiIp = ip + uzunluk;
    if (sonrakiIp >= chunk_.kod.size()) {
      ip += uzunluk;
      continue;
    }

    const std::size_t sonrakiUzunluk = opcodeUzunlugu(chunk_, sonrakiIp);
    if (sonrakiUzunluk == 0 || sonrakiIp + sonrakiUzunluk > chunk_.kod.size()) {
      ip += uzunluk;
      continue;
    }
    if (atlamaHedefi[sonrakiIp] != 0) {
      ip += uzunluk;
      continue;
    }

    const OpCode op = static_cast<OpCode>(chunk_.kod[ip]);
    const OpCode sonrakiOp = static_cast<OpCode>(chunk_.kod[sonrakiIp]);
    const bool sabitYukleyici =
        op == OpCode::OP_SABIT || op == OpCode::OP_BOS ||
        op == OpCode::OP_DOGRU || op == OpCode::OP_YANLIS;

    if ((sabitYukleyici || op == OpCode::OP_KOPYA) &&
        sonrakiOp == OpCode::OP_POP) {
      nopaDonustur(ip, uzunluk);
      nopaDonustur(sonrakiIp, sonrakiUzunluk);
      ip = sonrakiIp + sonrakiUzunluk;
      continue;
    }

    if ((op == OpCode::OP_DOGRU || op == OpCode::OP_YANLIS) &&
        sonrakiOp == OpCode::OP_NOT) {
      chunk_.kod[ip] = static_cast<std::uint8_t>(
          op == OpCode::OP_DOGRU ? OpCode::OP_YANLIS : OpCode::OP_DOGRU);
      nopaDonustur(sonrakiIp, sonrakiUzunluk);
      ip = sonrakiIp + sonrakiUzunluk;
      continue;
    }

    ip += uzunluk;
  }
}

std::uint16_t Compiler::localAlVeyaOlustur(const std::string &ad) {
  if (!islevIcindeyim()) {
    throw std::runtime_error(
        "Ic hata: localAlVeyaOlustur islev disinda cagrildi.");
  }
  IslevBaglami &baglam = islevYigini_.back();
  const auto it = baglam.localIndeksler.find(ad);
  if (it != baglam.localIndeksler.end()) {
    return it->second;
  }
  if (baglam.sonrakiLocal >= std::numeric_limits<std::uint16_t>::max()) {
    throw std::runtime_error("Derleme hatasi: lokal degisken limiti asildi.");
  }
  const std::uint16_t yeni = baglam.sonrakiLocal++;
  baglam.localIndeksler[ad] = yeni;
  if (baglam.localAdlari.size() <= yeni) {
    baglam.localAdlari.resize(static_cast<std::size_t>(yeni) + 1);
  }
  baglam.localAdlari[yeni] = ad;
  return yeni;
}

const std::uint16_t *Compiler::localBul(const std::string &ad) const {
  if (!islevIcindeyim()) {
    return nullptr;
  }
  const IslevBaglami &baglam = islevYigini_.back();
  const auto it = baglam.localIndeksler.find(ad);
  if (it == baglam.localIndeksler.end()) {
    return nullptr;
  }
  return &it->second;
}

bool Compiler::islevIcindeyim() const { return !islevYigini_.empty(); }

[[noreturn]] void Compiler::derlemeHatasi(std::size_t satir,
                                          const std::string &mesaj) {
  throw std::runtime_error("VM Derleme Hatasi (satir " + std::to_string(satir) +
                           "): " + mesaj);
}
