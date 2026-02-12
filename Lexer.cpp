#include "Lexer.h"

#include <unordered_set>
#include <vector>

Lexer::Lexer(const std::string &kaynakKodUtf8)
    : kaynakKod_(utf8ToU32(kaynakKodUtf8)) {
  // Bazı editörler dosya başına UTF-8 BOM ekleyebilir.
  if (!kaynakKod_.empty() && kaynakKod_.front() == U'\uFEFF') {
    kaynakKod_.erase(kaynakKod_.begin());
  }
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokenlar;

  // Python benzeri blok yapısı için girinti seviyelerini yığında tutuyoruz.
  std::vector<int> girintiYigini = {0};
  bool satirBasi = true;

  while (!dosyaSonu()) {
    if (satirBasi) {
      int girintiSayisi = 0;

      // Satır başındaki boşlukları ölç.
      while (!dosyaSonu()) {
        const char32_t c = bak();
        if (c == U' ') {
          ilerle();
          ++girintiSayisi;
          continue;
        }
        if (c == U'\t') {
          ilerle();
          girintiSayisi += 4;
          continue;
        }
        break;
      }

      if (dosyaSonu()) {
        break;
      }

      const char32_t c = bak();

      // Boş satır ve yorum satırı girinti üretmez.
      if (c != U'\n' && c != U'\r' && c != U'#') {
        const int mevcutGirinti = girintiYigini.back();
        if (girintiSayisi > mevcutGirinti) {
          girintiYigini.push_back(girintiSayisi);
          tokenlar.push_back({TokenType::GIRINTI, "<GIRINTI>", satir_});
        } else if (girintiSayisi < mevcutGirinti) {
          while (girintiYigini.size() > 1 &&
                 girintiSayisi < girintiYigini.back()) {
            girintiYigini.pop_back();
            tokenlar.push_back({TokenType::CIKINTI, "<CIKINTI>", satir_});
          }
          if (girintiSayisi != girintiYigini.back()) {
            tokenlar.push_back(
                {TokenType::HATA, "Geçersiz girinti seviyesi", satir_});
            tokenlar.push_back({TokenType::DOSYA_SONU, "", satir_});
            return tokenlar;
          }
        }
      }

      satirBasi = false;
    }

    const char32_t c = bak();

    // Satır içi boşlukları geç.
    if (c == U' ' || c == U'\t' || c == U'\v' || c == U'\f') {
      ilerle();
      continue;
    }

    // # ile başlayan yorumları satır sonuna kadar tamamen yoksay.
    if (c == U'#') {
      yorumSatiriAtla();
      continue;
    }

    if (c == U'\r') {
      const std::size_t mevcutSatir = satir_;
      ilerle();
      if (!dosyaSonu() && bak() == U'\n') {
        ilerle();
      }
      tokenlar.push_back({TokenType::YENI_SATIR, "\\n", mevcutSatir});
      ++satir_;
      satirBasi = true;
      continue;
    }

    if (c == U'\n') {
      const std::size_t mevcutSatir = satir_;
      ilerle();
      tokenlar.push_back({TokenType::YENI_SATIR, "\\n", mevcutSatir});
      ++satir_;
      satirBasi = true;
      continue;
    }

    if (c == U'"') {
      tokenlar.push_back(metin());
      continue;
    }

    if (rakamMi(c)) {
      tokenlar.push_back(sayi());
      continue;
    }

    if (kimlikBaslangiciMi(c)) {
      tokenlar.push_back(kimlikVeyaAnahtarKelime());
      continue;
    }

    if (operatorMu(c) || c == U'(' || c == U')' || c == U'[' || c == U']' ||
        c == U',' || c == U':' || c == U'{' || c == U'}' || c == U'.') {
      const std::u32string tekKarakter(1, ilerle());
      tokenlar.push_back({TokenType::ISLEM, u32ToUtf8(tekKarakter), satir_});
      continue;
    }

    // Desteklenmeyen karakterleri hata token'ına çevir.
    const std::u32string tekKarakter(1, ilerle());
    tokenlar.push_back({TokenType::HATA,
                        "Tanımsız karakter: " + u32ToUtf8(tekKarakter),
                        satir_});
  }

  // Dosya sonunda açık bloklar otomatik kapatılır.
  while (girintiYigini.size() > 1) {
    girintiYigini.pop_back();
    tokenlar.push_back({TokenType::CIKINTI, "<CIKINTI>", satir_});
  }

  tokenlar.push_back({TokenType::DOSYA_SONU, "", satir_});
  return tokenlar;
}

