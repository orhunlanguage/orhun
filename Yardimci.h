#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// UTF-8 metni kaba bir şekilde Unicode kod noktalarına çevirir.
// Geçersiz bayt dizilerinde U+FFFD eklenir.
inline std::u32string utf8KodNoktalarinaCevir(std::string_view metin) {
  std::u32string sonuc;
  std::size_t i = 0;

  while (i < metin.size()) {
    const unsigned char b0 = static_cast<unsigned char>(metin[i]);
    char32_t cp = U'\uFFFD';
    std::size_t uzunluk = 1;

    if ((b0 & 0x80) == 0x00) {
      cp = b0;
      uzunluk = 1;
    } else if ((b0 & 0xE0) == 0xC0 && i + 1 < metin.size()) {
      const unsigned char b1 = static_cast<unsigned char>(metin[i + 1]);
      if ((b1 & 0xC0) == 0x80) {
        cp = (static_cast<char32_t>(b0 & 0x1F) << 6) |
             static_cast<char32_t>(b1 & 0x3F);
        uzunluk = 2;
      }
    } else if ((b0 & 0xF0) == 0xE0 && i + 2 < metin.size()) {
      const unsigned char b1 = static_cast<unsigned char>(metin[i + 1]);
      const unsigned char b2 = static_cast<unsigned char>(metin[i + 2]);
      if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
        cp = (static_cast<char32_t>(b0 & 0x0F) << 12) |
             (static_cast<char32_t>(b1 & 0x3F) << 6) |
             static_cast<char32_t>(b2 & 0x3F);
        uzunluk = 3;
      }
    } else if ((b0 & 0xF8) == 0xF0 && i + 3 < metin.size()) {
      const unsigned char b1 = static_cast<unsigned char>(metin[i + 1]);
      const unsigned char b2 = static_cast<unsigned char>(metin[i + 2]);
      const unsigned char b3 = static_cast<unsigned char>(metin[i + 3]);
      if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80 &&
          (b3 & 0xC0) == 0x80) {
        cp = (static_cast<char32_t>(b0 & 0x07) << 18) |
             (static_cast<char32_t>(b1 & 0x3F) << 12) |
             (static_cast<char32_t>(b2 & 0x3F) << 6) |
             static_cast<char32_t>(b3 & 0x3F);
        uzunluk = 4;
      }
    }

    sonuc.push_back(cp);
    i += uzunluk;
  }

  return sonuc;
}

// Levenshtein (edit distance) - Unicode kod noktaları üstünden hesaplar.
inline std::size_t levenshteinMesafesi(std::string_view a, std::string_view b) {
  const std::u32string ua = utf8KodNoktalarinaCevir(a);
  const std::u32string ub = utf8KodNoktalarinaCevir(b);

  const std::size_t n = ua.size();
  const std::size_t m = ub.size();

  if (n == 0) {
    return m;
  }
  if (m == 0) {
    return n;
  }

  std::vector<std::size_t> onceki(m + 1), simdiki(m + 1);
  for (std::size_t j = 0; j <= m; ++j) {
    onceki[j] = j;
  }

  for (std::size_t i = 1; i <= n; ++i) {
    simdiki[0] = i;
    for (std::size_t j = 1; j <= m; ++j) {
      const std::size_t sil = onceki[j] + 1;
      const std::size_t ekle = simdiki[j - 1] + 1;
      const std::size_t degistir =
          onceki[j - 1] + (ua[i - 1] == ub[j - 1] ? 0 : 1);
      simdiki[j] = std::min({sil, ekle, degistir});
    }
    std::swap(onceki, simdiki);
  }

  return onceki[m];
}

inline std::optional<std::string>
enYakinOneri(std::string_view aranan, const std::vector<std::string> &adaylar,
             std::size_t maxMesafe = 3) {
  std::optional<std::string> enIyi;
  std::size_t enIyiMesafe = maxMesafe + 1;

  for (const auto &aday : adaylar) {
    if (aday.empty()) {
      continue;
    }
    if (aday == aranan) {
      continue;
    }

    const std::size_t mesafe = levenshteinMesafesi(aranan, aday);
    if (mesafe < enIyiMesafe) {
      enIyiMesafe = mesafe;
      enIyi = aday;
    }
  }

  if (enIyi.has_value() && enIyiMesafe <= maxMesafe) {
    return enIyi;
  }
  return std::nullopt;
}
