#pragma once

#include "AST.h"
#include "OrhunToken.h"

#include <memory>
#include <string>
#include <vector>

// Orhun v0.8 parser'ı:
// Lexer token akışını AST'ye dönüştürür.
class Parser {
public:
  explicit Parser(std::vector<OrhunToken> tokenlar);

  // Tüm programı parse eder.
  std::unique_ptr<ProgramNode> parse();

private:
  std::vector<OrhunToken> tokenlar_;
  std::size_t konum_ = 0;

  // Komut ayrıştırma fonksiyonları.
  std::unique_ptr<ASTNode> parseKomut();
  std::unique_ptr<ASTNode> parseAtama(std::unique_ptr<ASTNode> hedef,
                                      std::size_t satir, bool bildirimMi);
  std::unique_ptr<ASTNode> parseCokluAtama(std::vector<std::string> hedefler,
                                           std::size_t satir, bool bildirimMi);
  std::unique_ptr<ASTNode> parseYazdir();
  std::unique_ptr<ASTNode> parseEger();
  std::unique_ptr<ASTNode> parseTekrarla();
  std::unique_ptr<ASTNode> parseIslevTanim();
  std::unique_ptr<ASTNode> parseDisIslevTanim();
  std::unique_ptr<ASTNode> parseSinifTanim();
  std::unique_ptr<ASTNode> parseDenemeYakala();
  std::unique_ptr<ASTNode> parseKir();
  std::unique_ptr<ASTNode> parseDevam();
  std::unique_ptr<ASTNode> parseDondur();
  std::unique_ptr<ASTNode> parseDahilEt();

  std::unique_ptr<BlockNode> parseBlokVeyaTekKomut(const std::string &baglam,
                                                   bool girintiZorunlu);

  // Recursive descent ifade önceliği.
  std::unique_ptr<ASTNode> parseIfade(); // veya
  std::unique_ptr<ASTNode> parseVeya();
  std::unique_ptr<ASTNode> parseVe();
  std::unique_ptr<ASTNode> parseKarsilastirma();
  std::unique_ptr<ASTNode> parseToplama();
  std::unique_ptr<ASTNode> parseCarpma();
  std::unique_ptr<ASTNode> parseTekli();
  std::unique_ptr<ASTNode> parsePostfix();
  std::unique_ptr<ASTNode> parseBirincil();
  void parseIslevParametreleri(
      std::vector<std::string> &parametreler,
      std::vector<std::unique_ptr<ASTNode>> &varsayilanlar);
  std::string parseSozlukAnahtari();
  bool atanabilirHedefMi(const ASTNode *dugum) const;

  // Token gezinme yardımcıları.
  bool dosyaSonu() const;
  const OrhunToken &bak() const;
  const OrhunToken &bakIleri(std::size_t uzaklik) const;
  const OrhunToken &onceki() const;
  const OrhunToken &ilerle();

  bool kontrol(TokenTuru tur) const;
  bool kontrol(TokenTuru tur, const std::string &deger) const;
  bool eslesir(TokenTuru tur);
  bool eslesir(TokenTuru tur, const std::string &deger);

  const OrhunToken &tuket(TokenTuru tur, const std::string &hataMesaji);
  const OrhunToken &tuket(TokenTuru tur, const std::string &deger,
                          const std::string &hataMesaji);

  void yeniSatirlariAtla();
  void koleksiyonDuzeniniAtla(std::size_t &girintiDerinligi);
  bool ifadeBaslangiciMi(const OrhunToken &token) const;

  [[noreturn]] void syntaxError(const OrhunToken &token,
                                const std::string &mesaj) const;
};