std::u32string Lexer::utf8ToU32(const std::string &metin) {
  std::u32string sonuc;
  std::size_t i = 0;

  while (i < metin.size()) {
    const unsigned char b0 = static_cast<unsigned char>(metin[i]);
    char32_t kodNoktasi = 0;
    std::size_t uzunluk = 0;

    if ((b0 & 0x80) == 0x00) {
      kodNoktasi = b0;
      uzunluk = 1;
    } else if ((b0 & 0xE0) == 0xC0) {
      if (i + 1 >= metin.size()) {
        sonuc.push_back(U'\uFFFD');
        break;
      }
      const unsigned char b1 = static_cast<unsigned char>(metin[i + 1]);
      if ((b1 & 0xC0) != 0x80) {
        sonuc.push_back(U'\uFFFD');
        ++i;
        continue;
      }
      kodNoktasi = (static_cast<char32_t>(b0 & 0x1F) << 6) |
                   static_cast<char32_t>(b1 & 0x3F);
      uzunluk = 2;
    } else if ((b0 & 0xF0) == 0xE0) {
      if (i + 2 >= metin.size()) {
        sonuc.push_back(U'\uFFFD');
        break;
      }
      const unsigned char b1 = static_cast<unsigned char>(metin[i + 1]);
      const unsigned char b2 = static_cast<unsigned char>(metin[i + 2]);
      if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) {
        sonuc.push_back(U'\uFFFD');
        ++i;
        continue;
      }
      kodNoktasi = (static_cast<char32_t>(b0 & 0x0F) << 12) |
                   (static_cast<char32_t>(b1 & 0x3F) << 6) |
                   static_cast<char32_t>(b2 & 0x3F);
      uzunluk = 3;
    } else if ((b0 & 0xF8) == 0xF0) {
      if (i + 3 >= metin.size()) {
        sonuc.push_back(U'\uFFFD');
        break;
      }
      const unsigned char b1 = static_cast<unsigned char>(metin[i + 1]);
      const unsigned char b2 = static_cast<unsigned char>(metin[i + 2]);
      const unsigned char b3 = static_cast<unsigned char>(metin[i + 3]);
      if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) {
        sonuc.push_back(U'\uFFFD');
        ++i;
        continue;
      }
      kodNoktasi = (static_cast<char32_t>(b0 & 0x07) << 18) |
                   (static_cast<char32_t>(b1 & 0x3F) << 12) |
                   (static_cast<char32_t>(b2 & 0x3F) << 6) |
                   static_cast<char32_t>(b3 & 0x3F);
      uzunluk = 4;
    } else {
      sonuc.push_back(U'\uFFFD');
      ++i;
      continue;
    }

    sonuc.push_back(kodNoktasi);
    i += uzunluk;
  }

  return sonuc;
}

