#pragma once

#include <cstddef>
#include <string>

// Orhun dilinin token türleri.
// v0.5 ile sözlük/dot erişim sembolleri de ISLEM altında taşınır.
enum class TokenType {
    ANAHTAR_KELIME,
    KIMLIK,
    SAYI,
    METIN,
    ISLEM,
    YENI_SATIR,
    GIRINTI,
    CIKINTI,
    DOSYA_SONU,
    HATA
};

// Lexer çıktısındaki tek bir parçayı (token) temsil eder.
// satir bilgisi hata mesajlarında doğrudan kullanıcıya gösterilir.
struct Token {
    TokenType tur;
    std::string deger;
    std::size_t satir;
};
