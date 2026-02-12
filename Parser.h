#pragma once

#include "AST.h"
#include "Token.h"

#include <memory>
#include <string>
#include <vector>

// Orhun v0.5 parser'ı:
// Lexer token akışını AST'ye dönüştürür.
class Parser {
public:
  explicit Parser(std::vector<Token> tokenlar);

  // Tüm programı parse eder.
  std::unique_ptr<ProgramNode> parse();

private:
  std::vector<Token> tokenlar_;
  std::size_t konum_ = 0;

  // Komut ayrıştırma fonksiyonları.
  std::unique_ptr<ASTNode> parseKomut();
  std::unique_ptr<ASTNode> parseAtama(std::unique_ptr<ASTNode> hedef,
                                      std::size_t satir);
  std::unique_ptr<ASTNode> parseYazdir();
  std::unique_ptr<ASTNode> parseEger();
  std::unique_ptr<ASTNode> parseTekrarla();
  std::unique_ptr<ASTNode> parseIslevTanim();
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
  std::string parseSozlukAnahtari();
  bool atanabilirHedefMi(const ASTNode *dugum) const;

  // Token gezinme yardımcıları.
  bool dosyaSonu() const;
  const Token &bak() const;
  const Token &bakIleri(std::size_t uzaklik) const;
  const Token &onceki() const;
  const Token &ilerle();

  bool kontrol(TokenType tur) const;
  bool kontrol(TokenType tur, const std::string &deger) const;
  bool eslesir(TokenType tur);
  bool eslesir(TokenType tur, const std::string &deger);

  const Token &tuket(TokenType tur, const std::string &hataMesaji);
  const Token &tuket(TokenType tur, const std::string &deger,
                     const std::string &hataMesaji);

  void yeniSatirlariAtla();
  bool ifadeBaslangiciMi(const Token &token) const;

  [[noreturn]] void syntaxError(const Token &token,
                                const std::string &mesaj) const;
};