std::string Lexer::u32ToUtf8(const std::u32string &metin) {
  std::string sonuc;

  for (const char32_t cp : metin) {
    if (cp <= 0x7F) {
      sonuc.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
      sonuc.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
      sonuc.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
      sonuc.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
      sonuc.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      sonuc.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      sonuc.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
      sonuc.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      sonuc.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      sonuc.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  return sonuc;
}

bool Lexer::dosyaSonu() const { return konum_ >= kaynakKod_.size(); }

char32_t Lexer::bak() const {
  if (dosyaSonu()) {
    return U'\0';
  }
  return kaynakKod_[konum_];
}

char32_t Lexer::bakIleri(std::size_t uzaklik) const {
  const std::size_t hedef = konum_ + uzaklik;
  if (hedef >= kaynakKod_.size()) {
    return U'\0';
  }
  return kaynakKod_[hedef];
}

char32_t Lexer::ilerle() {
  if (dosyaSonu()) {
    return U'\0';
  }
  return kaynakKod_[konum_++];
}

Token Lexer::kimlikVeyaAnahtarKelime() {
  const std::size_t baslangicSatir = satir_;
  std::u32string yazi;
  yazi.push_back(ilerle());

  while (!dosyaSonu() && kimlikKarakteriMi(bak())) {
    yazi.push_back(ilerle());
  }

  const std::string deger = u32ToUtf8(yazi);
  if (anahtarKelimeMi(yazi)) {
    return {TokenType::ANAHTAR_KELIME, deger, baslangicSatir};
  }
  return {TokenType::KIMLIK, deger, baslangicSatir};
}

Token Lexer::sayi() {
  const std::size_t baslangicSatir = satir_;
  std::u32string yazi;
  bool noktaGoruldu = false;

  while (!dosyaSonu()) {
    const char32_t c = bak();
    if (rakamMi(c)) {
      yazi.push_back(ilerle());
      continue;
    }

    // Ondalık destek: sadece bir kez ve ardından en az bir rakam varsa.
    if (c == U'.' && !noktaGoruldu && rakamMi(bakIleri(1))) {
      noktaGoruldu = true;
      yazi.push_back(ilerle());
      continue;
    }

    break;
  }

  return {noktaGoruldu ? TokenType::ONDALIK : TokenType::SAYI,
          u32ToUtf8(yazi), baslangicSatir};
}

Token Lexer::metin() {
  const std::size_t baslangicSatir = satir_;
  std::u32string icerik;

  // Açılış tırnağını tüket.
  ilerle();

  while (!dosyaSonu()) {
    const char32_t c = ilerle();

    if (c == U'"') {
      return {TokenType::METIN, u32ToUtf8(icerik), baslangicSatir};
    }

    // Basit kaçış dizileri desteği.
    if (c == U'\\' && !dosyaSonu()) {
      const char32_t k = ilerle();
      if (k == U'"') {
        icerik.push_back(U'"');
        continue;
      }
      if (k == U'\\') {
        icerik.push_back(U'\\');
        continue;
      }
      if (k == U'n') {
        icerik.push_back(U'\n');
        continue;
      }
      if (k == U't') {
        icerik.push_back(U'\t');
        continue;
      }
      icerik.push_back(k);
      continue;
    }

    if (c == U'\n' || c == U'\r') {
      return {TokenType::HATA, "Kapanmayan metin sabiti", baslangicSatir};
    }

    icerik.push_back(c);
  }

  return {TokenType::HATA, "Kapanmayan metin sabiti", baslangicSatir};
}

bool Lexer::rakamMi(char32_t c) const { return c >= U'0' && c <= U'9'; }

bool Lexer::kimlikBaslangiciMi(char32_t c) const {
  const bool asciiHarf = (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
  return c == U'_' || asciiHarf || turkceHarfMi(c);
}

bool Lexer::kimlikKarakteriMi(char32_t c) const {
  return kimlikBaslangiciMi(c) || rakamMi(c);
}

bool Lexer::turkceHarfMi(char32_t c) const {
  switch (c) {
  case U'ç':
  case U'Ç':
  case U'ğ':
  case U'Ğ':
  case U'ı':
  case U'I':
  case U'İ':
  case U'ö':
  case U'Ö':
  case U'ş':
  case U'Ş':
  case U'ü':
  case U'Ü':
    return true;
  default:
    return false;
  }
}

bool Lexer::operatorMu(char32_t c) const {
  return c == U'+' || c == U'-' || c == U'*' || c == U'/';
}

bool Lexer::anahtarKelimeMi(const std::u32string &metin) const {
  static const std::unordered_set<std::u32string> anahtarKelimeler = {
      U"yazdır", U"olsun",  U"eğer",     U"ise",  U"değilse",
      U"doğru",  U"yanlış", U"tekrarla", U"kez",  U"sor",
      U"işlev",  U"döndür", U"dahil_et", U"sürece", U"eşit",
      U"eşit_değil",
      U"büyük",  U"küçük",  U"ve",       U"veya", U"değil",
      U"tip",    U"yeni",   U"benim",    U"deneme", U"yakala",
      U"kır",    U"devam",  U"ust",      U"için", U"içinde"};

  return anahtarKelimeler.find(metin) != anahtarKelimeler.end();
}

void Lexer::yorumSatiriAtla() {
  while (!dosyaSonu()) {
    const char32_t c = bak();
    if (c == U'\n' || c == U'\r') {
      break;
    }
    ilerle();
  }
}
