#pragma once

#include "Token.h"

#include <string>
#include <vector>

// Orhun v0.4.1 sözcük çözümleyicisi.
// Girdi UTF-8 metindir; içeride UTF-32'e dönüştürülerek karakter bazında işlenir.
class Lexer {
public:
    explicit Lexer(const std::string& kaynakKodUtf8);

    // Kaynak kodu token listesine dönüştürür.
    std::vector<Token> tokenize();

private:
    std::u32string kaynakKod_;
    std::size_t konum_ = 0;
    std::size_t satir_ = 1;

    // UTF-8 <-> UTF-32 dönüşüm yardımcıları.
    static std::u32string utf8ToU32(const std::string& metin);
    static std::string u32ToUtf8(const std::u32string& metin);

    bool dosyaSonu() const;
    char32_t bak() const;
    char32_t bakIleri(std::size_t uzaklik) const;
    char32_t ilerle();

    Token kimlikVeyaAnahtarKelime();
    Token sayi();
    Token metin();

    // Yardımcı karakter kontrolleri.
    bool rakamMi(char32_t c) const;
    bool kimlikBaslangiciMi(char32_t c) const;
    bool kimlikKarakteriMi(char32_t c) const;
    bool turkceHarfMi(char32_t c) const;
    bool operatorMu(char32_t c) const;
    bool anahtarKelimeMi(const std::u32string& metin) const;

    // # ile başlayan yorumları satır sonuna kadar atlar.
    void yorumSatiriAtla();
};
