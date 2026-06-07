#include "Chunk.h"
#include "Compiler.h"
#include "Interpreter.h"
#include "Lexer.h"
#include "Parser.h"
#include "Security/Hash.h"
#include "VM.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

constexpr char kPaketSihir[8] = {'O', 'R', 'H', 'N', 'P', 'K', 'G', '1'};

class CliCikisHatasi : public std::runtime_error {
public:
  CliCikisHatasi(int kod, const std::string &mesaj)
      : std::runtime_error(mesaj), kod_(kod) {}

  int kod() const noexcept { return kod_; }

private:
  int kod_;
};

std::string dosyaOku(const std::string &dosyaYolu) {
  std::ifstream dosya(dosyaYolu, std::ios::binary);
  if (!dosya.is_open()) {
    throw std::runtime_error("Hata: '" + dosyaYolu + "' dosyasi acilamadi.");
  }

  std::ostringstream tampon;
  tampon << dosya.rdbuf();
  return tampon.str();
}

std::vector<std::uint8_t> dosyaOkuIkili(const std::string &dosyaYolu) {
  std::ifstream dosya(dosyaYolu, std::ios::binary);
  if (!dosya.is_open()) {
    throw std::runtime_error("Hata: '" + dosyaYolu + "' dosyasi acilamadi.");
  }
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(dosya),
                                   std::istreambuf_iterator<char>());
}

void dosyaYaz(const std::string &dosyaYolu, const std::string &icerik) {
  std::ofstream dosya(dosyaYolu, std::ios::binary | std::ios::trunc);
  if (!dosya.is_open()) {
    throw std::runtime_error("Hata: '" + dosyaYolu + "' dosyasina yazilamadi.");
  }
  dosya << icerik;
}

void dosyaYazIkili(const std::string &dosyaYolu,
                   const std::vector<std::uint8_t> &icerik) {
  std::ofstream dosya(dosyaYolu, std::ios::binary | std::ios::trunc);
  if (!dosya.is_open()) {
    throw std::runtime_error("Hata: '" + dosyaYolu + "' dosyasina yazilamadi.");
  }
  if (!icerik.empty()) {
    dosya.write(reinterpret_cast<const char *>(icerik.data()),
                static_cast<std::streamsize>(icerik.size()));
  }
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

std::unique_ptr<ProgramNode> parseEt(const std::string &kaynakKod) {
  Lexer lexer(kaynakKod);
  std::vector<OrhunToken> tokenlar = lexer.tokenize();

  Parser parser(std::move(tokenlar));
  return parser.parse();
}

BytecodeChunk bytecodeDerle(const std::string &kaynakKod) {
  std::unique_ptr<ProgramNode> program = parseEt(kaynakKod);
  Compiler derleyici;
  return derleyici.derle(program.get());
}

bool kodCalistir(const std::string &kaynakKod, Interpreter &yorumlayici,
                 std::string *hataMesaji = nullptr) {
  try {
    std::unique_ptr<ProgramNode> program = parseEt(kaynakKod);
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

bool kodCalistirVM(const std::string &kaynakKod,
                   std::string *hataMesaji = nullptr) {
  try {
    BytecodeChunk chunk = bytecodeDerle(kaynakKod);
    VM vm;
    vm.calistir(chunk);
    return true;
  } catch (const std::exception &ex) {
    if (hataMesaji != nullptr) {
      *hataMesaji = ex.what();
      return false;
    }
    throw;
  }
}

std::uint32_t crc32Hesapla(const std::vector<std::uint8_t> &veri) {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (const std::uint8_t b : veri) {
    crc ^= static_cast<std::uint32_t>(b);
    for (int i = 0; i < 8; ++i) {
      const bool lsb = (crc & 1u) != 0u;
      crc >>= 1u;
      if (lsb) {
        crc ^= 0xEDB88320u;
      }
    }
  }
  return ~crc;
}

void streamU32Yaz(std::ofstream &dosya, std::uint32_t deger) {
  const std::uint8_t ham[4] = {
      static_cast<std::uint8_t>(deger & 0xFF),
      static_cast<std::uint8_t>((deger >> 8) & 0xFF),
      static_cast<std::uint8_t>((deger >> 16) & 0xFF),
      static_cast<std::uint8_t>((deger >> 24) & 0xFF),
  };
  dosya.write(reinterpret_cast<const char *>(ham), 4);
}

std::uint32_t hamdanU32(const std::uint8_t *ham) {
  return static_cast<std::uint32_t>(ham[0]) |
         (static_cast<std::uint32_t>(ham[1]) << 8) |
         (static_cast<std::uint32_t>(ham[2]) << 16) |
         (static_cast<std::uint32_t>(ham[3]) << 24);
}

void paketliExeUret(const std::string &calisanExeYolu,
                    const std::string &ciktiExeYolu,
                    const std::vector<std::uint8_t> &payload) {
  namespace fs = std::filesystem;
  fs::copy_file(fs::absolute(calisanExeYolu), fs::path(ciktiExeYolu),
                fs::copy_options::overwrite_existing);

  std::ofstream cikti(ciktiExeYolu, std::ios::binary | std::ios::app);
  if (!cikti.is_open()) {
    throw std::runtime_error("Hata: Paket exe acilamadi: " + ciktiExeYolu);
  }

  if (!payload.empty()) {
    cikti.write(reinterpret_cast<const char *>(payload.data()),
                static_cast<std::streamsize>(payload.size()));
  }

  const std::uint32_t boyut = static_cast<std::uint32_t>(payload.size());
  const std::uint32_t crc = crc32Hesapla(payload);
  cikti.write(kPaketSihir, static_cast<std::streamsize>(sizeof(kPaketSihir)));
  streamU32Yaz(cikti, boyut);
  streamU32Yaz(cikti, crc);
}

bool paketPayloadOku(const std::string &calisanExeYolu,
                     std::vector<std::uint8_t> &payload) {
  std::ifstream dosya(calisanExeYolu, std::ios::binary);
  if (!dosya.is_open()) {
    return false;
  }

  dosya.seekg(0, std::ios::end);
  const std::streamoff toplamBoyut = dosya.tellg();
  constexpr std::streamoff trailerBoyutu = 8 + 4 + 4;
  if (toplamBoyut < trailerBoyutu) {
    return false;
  }

  dosya.seekg(toplamBoyut - trailerBoyutu, std::ios::beg);
  std::uint8_t trailer[16] = {};
  dosya.read(reinterpret_cast<char *>(trailer), trailerBoyutu);
  if (dosya.gcount() != trailerBoyutu) {
    return false;
  }

  if (!std::equal(std::begin(kPaketSihir), std::end(kPaketSihir), trailer)) {
    return false;
  }

  const std::uint32_t payloadBoyutu = hamdanU32(trailer + 8);
  const std::uint32_t crcBeklenen = hamdanU32(trailer + 12);
  if (payloadBoyutu > static_cast<std::uint32_t>(toplamBoyut - trailerBoyutu)) {
    throw std::runtime_error("Paket bozuk: payload boyutu gecersiz.");
  }

  const std::streamoff payloadBaslangic =
      toplamBoyut - trailerBoyutu - static_cast<std::streamoff>(payloadBoyutu);
  dosya.seekg(payloadBaslangic, std::ios::beg);
  payload.resize(payloadBoyutu);
  if (payloadBoyutu > 0) {
    dosya.read(reinterpret_cast<char *>(payload.data()),
               static_cast<std::streamsize>(payloadBoyutu));
    if (static_cast<std::uint32_t>(dosya.gcount()) != payloadBoyutu) {
      throw std::runtime_error("Paket bozuk: payload okunamadi.");
    }
  }

  const std::uint32_t crcGercek = crc32Hesapla(payload);
  if (crcGercek != crcBeklenen) {
    throw std::runtime_error("Paket bozuk: CRC32 dogrulamasi basarisiz.");
  }

  return true;
}

bool gomuluPaketiCalistir(const std::string &calisanExeYolu) {
  std::vector<std::uint8_t> payload;
  if (!paketPayloadOku(calisanExeYolu, payload)) {
    return false;
  }

  BytecodeChunk chunk = chunkCoz(payload);
  VM vm;
  vm.calistir(chunk);
  return true;
}

int replCalistir() {
  Interpreter yorumlayici;
  std::string tampon;
  bool blokToplaniyor = false;

  std::cout << "Orhun REPL basladi. Cikmak icin 'cikis' yazin.\n";
  std::cout << "Blok komutlarinda (':' ile biten satirlar) calistirmak icin "
               "bos satir girin.\n";

  while (true) {
    std::cout << (tampon.empty() ? "orhun> " : "....> ");
    std::string satir;
    if (!std::getline(std::cin, satir)) {
      std::cout << '\n';
      break;
    }

    const std::string kirpilmis = sagaBoslukKirp(satir);
    if (tampon.empty() && (kirpilmis == "cikis" || kirpilmis == "çıkış" ||
                           kirpilmis == "exit" || kirpilmis == "quit")) {
      break;
    }

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

  if (!tampon.empty()) {
    std::string hata;
    if (!kodCalistir(tampon, yorumlayici, &hata)) {
      std::cerr << hata << '\n';
    }
  }

  return 0;
}

std::string tokeniYaziyaCevir(const OrhunToken &token) {
  if (token.tur != TokenTuru::METIN) {
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

bool noktalamaKapanisMi(const OrhunToken &token) {
  return token.tur == TokenTuru::ISLEM &&
         (token.deger == ")" || token.deger == "]" || token.deger == "}" ||
          token.deger == "," || token.deger == ":" || token.deger == ".");
}

bool acilisNoktalamasiMi(const OrhunToken &token) {
  return token.tur == TokenTuru::ISLEM &&
         (token.deger == "(" || token.deger == "[" || token.deger == "{" ||
          token.deger == ".");
}

bool boslukGerekliMi(const OrhunToken &onceki, const OrhunToken &simdiki) {
  if (simdiki.tur == TokenTuru::ISLEM && simdiki.deger == "?") {
    return false;
  }
  if (onceki.tur == TokenTuru::ISLEM && onceki.deger == "?") {
    return false;
  }
  if (simdiki.tur == TokenTuru::ISLEM && simdiki.deger == ".") {
    return false;
  }
  if (onceki.tur == TokenTuru::ISLEM && onceki.deger == ".") {
    return false;
  }
  if (noktalamaKapanisMi(simdiki)) {
    return false;
  }
  if (acilisNoktalamasiMi(onceki)) {
    return onceki.deger == ":";
  }
  if (simdiki.tur == TokenTuru::ISLEM && simdiki.deger == "(") {
    if (onceki.tur == TokenTuru::KIMLIK ||
        onceki.tur == TokenTuru::ANAHTAR_KELIME) {
      return false;
    }
    if (onceki.tur == TokenTuru::ISLEM &&
        (onceki.deger == ")" || onceki.deger == "]")) {
      return false;
    }
  }
  if (simdiki.tur == TokenTuru::ISLEM && simdiki.deger == "[") {
    if (onceki.tur == TokenTuru::KIMLIK ||
        onceki.tur == TokenTuru::ANAHTAR_KELIME) {
      return false;
    }
    if (onceki.tur == TokenTuru::ISLEM &&
        (onceki.deger == ")" || onceki.deger == "]")) {
      return false;
    }
  }
  if (onceki.tur == TokenTuru::ISLEM && onceki.deger == ",") {
    return true;
  }
  return true;
}

void sondakiBosluklariTemizle(std::string &satir) {
  while (!satir.empty() && (satir.back() == ' ' || satir.back() == '\t')) {
    satir.pop_back();
  }
}

std::string bicimlendir(const std::vector<OrhunToken> &tokenlar) {
  std::string sonuc;
  int girintiSeviyesi = 0;
  bool satirBasi = true;
  bool oncekiTokenVar = false;
  OrhunToken oncekiToken{};

  for (const OrhunToken &token : tokenlar) {
    if (token.tur == TokenTuru::DOSYA_SONU) {
      break;
    }
    if (token.tur == TokenTuru::HATA) {
      throw std::runtime_error(
          "Satir " + std::to_string(token.satir) +
          ": Bicimlendirme sirasinda lexer hatasi: " + token.deger);
    }
    if (token.tur == TokenTuru::GIRINTI) {
      ++girintiSeviyesi;
      continue;
    }
    if (token.tur == TokenTuru::CIKINTI) {
      girintiSeviyesi = std::max(0, girintiSeviyesi - 1);
      continue;
    }
    if (token.tur == TokenTuru::YENI_SATIR) {
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

std::string jsonKacis(const std::string &metin);

std::string tokenTuruAdi(TokenTuru tur) {
  switch (tur) {
  case TokenTuru::ANAHTAR_KELIME:
    return "ANAHTAR_KELIME";
  case TokenTuru::KIMLIK:
    return "KIMLIK";
  case TokenTuru::SAYI:
    return "SAYI";
  case TokenTuru::ONDALIK:
    return "ONDALIK";
  case TokenTuru::METIN:
    return "METIN";
  case TokenTuru::ISLEM:
    return "ISLEM";
  case TokenTuru::YENI_SATIR:
    return "YENI_SATIR";
  case TokenTuru::GIRINTI:
    return "GIRINTI";
  case TokenTuru::CIKINTI:
    return "CIKINTI";
  case TokenTuru::DOSYA_SONU:
    return "DOSYA_SONU";
  case TokenTuru::HATA:
    return "HATA";
  }
  return "BILINMEYEN";
}

std::string tokenlarJson(const std::vector<OrhunToken> &tokenlar) {
  std::ostringstream ss;
  ss << "[";
  for (std::size_t i = 0; i < tokenlar.size(); ++i) {
    if (i > 0) {
      ss << ",";
    }
    ss << "{\"tur\":\"" << tokenTuruAdi(tokenlar[i].tur) << "\","
       << "\"deger\":\"" << jsonKacis(tokenlar[i].deger) << "\","
       << "\"satir\":" << tokenlar[i].satir << ","
       << "\"sutun\":" << tokenlar[i].sutun << "}";
  }
  ss << "]";
  return ss.str();
}

std::string astDugumJson(const ASTNode *dugum);

std::string astDugumVeyaNullJson(const ASTNode *dugum) {
  return dugum == nullptr ? "null" : astDugumJson(dugum);
}

std::string astKomutlarJson(
    const std::vector<std::unique_ptr<ASTNode>> &komutlar) {
  std::ostringstream ss;
  ss << "[";
  for (std::size_t i = 0; i < komutlar.size(); ++i) {
    if (i > 0) {
      ss << ",";
    }
    ss << astDugumJson(komutlar[i].get());
  }
  ss << "]";
  return ss.str();
}

std::string astDugumDizisiJson(
    const std::vector<std::unique_ptr<ASTNode>> &dugumler) {
  std::ostringstream ss;
  ss << "[";
  for (std::size_t i = 0; i < dugumler.size(); ++i) {
    if (i > 0) {
      ss << ",";
    }
    ss << astDugumVeyaNullJson(dugumler[i].get());
  }
  ss << "]";
  return ss.str();
}

std::string metinDizisiJson(const std::vector<std::string> &degerler) {
  std::ostringstream ss;
  ss << "[";
  for (std::size_t i = 0; i < degerler.size(); ++i) {
    if (i > 0) {
      ss << ",";
    }
    ss << "\"" << jsonKacis(degerler[i]) << "\"";
  }
  ss << "]";
  return ss.str();
}

std::string opCodeAdi(OpCode op) {
  switch (op) {
  case OpCode::OP_SABIT: return "OP_SABIT";
  case OpCode::OP_BOS: return "OP_BOS";
  case OpCode::OP_DOGRU: return "OP_DOGRU";
  case OpCode::OP_YANLIS: return "OP_YANLIS";
  case OpCode::OP_POP: return "OP_POP";
  case OpCode::OP_KOPYA: return "OP_KOPYA";
  case OpCode::OP_GET_LOCAL: return "OP_GET_LOCAL";
  case OpCode::OP_SET_LOCAL: return "OP_SET_LOCAL";
  case OpCode::OP_DEFINE_LOCAL: return "OP_DEFINE_LOCAL";
  case OpCode::OP_GET_GLOBAL: return "OP_GET_GLOBAL";
  case OpCode::OP_SET_GLOBAL: return "OP_SET_GLOBAL";
  case OpCode::OP_ALAN_AL: return "OP_ALAN_AL";
  case OpCode::OP_ALAN_YAZ: return "OP_ALAN_YAZ";
  case OpCode::OP_METOD_YAZ: return "OP_METOD_YAZ";
  case OpCode::OP_INDEKS_AL: return "OP_INDEKS_AL";
  case OpCode::OP_INDEKS_YAZ: return "OP_INDEKS_YAZ";
  case OpCode::OP_UZUNLUK: return "OP_UZUNLUK";
  case OpCode::OP_LISTE_OLUSTUR: return "OP_LISTE_OLUSTUR";
  case OpCode::OP_LISTE_PUSH: return "OP_LISTE_PUSH";
  case OpCode::OP_LISTE_REZERVE: return "OP_LISTE_REZERVE";
  case OpCode::OP_SOZLUK_OLUSTUR: return "OP_SOZLUK_OLUSTUR";
  case OpCode::OP_ISLEV_OLUSTUR: return "OP_ISLEV_OLUSTUR";
  case OpCode::OP_CAGIR: return "OP_CAGIR";
  case OpCode::OP_SINIF: return "OP_SINIF";
  case OpCode::OP_MIRAS_AL: return "OP_MIRAS_AL";
  case OpCode::OP_TOPLA: return "OP_TOPLA";
  case OpCode::OP_CIKAR: return "OP_CIKAR";
  case OpCode::OP_CARP: return "OP_CARP";
  case OpCode::OP_BOL: return "OP_BOL";
  case OpCode::OP_MOD: return "OP_MOD";
  case OpCode::OP_NEGATE: return "OP_NEGATE";
  case OpCode::OP_NOT: return "OP_NOT";
  case OpCode::OP_ESIT: return "OP_ESIT";
  case OpCode::OP_BUYUK: return "OP_BUYUK";
  case OpCode::OP_KUCUK: return "OP_KUCUK";
  case OpCode::OP_VE: return "OP_VE";
  case OpCode::OP_VEYA: return "OP_VEYA";
  case OpCode::OP_YAZDIR: return "OP_YAZDIR";
  case OpCode::OP_ATLA: return "OP_ATLA";
  case OpCode::OP_ATLA_EGER_YANLIS: return "OP_ATLA_EGER_YANLIS";
  case OpCode::OP_DONGU: return "OP_DONGU";
  case OpCode::OP_TRY_BASLA: return "OP_TRY_BASLA";
  case OpCode::OP_TRY_BITIR: return "OP_TRY_BITIR";
  case OpCode::OP_DON: return "OP_DON";
  case OpCode::OP_NOP: return "OP_NOP";
  case OpCode::OP_GUVENLI_ALAN_AL: return "OP_GUVENLI_ALAN_AL";
  }
  return "OP_BILINMEYEN";
}

std::uint16_t bytecodeU16(const BytecodeChunk &chunk, std::size_t ip) {
  if (ip + 1 >= chunk.kod.size()) {
    throw std::runtime_error("Bozuk bytecode: U16 operand eksik.");
  }
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(chunk.kod[ip]) << 8) |
      static_cast<std::uint16_t>(chunk.kod[ip + 1]));
}

std::size_t bytecodeKomutUzunlugu(const BytecodeChunk &chunk, std::size_t ip) {
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
      throw std::runtime_error("Bozuk bytecode: islev operandlari eksik.");
    }
    const std::uint16_t localAdSayisi = bytecodeU16(chunk, ip + 13);
    return 15 + static_cast<std::size_t>(localAdSayisi) * 4;
  }
  default:
    return 1;
  }
}

std::string bytecodeSabitlerJson(const BytecodeChunk &chunk) {
  std::ostringstream ss;
  ss << std::setprecision(17) << "[";
  for (std::size_t i = 0; i < chunk.sabitler.size(); ++i) {
    if (i > 0) {
      ss << ",";
    }
    const SabitDeger &sabit = chunk.sabitler[i];
    if (std::holds_alternative<std::monostate>(sabit.veri)) {
      ss << "{\"tur\":\"bos\"}";
    } else if (const auto *sayi = std::get_if<double>(&sabit.veri)) {
      ss << "{\"tur\":\"sayi\",\"deger\":" << *sayi << "}";
    } else if (const auto *metin = std::get_if<std::string>(&sabit.veri)) {
      ss << "{\"tur\":\"metin\",\"deger\":\"" << jsonKacis(*metin)
         << "\"}";
    } else if (const auto *mantik = std::get_if<bool>(&sabit.veri)) {
      ss << "{\"tur\":\"mantik\",\"deger\":"
         << (*mantik ? "true" : "false") << "}";
    }
  }
  ss << "]";
  return ss.str();
}

std::string bytecodeSabitMetin(const BytecodeChunk &chunk,
                              std::uint16_t sabitIndeksi) {
  if (sabitIndeksi >= chunk.sabitler.size()) {
    throw std::runtime_error("Bozuk bytecode: metin sabiti indeksi gecersiz.");
  }
  const auto *metin = std::get_if<std::string>(&chunk.sabitler[sabitIndeksi].veri);
  if (metin == nullptr) {
    throw std::runtime_error("Bozuk bytecode: metin sabiti bekleniyor.");
  }
  return *metin;
}

std::string bytecodeIslevJson(const BytecodeChunk &chunk, std::size_t ip) {
  const std::uint16_t adSabit = bytecodeU16(chunk, ip + 1);
  const std::uint16_t minArity = bytecodeU16(chunk, ip + 3);
  const std::uint16_t maxArity = bytecodeU16(chunk, ip + 5);
  const std::uint16_t giris = bytecodeU16(chunk, ip + 7);
  const std::uint16_t localSayisi = bytecodeU16(chunk, ip + 9);
  const std::uint16_t baglamArg = bytecodeU16(chunk, ip + 11);
  const std::uint16_t localAdSayisi = bytecodeU16(chunk, ip + 13);

  std::ostringstream ss;
  ss << "{\"ad_sabit\":" << adSabit << ",\"ad\":\""
     << jsonKacis(bytecodeSabitMetin(chunk, adSabit))
     << "\",\"min_arity\":" << minArity << ",\"max_arity\":" << maxArity
     << ",\"giris\":" << giris << ",\"local_sayisi\":" << localSayisi
     << ",\"baglam_arg\":" << baglamArg
     << ",\"local_adlari\":[";
  for (std::uint16_t i = 0; i < localAdSayisi; ++i) {
    if (i > 0) {
      ss << ",";
    }
    const std::size_t pairIp = ip + 15 + static_cast<std::size_t>(i) * 4;
    const std::uint16_t localIndeks = bytecodeU16(chunk, pairIp);
    const std::uint16_t localAdSabit = bytecodeU16(chunk, pairIp + 2);
    ss << "{\"indeks\":" << localIndeks << ",\"ad_sabit\":" << localAdSabit
       << ",\"ad\":\""
       << jsonKacis(bytecodeSabitMetin(chunk, localAdSabit)) << "\"}";
  }
  ss << "]}";
  return ss.str();
}

std::string bytecodeKomutlarJson(const BytecodeChunk &chunk,
                                 std::size_t *komutSayisi = nullptr) {
  std::ostringstream ss;
  ss << "[";
  std::size_t ip = 0;
  std::size_t sayi = 0;
  while (ip < chunk.kod.size()) {
    const OpCode op = static_cast<OpCode>(chunk.kod[ip]);
    const std::size_t uzunluk = bytecodeKomutUzunlugu(chunk, ip);
    if (uzunluk == 0 || ip + uzunluk > chunk.kod.size()) {
      throw std::runtime_error("Bozuk bytecode: komut uzunlugu gecersiz.");
    }
    if (sayi > 0) {
      ss << ",";
    }
    ss << "{\"ip\":" << ip << ",\"op\":\"" << opCodeAdi(op)
       << "\",\"satir\":" << chunk.satirlar.at(ip);
    if (uzunluk == 3) {
      ss << ",\"operand\":" << bytecodeU16(chunk, ip + 1);
    }
    if (op == OpCode::OP_ISLEV_OLUSTUR) {
      ss << ",\"islev\":" << bytecodeIslevJson(chunk, ip);
    }
    ss << "}";
    ++sayi;
    ip += uzunluk;
  }
  ss << "]";
  if (komutSayisi != nullptr) {
    *komutSayisi = sayi;
  }
  return ss.str();
}

void astJsonBaslat(std::ostringstream &ss, const ASTNode *dugum,
                   const std::string &tur) {
  ss << "{\"tur\":\"" << tur << "\",\"satir\":" << dugum->satir();
}

std::string astDugumJson(const ASTNode *dugum) {
  if (dugum == nullptr) {
    return "null";
  }

  std::ostringstream ss;

  if (const auto *sayi = dynamic_cast<const SayiNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "Sayi");
    ss << ",\"deger\":\"" << jsonKacis(sayi->deger()) << "\"}";
    return ss.str();
  }
  if (const auto *metin = dynamic_cast<const MetinNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "Metin");
    ss << ",\"deger\":\"" << jsonKacis(metin->deger()) << "\"}";
    return ss.str();
  }
  if (const auto *mantik = dynamic_cast<const MantikNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "Mantik");
    ss << ",\"deger\":" << (mantik->deger() ? "true" : "false") << "}";
    return ss.str();
  }
  if (const auto *kimlik = dynamic_cast<const KimlikNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "Kimlik");
    ss << ",\"ad\":\"" << jsonKacis(kimlik->ad()) << "\"}";
    return ss.str();
  }
  if (const auto *tekli = dynamic_cast<const TekliIslemNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "TekliIslem");
    ss << ",\"op\":\"" << jsonKacis(tekli->op()) << "\",\"ifade\":"
       << astDugumJson(tekli->ifade()) << "}";
    return ss.str();
  }
  if (const auto *ikili = dynamic_cast<const IkiliIslemNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "IkiliIslem");
    ss << ",\"op\":\"" << jsonKacis(ikili->op()) << "\",\"sol\":"
       << astDugumJson(ikili->sol()) << ",\"sag\":"
       << astDugumJson(ikili->sag()) << "}";
    return ss.str();
  }
  if (const auto *sor = dynamic_cast<const SorNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "Sor");
    ss << ",\"ifade\":" << astDugumJson(sor->soruIfadesi()) << "}";
    return ss.str();
  }
  if (const auto *liste = dynamic_cast<const ListeNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "Liste");
    ss << ",\"ogeler\":" << astDugumDizisiJson(liste->ogeler()) << "}";
    return ss.str();
  }
  if (const auto *sozluk = dynamic_cast<const SozlukNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "Sozluk");
    ss << ",\"ogeler\":[";
    const auto &ogeler = sozluk->ogeler();
    for (std::size_t i = 0; i < ogeler.size(); ++i) {
      if (i > 0) {
        ss << ",";
      }
      ss << "{\"anahtar\":\"" << jsonKacis(ogeler[i].first)
         << "\",\"deger\":" << astDugumJson(ogeler[i].second.get()) << "}";
    }
    ss << "]}";
    return ss.str();
  }
  if (const auto *indeks = dynamic_cast<const IndeksErisimNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "IndeksErisim");
    ss << ",\"hedef\":" << astDugumJson(indeks->hedef())
       << ",\"indeks\":" << astDugumJson(indeks->indeks()) << "}";
    return ss.str();
  }
  if (const auto *dilim = dynamic_cast<const DilimErisimNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "DilimErisim");
    ss << ",\"hedef\":" << astDugumJson(dilim->hedef())
       << ",\"baslangic\":" << astDugumVeyaNullJson(dilim->baslangic())
       << ",\"bitis\":" << astDugumVeyaNullJson(dilim->bitis()) << "}";
    return ss.str();
  }
  if (const auto *alan = dynamic_cast<const AlanErisimNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "AlanErisim");
    ss << ",\"alan\":\"" << jsonKacis(alan->alanAdi())
       << "\",\"hedef\":" << astDugumJson(alan->hedef()) << "}";
    return ss.str();
  }
  if (const auto *alan =
          dynamic_cast<const GuvenliAlanErisimNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "GuvenliAlanErisim");
    ss << ",\"alan\":\"" << jsonKacis(alan->alanAdi())
       << "\",\"hedef\":" << astDugumJson(alan->hedef()) << "}";
    return ss.str();
  }
  if (const auto *benim = dynamic_cast<const BenimErisimNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "BenimErisim");
    ss << ",\"alan\":\"" << jsonKacis(benim->alanAdi()) << "\"}";
    return ss.str();
  }
  if (const auto *ust = dynamic_cast<const UstErisimNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "UstErisim");
    ss << ",\"metod\":\"" << jsonKacis(ust->metodAdi()) << "\"}";
    return ss.str();
  }
  if (const auto *cagri = dynamic_cast<const IslevCagriNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "IslevCagri");
    ss << ",\"ad\":\"" << jsonKacis(cagri->ad())
       << "\",\"argumanlar\":" << astDugumDizisiJson(cagri->argumanlar())
       << "}";
    return ss.str();
  }
  if (const auto *yeni = dynamic_cast<const YeniNesneNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "YeniNesne");
    ss << ",\"sinif\":\"" << jsonKacis(yeni->sinifAdi())
       << "\",\"argumanlar\":" << astDugumDizisiJson(yeni->argumanlar())
       << "}";
    return ss.str();
  }
  if (const auto *uretec = dynamic_cast<const ListeUretecNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "ListeUretec");
    ss << ",\"degisken\":\"" << jsonKacis(uretec->donguDegiskeni())
       << "\",\"ifade\":" << astDugumJson(uretec->ifade())
       << ",\"kaynak\":" << astDugumJson(uretec->kaynakListe())
       << ",\"kosul\":" << astDugumVeyaNullJson(uretec->kosul()) << "}";
    return ss.str();
  }
  if (const auto *blok = dynamic_cast<const BlockNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "Block");
    ss << ",\"komutlar\":" << astKomutlarJson(blok->komutlar()) << "}";
    return ss.str();
  }
  if (const auto *program = dynamic_cast<const ProgramNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "Program");
    ss << ",\"komutlar\":" << astKomutlarJson(program->komutlar()) << "}";
    return ss.str();
  }
  if (const auto *atama = dynamic_cast<const AtamaNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "Atama");
    ss << ",\"hedef\":" << astDugumJson(atama->hedef())
       << ",\"ifade\":" << astDugumJson(atama->ifade())
       << ",\"bildirim\":" << (atama->bildirimMi() ? "true" : "false")
       << "}";
    return ss.str();
  }
  if (const auto *atama = dynamic_cast<const CokluAtamaNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "CokluAtama");
    ss << ",\"hedefler\":" << metinDizisiJson(atama->hedefler())
       << ",\"ifade\":" << astDugumJson(atama->ifade())
       << ",\"bildirim\":" << (atama->bildirimMi() ? "true" : "false")
       << "}";
    return ss.str();
  }
  if (const auto *yazdir = dynamic_cast<const YazdirNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "Yazdir");
    ss << ",\"ifade\":" << astDugumJson(yazdir->ifade()) << "}";
    return ss.str();
  }
  if (const auto *eger = dynamic_cast<const EgerNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "Eger");
    ss << ",\"kosul\":" << astDugumJson(eger->kosul())
       << ",\"dogru_blok\":" << astDugumJson(eger->dogruBlok())
       << ",\"yanlis_blok\":" << astDugumVeyaNullJson(eger->yanlisBlok())
       << "}";
    return ss.str();
  }
  if (const auto *tekrarla = dynamic_cast<const TekrarlaNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "Tekrarla");
    ss << ",\"kac_kez\":" << astDugumJson(tekrarla->kacKezIfadesi())
       << ",\"govde\":" << astDugumJson(tekrarla->govde()) << "}";
    return ss.str();
  }
  if (const auto *surece = dynamic_cast<const SureceNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "Surece");
    ss << ",\"kosul\":" << astDugumJson(surece->kosul())
       << ",\"govde\":" << astDugumJson(surece->govde()) << "}";
    return ss.str();
  }
  if (const auto *islev = dynamic_cast<const IslevTanimNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "IslevTanim");
    ss << ",\"ad\":\"" << jsonKacis(islev->ad())
       << "\",\"parametreler\":" << metinDizisiJson(islev->parametreler())
       << ",\"varsayilanlar\":"
       << astDugumDizisiJson(islev->varsayilanlar())
       << ",\"govde\":" << astDugumJson(islev->govde()) << "}";
    return ss.str();
  }
  if (const auto *islev = dynamic_cast<const IsimsizIslevNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "IsimsizIslev");
    ss << ",\"parametreler\":" << metinDizisiJson(islev->parametreler())
       << ",\"varsayilanlar\":"
       << astDugumDizisiJson(islev->varsayilanlar())
       << ",\"govde\":" << astDugumJson(islev->govde()) << "}";
    return ss.str();
  }
  if (const auto *paralel = dynamic_cast<const ParalelYapNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "ParalelYap");
    ss << ",\"govde\":" << astDugumJson(paralel->govde()) << "}";
    return ss.str();
  }
  if (const auto *dis = dynamic_cast<const DisIslevTanimNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "DisIslevTanim");
    ss << ",\"ad\":\"" << jsonKacis(dis->ad())
       << "\",\"kutuphane\":\"" << jsonKacis(dis->kutuphaneYolu())
       << "\",\"parametre_adlari\":"
       << metinDizisiJson(dis->parametreAdlari())
       << ",\"parametre_tipleri\":" << metinDizisiJson(dis->parametreTipleri())
       << ",\"donus_tipi\":\"" << jsonKacis(dis->donusTipi()) << "\"}";
    return ss.str();
  }
  if (const auto *dondur = dynamic_cast<const DondurNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "Dondur");
    ss << ",\"ifade\":" << astDugumJson(dondur->ifade()) << "}";
    return ss.str();
  }
  if (const auto *dahil = dynamic_cast<const DahilEtNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "DahilEt");
    ss << ",\"dosya\":\"" << jsonKacis(dahil->dosyaAdi()) << "\"}";
    return ss.str();
  }
  if (const auto *sinif = dynamic_cast<const SinifTanimNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "SinifTanim");
    ss << ",\"ad\":\"" << jsonKacis(sinif->ad())
       << "\",\"ebeveyn\":\"" << jsonKacis(sinif->ebeveynAdi())
       << "\",\"govde\":" << astDugumJson(sinif->govde()) << "}";
    return ss.str();
  }
  if (const auto *deneme = dynamic_cast<const DenemeYakalaNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "DenemeYakala");
    ss << ",\"deneme\":" << astDugumJson(deneme->denemeBlogu())
       << ",\"hata_degiskeni\":\"" << jsonKacis(deneme->hataDegiskeni())
       << "\",\"yakala\":" << astDugumJson(deneme->yakalaBlogu()) << "}";
    return ss.str();
  }
  if (dynamic_cast<const KirNode *>(dugum) != nullptr) {
    astJsonBaslat(ss, dugum, "Kir");
    ss << "}";
    return ss.str();
  }
  if (dynamic_cast<const DevamNode *>(dugum) != nullptr) {
    astJsonBaslat(ss, dugum, "Devam");
    ss << "}";
    return ss.str();
  }
  if (const auto *ifade = dynamic_cast<const IfadeKomutNode *>(dugum)) {
    astJsonBaslat(ss, dugum, "IfadeKomut");
    ss << ",\"ifade\":" << astDugumJson(ifade->ifade()) << "}";
    return ss.str();
  }

  astJsonBaslat(ss, dugum, "Bilinmeyen");
  ss << "}";
  return ss.str();
}

int komutLex(const std::string &dosyaYolu, bool jsonCikti) {
  if (dosyaYolu.size() < 3 || dosyaYolu.substr(dosyaYolu.size() - 3) != ".oh") {
    throw std::runtime_error("Hata: lex komutu icin .oh dosyasi bekleniyor.");
  }

  const std::string kaynakKod = dosyaOku(dosyaYolu);
  Lexer lexer(kaynakKod);
  const std::vector<OrhunToken> tokenlar = lexer.tokenize();
  std::size_t hataSayisi = 0;
  for (const auto &token : tokenlar) {
    if (token.tur == TokenTuru::HATA) {
      ++hataSayisi;
    }
  }

  if (jsonCikti) {
    std::cout << "{"
              << "\"dosya\":\"" << jsonKacis(dosyaYolu) << "\","
              << "\"hata_sayisi\":" << hataSayisi << ","
              << "\"tokenlar\":" << tokenlarJson(tokenlar) << "}\n";
    return hataSayisi == 0 ? 0 : 1;
  }

  for (const auto &token : tokenlar) {
    std::cout << token.satir << ":" << token.sutun << " "
              << tokenTuruAdi(token.tur);
    if (!token.deger.empty()) {
      std::cout << " " << tokeniYaziyaCevir(token);
    }
    std::cout << "\n";
  }
  return hataSayisi == 0 ? 0 : 1;
}

int komutParse(const std::string &dosyaYolu, bool jsonCikti) {
  if (dosyaYolu.size() < 3 || dosyaYolu.substr(dosyaYolu.size() - 3) != ".oh") {
    throw std::runtime_error("Hata: parse komutu icin .oh dosyasi bekleniyor.");
  }

  const std::string kaynakKod = dosyaOku(dosyaYolu);
  try {
    std::unique_ptr<ProgramNode> program = parseEt(kaynakKod);
    if (jsonCikti) {
      std::cout << "{"
                << "\"dosya\":\"" << jsonKacis(dosyaYolu) << "\","
                << "\"durum\":\"ok\","
                << "\"hata_sayisi\":0,"
                << "\"ast\":" << astDugumJson(program.get()) << "}\n";
    } else {
      program->yazdir_agac(std::cout);
    }
    return 0;
  } catch (const std::exception &ex) {
    if (!jsonCikti) {
      throw;
    }
    std::cout << "{"
              << "\"dosya\":\"" << jsonKacis(dosyaYolu) << "\","
              << "\"durum\":\"fail\","
              << "\"hata_sayisi\":1,"
              << "\"hata\":{\"mesaj\":\"" << jsonKacis(ex.what())
              << "\"},"
              << "\"ast\":null"
              << "}\n";
    return 1;
  }
}

int komutBytecode(const std::string &dosyaYolu, bool jsonCikti) {
  if (dosyaYolu.size() < 3 || dosyaYolu.substr(dosyaYolu.size() - 3) != ".oh") {
    throw std::runtime_error("Hata: baytkod komutu icin .oh dosyasi bekleniyor.");
  }

  try {
    const BytecodeChunk chunk = bytecodeDerle(dosyaOku(dosyaYolu));
    std::size_t komutSayisi = 0;
    const std::string komutlar = bytecodeKomutlarJson(chunk, &komutSayisi);
    if (jsonCikti) {
      std::cout << "{"
                << "\"dosya\":\"" << jsonKacis(dosyaYolu) << "\","
                << "\"durum\":\"ok\","
                << "\"hata_sayisi\":0,"
                << "\"bytecode\":{\"kod_boyutu\":" << chunk.kod.size() << ","
                << "\"komut_sayisi\":" << komutSayisi << ","
                << "\"sabit_sayisi\":" << chunk.sabitler.size() << ","
                << "\"komutlar\":" << komutlar << ","
                << "\"sabitler\":" << bytecodeSabitlerJson(chunk) << "}}\n";
    } else {
      std::cout << "Bytecode komut sayisi: " << komutSayisi << "\n";
      std::cout << "Sabit sayisi: " << chunk.sabitler.size() << "\n";
      std::cout << komutlar << "\n";
    }
    return 0;
  } catch (const std::exception &ex) {
    if (!jsonCikti) {
      throw;
    }
    std::cout << "{"
              << "\"dosya\":\"" << jsonKacis(dosyaYolu) << "\","
              << "\"durum\":\"fail\","
              << "\"hata_sayisi\":1,"
              << "\"hata\":{\"mesaj\":\"" << jsonKacis(ex.what()) << "\"},"
              << "\"bytecode\":null"
              << "}\n";
    return 1;
  }
}

int komutFmt(const std::string &dosyaYolu, bool checkModu, bool jsonCikti) {
  if (dosyaYolu.size() < 3 || dosyaYolu.substr(dosyaYolu.size() - 3) != ".oh") {
    throw std::runtime_error("Hata: fmt komutu icin .oh dosyasi bekleniyor.");
  }
  const std::string kaynakKod = dosyaOku(dosyaYolu);
  Lexer lexer(kaynakKod);
  const std::vector<OrhunToken> tokenlar = lexer.tokenize();
  const std::string yeniIcerik = bicimlendir(tokenlar);
  const bool degisti = kaynakKod != yeniIcerik;

  if (!checkModu && degisti) {
    dosyaYaz(dosyaYolu, yeniIcerik);
  }

  if (jsonCikti) {
    std::cout << "{"
              << "\"dosya\":\"" << jsonKacis(dosyaYolu) << "\","
              << "\"mod\":\"" << (checkModu ? "check" : "write") << "\","
              << "\"degisti\":" << (degisti ? "true" : "false") << ","
              << "\"durum\":\""
              << (checkModu ? (degisti ? "needs_format" : "ok")
                            : (degisti ? "formatted" : "unchanged"))
              << "\""
              << "}\n";
    return (checkModu && degisti) ? 1 : 0;
  }

  if (checkModu) {
    if (degisti) {
      std::cout << "Bicim farki var: " << dosyaYolu << "\n";
      return 1;
    }
    std::cout << "Bicim uygun: " << dosyaYolu << "\n";
    return 0;
  }

  if (degisti) {
    std::cout << "Bicimlendirildi: " << dosyaYolu << "\n";
  } else {
    std::cout << "Zaten bicimli: " << dosyaYolu << "\n";
  }
  return 0;
}

struct LintMesaji {
  std::size_t satir = 0;
  std::string seviye;
  std::string mesaj;
};

std::string lintMesajlariJson(const std::vector<LintMesaji> &mesajlar) {
  std::ostringstream ss;
  ss << "[";
  for (std::size_t i = 0; i < mesajlar.size(); ++i) {
    if (i > 0) {
      ss << ",";
    }
    ss << "{\"satir\":" << mesajlar[i].satir << ",\"seviye\":\""
       << jsonKacis(mesajlar[i].seviye) << "\",\"mesaj\":\""
       << jsonKacis(mesajlar[i].mesaj) << "\"}";
  }
  ss << "]";
  return ss.str();
}

std::vector<std::string> satirlaraBol(const std::string &icerik) {
  std::vector<std::string> satirlar;
  std::string aktif;
  for (char c : icerik) {
    if (c == '\n') {
      satirlar.push_back(aktif);
      aktif.clear();
      continue;
    }
    if (c != '\r') {
      aktif.push_back(c);
    }
  }
  if (!aktif.empty() || icerik.empty() || icerik.back() != '\n') {
    satirlar.push_back(aktif);
  }
  return satirlar;
}

void lintMesajiEkle(std::vector<LintMesaji> &mesajlar, std::size_t satir,
                    const std::string &seviye, const std::string &mesaj) {
  mesajlar.push_back({satir, seviye, mesaj});
}

std::vector<LintMesaji> lintCalistir(const std::string &kaynakKod) {
  std::vector<LintMesaji> mesajlar;
  const std::vector<std::string> satirlar = satirlaraBol(kaynakKod);
  std::size_t bosSeri = 0;

  for (std::size_t i = 0; i < satirlar.size(); ++i) {
    const std::string &satir = satirlar[i];
    const std::size_t satirNo = i + 1;

    if (satir.find('\t') != std::string::npos) {
      lintMesajiEkle(mesajlar, satirNo, "uyari",
                     "Tab karakteri tespit edildi; 4 bosluk kullanin.");
    }
    if (!satir.empty() && (satir.back() == ' ' || satir.back() == '\t')) {
      lintMesajiEkle(mesajlar, satirNo, "uyari",
                     "Satir sonunda gereksiz bosluk var.");
    }
    if (satir.size() > 140) {
      lintMesajiEkle(
          mesajlar, satirNo, "uyari",
          "Satir uzunlugu 140 karakteri asiyor (okunabilirlik dusuyor).");
    }

    std::size_t bosluk = 0;
    while (bosluk < satir.size() && satir[bosluk] == ' ') {
      ++bosluk;
    }
    const bool bosSatir = bosluk == satir.size();
    if (!bosSatir && (bosluk % 4) != 0) {
      lintMesajiEkle(mesajlar, satirNo, "uyari",
                     "Girinti 4'un kati degil; blok hizalamasi bozulabilir.");
    }

    if (bosSatir) {
      ++bosSeri;
      if (bosSeri > 2) {
        lintMesajiEkle(mesajlar, satirNo, "uyari",
                       "Ardisik cok fazla bos satir var.");
      }
    } else {
      bosSeri = 0;
    }
  }

  try {
    static_cast<void>(parseEt(kaynakKod));
  } catch (const std::exception &ex) {
    lintMesajiEkle(mesajlar, 0, "hata",
                   std::string("Parser hatasi: ") + ex.what());
  }

  return mesajlar;
}

int komutLint(const std::string &dosyaYolu, bool strict, bool jsonCikti) {
  if (dosyaYolu.size() < 3 || dosyaYolu.substr(dosyaYolu.size() - 3) != ".oh") {
    throw std::runtime_error("Hata: lint komutu icin .oh dosyasi bekleniyor.");
  }

  const std::string kaynakKod = dosyaOku(dosyaYolu);
  const std::vector<LintMesaji> mesajlar = lintCalistir(kaynakKod);
  std::size_t hataSayisi = 0;
  std::size_t uyariSayisi = 0;

  for (const auto &mesaj : mesajlar) {
    if (mesaj.seviye == "hata") {
      ++hataSayisi;
      if (!jsonCikti) {
        std::cout << "[HATA] " << mesaj.mesaj << "\n";
      }
      continue;
    }
    ++uyariSayisi;
    if (!jsonCikti) {
      std::cout << "[UYARI] Satir " << mesaj.satir << ": " << mesaj.mesaj
                << "\n";
    }
  }

  const bool basarisiz = hataSayisi > 0 || (strict && uyariSayisi > 0);
  if (jsonCikti) {
    std::cout << "{"
              << "\"dosya\":\"" << jsonKacis(dosyaYolu) << "\","
              << "\"strict\":" << (strict ? "true" : "false") << ","
              << "\"hata_sayisi\":" << hataSayisi << ","
              << "\"uyari_sayisi\":" << uyariSayisi << ","
              << "\"durum\":\"" << (basarisiz ? "fail" : "ok") << "\","
              << "\"mesajlar\":" << lintMesajlariJson(mesajlar) << "}\n";
    return basarisiz ? 1 : 0;
  }

  std::cout << "Lint ozeti: " << hataSayisi << " hata, " << uyariSayisi
            << " uyari.\n";
  if (basarisiz) {
    return 1;
  }
  return 0;
}

struct DepoKaydi {
  std::string ad;
  std::string kaynak;
  std::string aciklama;
};

std::filesystem::path varsayilanDepoIndexYolu() {
  namespace fs = std::filesystem;
  if (const char *env = std::getenv("ORHUN_DEPO_INDEX")) {
    if (*env != '\0') {
      return fs::path(env);
    }
  }

  const fs::path yol = fs::current_path() / "orhun_depo" / "index.txt";
  return yol;
}

std::string solaSagaKirp(const std::string &metin) {
  std::size_t bas = 0;
  while (bas < metin.size() && (metin[bas] == ' ' || metin[bas] == '\t' ||
                                metin[bas] == '\r' || metin[bas] == '\n')) {
    ++bas;
  }

  std::size_t son = metin.size();
  while (son > bas && (metin[son - 1] == ' ' || metin[son - 1] == '\t' ||
                       metin[son - 1] == '\r' || metin[son - 1] == '\n')) {
    --son;
  }

  return metin.substr(bas, son - bas);
}

std::string asciiKucuk(const std::string &metin) {
  std::string sonuc = metin;
  std::transform(
      sonuc.begin(), sonuc.end(), sonuc.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return sonuc;
}

std::vector<DepoKaydi> depoIndexOku(const std::filesystem::path &indexYolu) {
  std::vector<DepoKaydi> kayitlar;
  if (!std::filesystem::exists(indexYolu)) {
    return kayitlar;
  }

  std::istringstream akis(dosyaOku(indexYolu.string()));
  std::string satir;
  while (std::getline(akis, satir)) {
    const std::string temiz = solaSagaKirp(satir);
    if (temiz.empty() || temiz[0] == '#') {
      continue;
    }

    const std::size_t p1 = temiz.find('|');
    if (p1 == std::string::npos) {
      continue;
    }
    const std::size_t p2 = temiz.find('|', p1 + 1);
    if (p2 == std::string::npos) {
      continue;
    }

    DepoKaydi kayit;
    kayit.ad = solaSagaKirp(temiz.substr(0, p1));
    kayit.kaynak = solaSagaKirp(temiz.substr(p1 + 1, p2 - (p1 + 1)));
    kayit.aciklama = solaSagaKirp(temiz.substr(p2 + 1));
    if (!kayit.ad.empty() && !kayit.kaynak.empty()) {
      kayitlar.push_back(std::move(kayit));
    }
  }

  return kayitlar;
}

int komutPaketDepoBaslat(const std::string &klasor) {
  namespace fs = std::filesystem;
  const fs::path kok =
      klasor.empty() ? (fs::current_path() / "orhun_depo") : fs::path(klasor);
  fs::create_directories(kok / "paketler");

  const fs::path index = kok / "index.txt";
  if (!fs::exists(index)) {
    const std::string ornek =
        "# ad | kaynak | aciklama\n"
        "# ornek_paket | https://github.com/ornek/orhun-ornek.git | ornek "
        "aciklama\n";
    dosyaYaz(index.string(), ornek);
  }

  const fs::path readme = kok / "README.md";
  if (!fs::exists(readme)) {
    dosyaYaz(readme.string(),
             "# Orhun Paket Deposu\n\n"
             "- Paket ekle: `orhun paket depo-ekle <ad> <kaynak> [aciklama]`\n"
             "- Paket ara: `orhun paket ara <kelime>`\n"
             "- Paket kur: `orhun paket kur depo:<ad>`\n");
  }

  std::cout << "Depo baslatildi: " << kok.string() << "\n";
  std::cout << "Index: " << index.string() << "\n";
  return 0;
}

int komutPaketDepoEkle(const std::string &ad, const std::string &kaynak,
                       const std::string &aciklama) {
  if (ad.empty() || kaynak.empty()) {
    throw std::runtime_error(
        "Hata: paket depo-ekle <ad> <kaynak> [aciklama] kullanin.");
  }

  const std::filesystem::path indexYolu = varsayilanDepoIndexYolu();
  std::filesystem::create_directories(indexYolu.parent_path());
  auto kayitlar = depoIndexOku(indexYolu);
  for (const auto &kayit : kayitlar) {
    if (kayit.ad == ad) {
      throw std::runtime_error("Hata: '" + ad + "' kaydi zaten depoda mevcut.");
    }
  }

  std::ofstream dosya(indexYolu, std::ios::app | std::ios::binary);
  if (!dosya.is_open()) {
    throw std::runtime_error("Hata: depo index dosyasina yazilamadi: " +
                             indexYolu.string());
  }
  dosya << ad << " | " << kaynak << " | " << aciklama << "\n";

  std::cout << "Depoya eklendi: " << ad << " -> " << kaynak << "\n";
  return 0;
}

int komutPaketAra(const std::string &sorgu) {
  if (sorgu.empty()) {
    throw std::runtime_error("Hata: paket ara <kelime> kullanin.");
  }

  const std::filesystem::path indexYolu = varsayilanDepoIndexYolu();
  const auto kayitlar = depoIndexOku(indexYolu);
  if (kayitlar.empty()) {
    std::cout << "Depo kaydi bulunamadi: " << indexYolu.string() << "\n";
    return 0;
  }

  const std::string needle = asciiKucuk(sorgu);
  std::size_t sayi = 0;
  for (const auto &kayit : kayitlar) {
    const std::string havuz =
        asciiKucuk(kayit.ad + " " + kayit.kaynak + " " + kayit.aciklama);
    if (havuz.find(needle) == std::string::npos) {
      continue;
    }
    ++sayi;
    std::cout << "- " << kayit.ad << " -> " << kayit.kaynak;
    if (!kayit.aciklama.empty()) {
      std::cout << " | " << kayit.aciklama;
    }
    std::cout << "\n";
  }

  if (sayi == 0) {
    std::cout << "Sonuc bulunamadi.\n";
  } else {
    std::cout << "Toplam " << sayi << " paket bulundu.\n";
  }
  return 0;
}

std::optional<std::string> depodanKaynakCoz(const std::string &referans) {
  if (referans.rfind("depo:", 0) != 0) {
    return std::nullopt;
  }

  const std::string paketAdi = referans.substr(5);
  if (paketAdi.empty()) {
    return std::nullopt;
  }

  const auto kayitlar = depoIndexOku(varsayilanDepoIndexYolu());
  for (const auto &kayit : kayitlar) {
    if (kayit.ad == paketAdi) {
      return kayit.kaynak;
    }
  }
  return std::nullopt;
}

int komutPaketYeni(const std::string &projeAdi) {
  namespace fs = std::filesystem;
  if (projeAdi.empty()) {
    throw std::runtime_error("Hata: Proje adi bos olamaz.");
  }

  const fs::path kok = fs::current_path() / projeAdi;
  if (fs::exists(kok)) {
    throw std::runtime_error("Hata: '" + kok.string() + "' zaten mevcut.");
  }

  fs::create_directories(kok / "lib");

  const std::string anaDosya = "# " + projeAdi +
                               "\n"
                               "yaz \"Merhaba Orhun!\"\n";
  dosyaYaz((kok / "main.oh").string(), anaDosya);

  const std::string yapilandirma = "ad: \"" + projeAdi +
                                   "\"\n"
                                   "surum: \"0.1.0\"\n"
                                   "ana_dosya: \"main.oh\"\n";
  dosyaYaz((kok / "orhun.yap").string(), yapilandirma);

  std::cout << "Paket iskeleti olusturuldu: " << kok.string() << "\n";
  return 0;
}

std::string paketAdiCikar(const std::string &kaynak) {
  if (kaynak.empty()) {
    return "paket";
  }

  std::string temiz = kaynak;
  while (!temiz.empty() && (temiz.back() == '/' || temiz.back() == '\\')) {
    temiz.pop_back();
  }
  const std::size_t slash = temiz.find_last_of("/\\");
  std::string ad = slash == std::string::npos ? temiz : temiz.substr(slash + 1);
  if (ad.size() > 4 && ad.substr(ad.size() - 4) == ".git") {
    ad = ad.substr(0, ad.size() - 4);
  }
  if (ad.empty()) {
    return "paket";
  }
  return ad;
}

bool uzakKaynakMi(const std::string &kaynak) {
  return kaynak.rfind("http://", 0) == 0 || kaynak.rfind("https://", 0) == 0 ||
         kaynak.rfind("git@", 0) == 0 || kaynak.rfind("ssh://", 0) == 0;
}

bool paketAdiGecerliMi(const std::string &ad) {
  if (ad.empty()) {
    return false;
  }
  for (char c : ad) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (!(std::isalnum(uc) || c == '_' || c == '-' || c == '.')) {
      return false;
    }
  }
  return true;
}

bool uzakKaynakGuvenliMi(const std::string &kaynak) {
  if (!uzakKaynakMi(kaynak)) {
    return false;
  }
  for (char c : kaynak) {
    if (c == '"' || c == '\'' || c == '`' || c == '\n' || c == '\r' ||
        c == '\t' || c == '&' || c == '|' || c == ';' || c == '<' || c == '>' ||
        c == '$') {
      return false;
    }
  }
  return true;
}

std::optional<std::string> uzakKaynakHostBul(const std::string &kaynak) {
  if (kaynak.rfind("http://", 0) == 0 || kaynak.rfind("https://", 0) == 0 ||
      kaynak.rfind("ssh://", 0) == 0) {
    const std::size_t scheme = kaynak.find("://");
    if (scheme == std::string::npos) {
      return std::nullopt;
    }
    std::size_t bas = scheme + 3;
    const std::size_t atPos = kaynak.find('@', bas);
    if (atPos != std::string::npos && atPos < kaynak.find_first_of(":/", bas)) {
      bas = atPos + 1;
    }
    const std::size_t son = kaynak.find_first_of(":/", bas);
    const std::string host = son == std::string::npos
                                 ? kaynak.substr(bas)
                                 : kaynak.substr(bas, son - bas);
    if (!host.empty()) {
      return asciiKucuk(host);
    }
    return std::nullopt;
  }
  if (kaynak.rfind("git@", 0) == 0) {
    const std::size_t bas = 4;
    const std::size_t son = kaynak.find(':', bas);
    if (son == std::string::npos || son <= bas) {
      return std::nullopt;
    }
    return asciiKucuk(kaynak.substr(bas, son - bas));
  }
  return std::nullopt;
}

bool paketKaynakAllowlistteMi(const std::string &kaynak) {
  const auto host = uzakKaynakHostBul(kaynak);
  if (!host.has_value()) {
    return false;
  }
  std::unordered_set<std::string> izinliler = {"github.com", "gitlab.com",
                                               "bitbucket.org"};
  if (const char *env = std::getenv("ORHUN_PAKET_ALLOWLIST")) {
    std::istringstream ak(env);
    for (std::string parca; std::getline(ak, parca, ',');) {
      const std::string trim = asciiKucuk(solaSagaKirp(parca));
      if (!trim.empty()) {
        izinliler.insert(trim);
      }
    }
  }
  if (izinliler.find(*host) != izinliler.end()) {
    return true;
  }
  for (const std::string &alan : izinliler) {
    if (host->size() > alan.size() &&
        host->compare(host->size() - alan.size(), alan.size(), alan) == 0 &&
        (*host)[host->size() - alan.size() - 1] == '.') {
      return true;
    }
  }
  return false;
}

int processCalistir(const std::vector<std::string> &argumanlar) {
  if (argumanlar.empty()) {
    throw std::runtime_error("Hata: processCalistir icin komut verilmedi.");
  }
#ifdef _WIN32
  std::vector<char *> ham;
  ham.reserve(argumanlar.size() + 1);
  for (const std::string &s : argumanlar) {
    ham.push_back(const_cast<char *>(s.c_str()));
  }
  ham.push_back(nullptr);
  const int kod = _spawnvp(_P_WAIT, argumanlar[0].c_str(), ham.data());
  if (kod == -1) {
    throw std::runtime_error("Hata: '" + argumanlar[0] +
                             "' calistirilamadi: " + std::strerror(errno));
  }
  return kod;
#else
  std::vector<char *> ham;
  ham.reserve(argumanlar.size() + 1);
  for (const std::string &s : argumanlar) {
    ham.push_back(const_cast<char *>(s.c_str()));
  }
  ham.push_back(nullptr);

  const pid_t pid = fork();
  if (pid < 0) {
    throw std::runtime_error("Hata: process fork basarisiz.");
  }
  if (pid == 0) {
    execvp(ham[0], ham.data());
    _exit(127);
  }
  int durum = 0;
  if (waitpid(pid, &durum, 0) < 0) {
    throw std::runtime_error("Hata: process wait basarisiz.");
  }
  if (WIFEXITED(durum)) {
    return WEXITSTATUS(durum);
  }
  return 1;
#endif
}

int gitRefCheckout(const std::filesystem::path &hedefYol,
                   const std::string &kaynakRef) {
  if (kaynakRef.empty()) {
    return 0;
  }
  return processCalistir({"git", "-C", hedefYol.string(), "checkout",
                          "--detach", kaynakRef});
}

bool programYoldaVarMi(const std::string &ad) {
  const char *envPath = std::getenv("PATH");
  if (envPath == nullptr || *envPath == '\0') {
    return false;
  }
  const std::string pathDegeri(envPath);
#ifdef _WIN32
  const char ayrac = ';';
  const std::vector<std::string> uzantilar = {".exe", ".cmd", ".bat", ""};
#else
  const char ayrac = ':';
  const std::vector<std::string> uzantilar = {""};
#endif
  std::size_t bas = 0;
  while (bas <= pathDegeri.size()) {
    const std::size_t son = pathDegeri.find(ayrac, bas);
    const std::string parca = son == std::string::npos
                                  ? pathDegeri.substr(bas)
                                  : pathDegeri.substr(bas, son - bas);
    if (!parca.empty()) {
      const std::filesystem::path kok(parca);
      for (const std::string &uzanti : uzantilar) {
        const std::filesystem::path aday = kok / (ad + uzanti);
        std::error_code ec;
        if (std::filesystem::exists(aday, ec) && !ec &&
            std::filesystem::is_regular_file(aday, ec)) {
          return true;
        }
      }
    }
    if (son == std::string::npos) {
      break;
    }
    bas = son + 1;
  }
  return false;
}

std::string crc32Hex(const std::string &metin) {
  std::vector<std::uint8_t> veri(metin.begin(), metin.end());
  std::ostringstream ss;
  ss << std::hex << std::setfill('0') << std::setw(8) << crc32Hesapla(veri);
  return ss.str();
}

struct LockKaydi {
  std::string ad;
  std::string kaynak;
  std::string ozet;
  std::string surum;
  std::string commitPin;
  std::string icerikHash;
  std::string kaynakRef;
};

std::vector<std::string> boruIleBol(const std::string &metin) {
  std::vector<std::string> parcalar;
  std::string biriken;
  for (char c : metin) {
    if (c == '|') {
      parcalar.push_back(biriken);
      biriken.clear();
    } else {
      biriken.push_back(c);
    }
  }
  parcalar.push_back(biriken);
  return parcalar;
}

bool hexCommitDegeriMi(const std::string &metin) {
  if (metin.size() < 7 || metin.size() > 64) {
    return false;
  }
  return std::all_of(metin.begin(), metin.end(), [](unsigned char c) {
    return std::isxdigit(c) != 0;
  });
}

bool kaynakRefGecerliMi(const std::string &metin) {
  if (metin.empty()) {
    return true;
  }
  return std::all_of(metin.begin(), metin.end(), [](unsigned char c) {
    return !std::isspace(c) && c != '|';
  });
}

std::string lockKaydiOzetIcerigi(const LockKaydi &kayit) {
  if (kayit.surum == "v3") {
    std::string ozet = kayit.kaynak + "|" + kayit.ad + "|" + kayit.surum +
                       "|" + kayit.commitPin + "|" + kayit.icerikHash;
    if (!kayit.kaynakRef.empty()) {
      ozet += "|" + kayit.kaynakRef;
    }
    return ozet;
  }
  return kayit.kaynak + "|" + kayit.ad + "|" + kayit.surum;
}

std::string lockKaydiSha256(const LockKaydi &kayit) {
  return security::sha256Hex(lockKaydiOzetIcerigi(kayit));
}

std::optional<std::filesystem::path>
gitDizininiBul(const std::filesystem::path &paketKlasoru) {
  namespace fs = std::filesystem;
  const fs::path aday = paketKlasoru / ".git";
  std::error_code ec;
  if (fs::is_directory(aday, ec) && !ec) {
    return aday;
  }
  if (!fs::is_regular_file(aday, ec) || ec) {
    return std::nullopt;
  }
  std::istringstream ak(dosyaOku(aday.string()));
  std::string satir;
  if (!std::getline(ak, satir)) {
    return std::nullopt;
  }
  const std::string trim = solaSagaKirp(satir);
  if (trim.rfind("gitdir:", 0) != 0) {
    return std::nullopt;
  }
  std::string yol = solaSagaKirp(trim.substr(7));
  if (yol.empty()) {
    return std::nullopt;
  }
  fs::path gitYolu(yol);
  if (gitYolu.is_relative()) {
    gitYolu = paketKlasoru / gitYolu;
  }
  if (fs::exists(gitYolu, ec) && !ec) {
    return fs::weakly_canonical(gitYolu, ec);
  }
  return std::nullopt;
}

std::optional<std::string> gitRefCommitiniBul(
    const std::filesystem::path &gitDizini, const std::string &refAdi) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path refYolu = gitDizini / fs::path(refAdi);
  if (fs::exists(refYolu, ec) && !ec) {
    const std::string commit = solaSagaKirp(dosyaOku(refYolu.string()));
    if (hexCommitDegeriMi(commit)) {
      return asciiKucuk(commit);
    }
  }

  const fs::path packedRefs = gitDizini / "packed-refs";
  if (!fs::exists(packedRefs, ec) || ec) {
    return std::nullopt;
  }
  std::istringstream ak(dosyaOku(packedRefs.string()));
  for (std::string satir; std::getline(ak, satir);) {
    const std::string trim = solaSagaKirp(satir);
    if (trim.empty() || trim[0] == '#' || trim[0] == '^') {
      continue;
    }
    const std::size_t bosluk = trim.find(' ');
    if (bosluk == std::string::npos) {
      continue;
    }
    const std::string commit = trim.substr(0, bosluk);
    const std::string ref = solaSagaKirp(trim.substr(bosluk + 1));
    if (ref == refAdi && hexCommitDegeriMi(commit)) {
      return asciiKucuk(commit);
    }
  }
  return std::nullopt;
}

std::optional<std::string> gitCommitPinBul(
    const std::filesystem::path &paketKlasoru) {
  auto gitDizini = gitDizininiBul(paketKlasoru);
  if (!gitDizini.has_value()) {
    return std::nullopt;
  }
  const std::filesystem::path headDosyasi = *gitDizini / "HEAD";
  std::error_code ec;
  if (!std::filesystem::exists(headDosyasi, ec) || ec) {
    return std::nullopt;
  }
  const std::string head = solaSagaKirp(dosyaOku(headDosyasi.string()));
  if (head.rfind("ref:", 0) == 0) {
    const std::string ref = solaSagaKirp(head.substr(4));
    if (ref.empty()) {
      return std::nullopt;
    }
    return gitRefCommitiniBul(*gitDizini, ref);
  }
  if (hexCommitDegeriMi(head)) {
    return asciiKucuk(head);
  }
  return std::nullopt;
}

std::optional<std::string> gitKaynakRefCommitBul(
    const std::filesystem::path &paketKlasoru, const std::string &kaynakRef) {
  if (kaynakRef.empty()) {
    return std::nullopt;
  }
  if (hexCommitDegeriMi(kaynakRef)) {
    return asciiKucuk(kaynakRef);
  }
  auto gitDizini = gitDizininiBul(paketKlasoru);
  if (!gitDizini.has_value()) {
    return std::nullopt;
  }
  std::vector<std::string> adayRefler;
  if (kaynakRef.rfind("refs/", 0) == 0) {
    adayRefler.push_back(kaynakRef);
  } else {
    adayRefler.push_back("refs/heads/" + kaynakRef);
    adayRefler.push_back("refs/tags/" + kaynakRef);
    adayRefler.push_back("refs/remotes/origin/" + kaynakRef);
  }
  for (const auto &aday : adayRefler) {
    auto commit = gitRefCommitiniBul(*gitDizini, aday);
    if (commit.has_value()) {
      return commit;
    }
  }
  return std::nullopt;
}

std::string klasorIcerikSha256(const std::filesystem::path &klasor) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::exists(klasor, ec) || ec || !fs::is_directory(klasor, ec)) {
    throw std::runtime_error("Hata: lock icerik hash icin klasor bulunamadi: " +
                             klasor.string());
  }

  std::vector<fs::path> dosyalar;
  fs::recursive_directory_iterator son;
  for (fs::recursive_directory_iterator it(
           klasor, fs::directory_options::skip_permission_denied);
       it != son; ++it) {
    const fs::path yol = it->path();
    if (it->is_directory(ec)) {
      if (!ec && yol.filename() == ".git") {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (!it->is_regular_file(ec) || ec) {
      continue;
    }
    dosyalar.push_back(yol);
  }

  std::sort(dosyalar.begin(), dosyalar.end(),
            [&](const fs::path &sol, const fs::path &sag) {
              std::error_code relEc1;
              std::error_code relEc2;
              const std::string solRel =
                  fs::relative(sol, klasor, relEc1).generic_string();
              const std::string sagRel =
                  fs::relative(sag, klasor, relEc2).generic_string();
              return solRel < sagRel;
            });

  std::ostringstream birikim;
  for (const auto &dosya : dosyalar) {
    std::error_code relEc;
    const std::string goreli =
        fs::relative(dosya, klasor, relEc).generic_string();
    if (relEc) {
      continue;
    }
    const std::vector<std::uint8_t> ham = dosyaOkuIkili(dosya.string());
    const std::string hamMetin(ham.begin(), ham.end());
    birikim << goreli << "|" << ham.size() << "|"
            << security::sha256Hex(hamMetin) << "\n";
  }
  return security::sha256Hex(birikim.str());
}

LockKaydi lockKaydiV3Olustur(const std::string &ad, const std::string &kaynak,
                             const std::filesystem::path &paketKlasoru,
                             const std::string &kaynakRef) {
  LockKaydi kayit;
  kayit.ad = ad;
  kayit.kaynak = kaynak;
  kayit.surum = "v3";
  kayit.commitPin = gitCommitPinBul(paketKlasoru).value_or("-");
  kayit.icerikHash = klasorIcerikSha256(paketKlasoru);
  kayit.kaynakRef = kaynakRef;
  kayit.ozet = lockKaydiSha256(kayit);
  return kayit;
}

std::optional<LockKaydi> lockKaydiCoz(const std::string &satir) {
  const std::string trim = solaSagaKirp(satir);
  if (trim.empty() || trim[0] == '#') {
    return std::nullopt;
  }
  const std::vector<std::string> parcalar = boruIleBol(trim);
  if (parcalar.size() == 3) {
    // Eski v1: ad|kaynak|crc32
    return LockKaydi{parcalar[0], parcalar[1], parcalar[2], "v1", "", "", ""};
  }
  if (parcalar.size() == 4) {
    if (parcalar[3] == "v3") {
      throw std::runtime_error(
          "Hata: orhun.lock v3 kaydi commit/hash alanlari olmadan geldi: '" +
          trim + "'");
    }
    return LockKaydi{parcalar[0], parcalar[1], parcalar[2], parcalar[3], "",
                     "", ""};
  }
  if (parcalar.size() == 6) {
    return LockKaydi{parcalar[0], parcalar[1], parcalar[2], parcalar[3],
                     parcalar[4], parcalar[5], ""};
  }
  if (parcalar.size() == 7) {
    return LockKaydi{parcalar[0], parcalar[1], parcalar[2], parcalar[3],
                     parcalar[4], parcalar[5], parcalar[6]};
  }
  throw std::runtime_error("Hata: orhun.lock satiri gecersiz: '" + trim + "'");
}

std::vector<LockKaydi>
lockKayitlariniOku(const std::filesystem::path &lockDosyasi) {
  std::vector<LockKaydi> kayitlar;
  if (!std::filesystem::exists(lockDosyasi)) {
    return kayitlar;
  }
  std::istringstream ak(dosyaOku(lockDosyasi.string()));
  for (std::string satir; std::getline(ak, satir);) {
    auto kayit = lockKaydiCoz(satir);
    if (kayit.has_value()) {
      kayitlar.push_back(std::move(*kayit));
    }
  }
  return kayitlar;
}

void lockKayitlariniYaz(const std::filesystem::path &lockDosyasi,
                        const std::vector<LockKaydi> &kayitlar) {
  std::ostringstream yeni;
  yeni << "# ad|kaynak|sha256|surum|commit_pin|icerik_sha256|source_ref\n";
  for (std::size_t i = 0; i < kayitlar.size(); ++i) {
    const auto &k = kayitlar[i];
    if (k.surum == "v3") {
      yeni << k.ad << "|" << k.kaynak << "|" << k.ozet << "|" << k.surum
           << "|" << k.commitPin << "|" << k.icerikHash << "|" << k.kaynakRef;
    } else {
      yeni << k.ad << "|" << k.kaynak << "|" << k.ozet << "|" << k.surum;
    }
    if (i + 1 < kayitlar.size()) {
      yeni << '\n';
    }
  }
  dosyaYaz(lockDosyasi.string(), yeni.str());
}

void orhunLockKaydet(const std::string &paketAdi, const std::string &kaynak,
                     const std::filesystem::path &paketKlasoru,
                     const std::string &kaynakRef, bool kilitliMod) {
  if (!kilitliMod) {
    return;
  }
  namespace fs = std::filesystem;
  const fs::path lockDosyasi = fs::current_path() / "orhun.lock";
  std::vector<LockKaydi> kayitlar = lockKayitlariniOku(lockDosyasi);

  const LockKaydi yeniKayit =
      lockKaydiV3Olustur(paketAdi, kaynak, paketKlasoru, kaynakRef);

  bool guncellendi = false;
  for (auto &kayit : kayitlar) {
    if (kayit.ad == paketAdi) {
      kayit = yeniKayit;
      guncellendi = true;
      break;
    }
  }
  if (!guncellendi) {
    kayitlar.push_back(std::move(yeniKayit));
  }
  lockKayitlariniYaz(lockDosyasi, kayitlar);
}

bool lockKaydiDogrula(const LockKaydi &kayit, std::string *hata) {
  if (kayit.ad.empty() || kayit.kaynak.empty()) {
    if (hata != nullptr) {
      *hata = "kayit alanlari bos";
    }
    return false;
  }
  if (kayit.surum == "v1") {
    const std::string beklenen = crc32Hex(kayit.kaynak + "|" + kayit.ad);
    if (asciiKucuk(kayit.ozet) != asciiKucuk(beklenen)) {
      if (hata != nullptr) {
        *hata = "v1 CRC32 ozeti uyusmuyor";
      }
      return false;
    }
    return true;
  }
  if (kayit.surum != "v2" && kayit.surum != "v3") {
    if (hata != nullptr) {
      *hata = "desteklenmeyen lock surumu: " + kayit.surum;
    }
    return false;
  }
  if (kayit.surum == "v3" &&
      (kayit.commitPin.empty() || kayit.icerikHash.empty())) {
    if (hata != nullptr) {
      *hata = "v3 kaydinda commit pin veya icerik hash bos";
    }
    return false;
  }
  if (kayit.surum == "v3" && !kaynakRefGecerliMi(kayit.kaynakRef)) {
    if (hata != nullptr) {
      *hata = "v3 source_ref alani gecersiz";
    }
    return false;
  }
  const std::string beklenen = lockKaydiSha256(kayit);
  if (asciiKucuk(kayit.ozet) != asciiKucuk(beklenen)) {
    if (hata != nullptr) {
      *hata = "SHA-256 uyusmuyor";
    }
    return false;
  }
  return true;
}

int komutPaketDogrula() {
  namespace fs = std::filesystem;
  const fs::path lockDosyasi = fs::current_path() / "orhun.lock";
  if (!fs::exists(lockDosyasi)) {
    std::cout << "orhun.lock bulunamadi.\n";
    return 1;
  }

  const std::vector<LockKaydi> kayitlar = lockKayitlariniOku(lockDosyasi);
  if (kayitlar.empty()) {
    std::cout << "orhun.lock bos.\n";
    return 1;
  }

  bool basarisiz = false;
  for (const auto &kayit : kayitlar) {
    std::string neden;
    if (!lockKaydiDogrula(kayit, &neden)) {
      std::cout << "[HATA] " << kayit.ad << ": " << neden << "\n";
      basarisiz = true;
      continue;
    }
    if (uzakKaynakMi(kayit.kaynak)) {
      if (!uzakKaynakGuvenliMi(kayit.kaynak) ||
          !paketKaynakAllowlistteMi(kayit.kaynak)) {
        std::cout << "[HATA] " << kayit.ad
                  << ": kaynak allowlist/guvenlik kosullarini saglamiyor.\n";
        basarisiz = true;
        continue;
      }
    }
    const fs::path paketKlasoru = fs::current_path() / "lib" / kayit.ad;
    if (!fs::exists(paketKlasoru)) {
      std::cout << "[HATA] " << kayit.ad << ": lib/" << kayit.ad
                << " klasoru bulunamadi.\n";
      basarisiz = true;
      continue;
    }
    if (kayit.surum == "v3") {
      try {
        const std::string gercekIcerikHash = klasorIcerikSha256(paketKlasoru);
        if (asciiKucuk(gercekIcerikHash) != asciiKucuk(kayit.icerikHash)) {
          std::cout << "[HATA] " << kayit.ad
                    << ": icerik hash uyusmuyor (lock v3).\n";
          basarisiz = true;
          continue;
        }
      } catch (const std::exception &ex) {
        std::cout << "[HATA] " << kayit.ad
                  << ": icerik hash hesaplanamadi (" << ex.what() << ").\n";
        basarisiz = true;
        continue;
      }

      if (kayit.commitPin != "-" && !kayit.commitPin.empty()) {
        const auto gercekCommit = gitCommitPinBul(paketKlasoru);
        if (!gercekCommit.has_value()) {
          std::cout << "[HATA] " << kayit.ad
                    << ": commit pin dogrulanamadi (git metadata yok).\n";
          basarisiz = true;
          continue;
        }
        if (asciiKucuk(*gercekCommit) != asciiKucuk(kayit.commitPin)) {
          std::cout << "[HATA] " << kayit.ad << ": commit pin uyusmuyor.\n";
          basarisiz = true;
          continue;
        }
      }
      if (!kayit.kaynakRef.empty()) {
        const auto refCommit = gitKaynakRefCommitBul(paketKlasoru, kayit.kaynakRef);
        if (!refCommit.has_value()) {
          std::cout << "[HATA] " << kayit.ad
                    << ": source_ref commit'e cozulemedi.\n";
          basarisiz = true;
          continue;
        }
        if (kayit.commitPin == "-" || kayit.commitPin.empty()) {
          std::cout << "[HATA] " << kayit.ad
                    << ": source_ref icin commit pin gerekli.\n";
          basarisiz = true;
          continue;
        }
        if (asciiKucuk(*refCommit) != asciiKucuk(kayit.commitPin)) {
          std::cout << "[HATA] " << kayit.ad
                    << ": source_ref commit pin ile tutarsiz.\n";
          basarisiz = true;
          continue;
        }
      }
    }
    std::cout << "[OK] " << kayit.ad << " (" << kayit.surum << ")\n";
  }

  return basarisiz ? 1 : 0;
}

int komutPaketLockGuncelle() {
  namespace fs = std::filesystem;
  const fs::path lockDosyasi = fs::current_path() / "orhun.lock";
  if (!fs::exists(lockDosyasi)) {
    std::cout << "orhun.lock bulunamadi.\n";
    return 1;
  }
  const std::vector<LockKaydi> kayitlar = lockKayitlariniOku(lockDosyasi);
  if (kayitlar.empty()) {
    std::cout << "orhun.lock bos.\n";
    return 1;
  }

  std::vector<LockKaydi> guncel;
  guncel.reserve(kayitlar.size());
  for (const auto &kayit : kayitlar) {
    const fs::path paketKlasoru = fs::current_path() / "lib" / kayit.ad;
    if (!fs::exists(paketKlasoru)) {
      throw std::runtime_error("Hata: lock guncelle icin lib/" + kayit.ad +
                               " klasoru bulunamadi.");
    }
    guncel.push_back(
        lockKaydiV3Olustur(kayit.ad, kayit.kaynak, paketKlasoru, kayit.kaynakRef));
  }
  lockKayitlariniYaz(lockDosyasi, guncel);
  std::cout << "orhun.lock v3'e guncellendi. kayit_sayisi=" << guncel.size()
            << "\n";
  return 0;
}

void orhunYapBagimlilikEkle(const std::string &paketAdi) {
  namespace fs = std::filesystem;
  const fs::path yapDosyasi = fs::current_path() / "orhun.yap";
  if (!fs::exists(yapDosyasi)) {
    return;
  }

  std::string icerik = dosyaOku(yapDosyasi.string());
  if (icerik.find("- " + paketAdi) != std::string::npos) {
    return;
  }

  if (icerik.find("bagimliliklar:") == std::string::npos) {
    if (!icerik.empty() && icerik.back() != '\n') {
      icerik.push_back('\n');
    }
    icerik += "bagimliliklar:\n";
  }

  if (!icerik.empty() && icerik.back() != '\n') {
    icerik.push_back('\n');
  }
  icerik += "- " + paketAdi + "\n";
  dosyaYaz(yapDosyasi.string(), icerik);
}

int komutPaketKur(const std::string &kaynak, const std::string &hedefAdi,
                  const std::string &kaynakRef, bool noLock) {
  namespace fs = std::filesystem;
  if (kaynak.empty()) {
    throw std::runtime_error("Hata: paket kur icin kaynak belirtilmeli.");
  }
  if (!kaynakRefGecerliMi(kaynakRef)) {
    throw std::runtime_error(
        "Hata: --ref alani bosluk veya '|' karakteri iceremez.");
  }

  std::string cozulmusKaynak = kaynak;
  if (kaynak.rfind("depo:", 0) == 0) {
    auto cozum = depodanKaynakCoz(kaynak);
    if (!cozum.has_value()) {
      throw std::runtime_error("Hata: '" + kaynak +
                               "' deposunda paket bulunamadi. 'orhun paket ara "
                               "<kelime>' ile arayin.");
    }
    cozulmusKaynak = cozum.value();
  }

  fs::path libKlasoru = fs::current_path() / "lib";
  fs::create_directories(libKlasoru);

  const std::string paketAdi =
      hedefAdi.empty() ? paketAdiCikar(cozulmusKaynak) : hedefAdi;
  if (!paketAdiGecerliMi(paketAdi)) {
    throw std::runtime_error(
        "Hata: paket adi yalnizca harf/rakam/_/./- icerebilir.");
  }
  const fs::path hedefYol = libKlasoru / paketAdi;
  if (fs::exists(hedefYol)) {
    throw std::runtime_error("Hata: '" + hedefYol.string() + "' zaten mevcut.");
  }

  std::error_code ec;
  if (fs::exists(cozulmusKaynak, ec) && !ec) {
    fs::copy(cozulmusKaynak, hedefYol,
             fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    if (ec) {
      throw std::runtime_error("Hata: Yerel paket kopyalanamadi: " +
                               ec.message());
    }
    if (!kaynakRef.empty()) {
      const int checkoutKod = gitRefCheckout(hedefYol, kaynakRef);
      if (checkoutKod != 0) {
        throw std::runtime_error(
            "Hata: --ref checkout basarisiz (yerel kaynak). Git cikis kodu: " +
            std::to_string(checkoutKod));
      }
    }
  } else if (uzakKaynakMi(cozulmusKaynak)) {
    if (!uzakKaynakGuvenliMi(cozulmusKaynak)) {
      throw std::runtime_error(
          "Hata: Paket kaynagi guvenli degil. Tehlikeli karakterler iceriyor.");
    }
    if (!paketKaynakAllowlistteMi(cozulmusKaynak)) {
      throw std::runtime_error(
          "Hata: Paket kaynagi allowlist disinda. ORHUN_PAKET_ALLOWLIST ile "
          "izinli alan adlarini genisletebilirsiniz.");
    }
    std::vector<std::string> cloneKomut = {"git", "clone"};
    if (kaynakRef.empty()) {
      cloneKomut.push_back("--depth");
      cloneKomut.push_back("1");
    }
    cloneKomut.push_back(cozulmusKaynak);
    cloneKomut.push_back(hedefYol.string());
    const int kod = processCalistir(cloneKomut);
    if (kod != 0) {
      throw std::runtime_error(
          "Hata: Paket indirilemedi. Git clone cikis kodu: " +
          std::to_string(kod));
    }
    if (!kaynakRef.empty()) {
      const int checkoutKod = gitRefCheckout(hedefYol, kaynakRef);
      if (checkoutKod != 0) {
        throw std::runtime_error(
            "Hata: --ref checkout basarisiz (uzak kaynak). Git cikis kodu: " +
            std::to_string(checkoutKod));
      }
    }
  } else {
    throw std::runtime_error(
        "Hata: Paket kaynagi bulunamadi. Yerel yol ya da git URL verin.");
  }

  orhunYapBagimlilikEkle(paketAdi);
  orhunLockKaydet(paketAdi, cozulmusKaynak, hedefYol, kaynakRef, !noLock);
  if (noLock) {
    std::cout << "[uyari] --no-lock etkin: orhun.lock guncellenmedi.\n";
  }
  std::cout << "Paket kuruldu: " << paketAdi << " -> " << hedefYol.string()
            << "\n";
  if (!kaynakRef.empty()) {
    std::cout << "Sabitlenen ref: " << kaynakRef << "\n";
  }
  return 0;
}

int komutPaketListe() {
  namespace fs = std::filesystem;
  const fs::path libKlasoru = fs::current_path() / "lib";
  if (!fs::exists(libKlasoru)) {
    std::cout << "Kurulu paket yok (lib klasoru bulunamadi).\n";
    return 0;
  }

  std::vector<std::string> paketler;
  for (const auto &giris : fs::directory_iterator(libKlasoru)) {
    if (giris.is_directory()) {
      paketler.push_back(giris.path().filename().u8string());
    }
  }
  std::sort(paketler.begin(), paketler.end());

  if (paketler.empty()) {
    std::cout << "Kurulu paket yok.\n";
    return 0;
  }

  std::cout << "Kurulu paketler:\n";
  for (const auto &ad : paketler) {
    std::cout << "- " << ad << "\n";
  }
  return 0;
}

bool vmFallbackAcikMi() {
  auto metniKucuklestir = [](std::string metin) {
    std::transform(
        metin.begin(), metin.end(), metin.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return metin;
  };
  auto acikMi = [&](const std::string &ham) {
    const std::string metin = metniKucuklestir(ham);
    return !(metin == "0" || metin == "false" || metin == "off" ||
             metin == "hayir" || metin == "no");
  };
  auto kanalBul = [&]() {
    const char *kanal = std::getenv("ORHUN_CHANNEL");
    if (kanal == nullptr || *kanal == '\0') {
      kanal = std::getenv("ORHUN_RELEASE_CHANNEL");
    }
    if (kanal == nullptr || *kanal == '\0') {
      return std::string("stable");
    }
    std::string normalized = metniKucuklestir(kanal);
    if (normalized == "stable" || normalized == "beta" ||
        normalized == "nightly" || normalized == "dev") {
      return normalized;
    }
    return std::string("stable");
  };

  const char *deger = std::getenv("ORHUN_VM_FALLBACK");
  if (deger != nullptr && *deger != '\0') {
    return acikMi(deger);
  }
  const std::string kanal = kanalBul();
  return kanal == "beta" || kanal == "nightly" || kanal == "dev";
}

std::string vmFallbackKanaliniBul() {
  const char *kanal = std::getenv("ORHUN_CHANNEL");
  if (kanal == nullptr || *kanal == '\0') {
    kanal = std::getenv("ORHUN_RELEASE_CHANNEL");
  }
  if (kanal == nullptr || *kanal == '\0') {
    return "stable";
  }
  std::string metin(kanal);
  std::transform(
      metin.begin(), metin.end(), metin.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (metin == "stable" || metin == "beta" || metin == "nightly" ||
      metin == "dev") {
    return metin;
  }
  return "stable";
}

std::string vmFallbackKaynakBilgisi() {
  const char *deger = std::getenv("ORHUN_VM_FALLBACK");
  if (deger != nullptr && *deger != '\0') {
    return "ORHUN_VM_FALLBACK";
  }
  const char *kanal = std::getenv("ORHUN_CHANNEL");
  if (kanal != nullptr && *kanal != '\0') {
    return "ORHUN_CHANNEL";
  }
  kanal = std::getenv("ORHUN_RELEASE_CHANNEL");
  if (kanal != nullptr && *kanal != '\0') {
    return "ORHUN_RELEASE_CHANNEL";
  }
  return "varsayilan(stable)";
}

bool vmFallbackIzinliHata(const std::string &hata) {
  static const std::vector<std::string> izinliImzalar = {
      "ORH-COMP-001", "ORH-COMP-002", "ORH-COMP-003"};
  for (const auto &imza : izinliImzalar) {
    if (hata.find(imza) != std::string::npos) {
      return true;
    }
  }
  return false;
}

int dosyaCalistirVM(const std::string &dosyaYolu, bool katiMod) {
  if (dosyaYolu.size() < 3 || dosyaYolu.substr(dosyaYolu.size() - 3) != ".oh") {
    throw std::runtime_error(
        "Hata: VM calistirma icin .oh dosyasi bekleniyor.");
  }

  const std::string kod = dosyaOku(dosyaYolu);
  if (katiMod) {
    kodCalistirVM(kod);
    return 0;
  }

  try {
    kodCalistirVM(kod);
  } catch (const std::exception &ex) {
    if (katiMod || !vmFallbackAcikMi() || !vmFallbackIzinliHata(ex.what())) {
      throw;
    }
    // Uretim modunda VM hatalarinda da yorumlayiciya dus: kullanici acisindan
    // davranis korunur; vm-kati modunda ise dogrudan hata verilir.
    static bool uyariYazildi = false;
    if (!uyariYazildi) {
      std::cerr << "[uyari] VM fallback devrede. ORHUN_VM_FALLBACK=0 ile "
                   "kapatabilirsiniz.\n";
      uyariYazildi = true;
    }
    Interpreter yorumlayici;
    kodCalistir(kod, yorumlayici);
  }
  return 0;
}

int dosyaCalistirYorumlayici(const std::string &dosyaYolu) {
  if (dosyaYolu.size() < 3 || dosyaYolu.substr(dosyaYolu.size() - 3) != ".oh") {
    throw std::runtime_error(
        "Hata: yorumla komutu icin .oh dosyasi bekleniyor.");
  }

  const std::string kod = dosyaOku(dosyaYolu);
  Interpreter yorumlayici;
  kodCalistir(kod, yorumlayici);
  return 0;
}

template <typename F> double tekOlcumMilisaniye(F &&fonksiyon) {
  const auto bas = std::chrono::steady_clock::now();
  fonksiyon();
  const auto son = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(son - bas).count();
}

template <typename F>
std::vector<double> dagilimOlcMilisaniye(F &&fonksiyon, int tekrar) {
  if (tekrar <= 0) {
    throw std::runtime_error("hiz komutu icin tekrar sayisi pozitif olmali.");
  }
  std::vector<double> olcumler;
  olcumler.reserve(static_cast<std::size_t>(tekrar));
  for (int i = 0; i < tekrar; ++i) {
    olcumler.push_back(tekOlcumMilisaniye(fonksiyon));
  }
  return olcumler;
}

double toplamMilisaniye(const std::vector<double> &olcumler) {
  double toplam = 0.0;
  for (double ms : olcumler) {
    toplam += ms;
  }
  return toplam;
}

double yuzdeDilimi(std::vector<double> olcumler, double yuzde) {
  if (olcumler.empty()) {
    return 0.0;
  }
  std::sort(olcumler.begin(), olcumler.end());
  const double clamped = std::clamp(yuzde, 0.0, 100.0);
  const double konum =
      (clamped / 100.0) * static_cast<double>(olcumler.size() - 1);
  const auto altIndex = static_cast<std::size_t>(std::floor(konum));
  const auto ustIndex = static_cast<std::size_t>(std::ceil(konum));
  if (altIndex == ustIndex) {
    return olcumler[altIndex];
  }
  const double oran = konum - static_cast<double>(altIndex);
  return olcumler[altIndex] + (olcumler[ustIndex] - olcumler[altIndex]) * oran;
}

std::string jsonKacis(const std::string &metin) {
  std::ostringstream ss;
  for (char c : metin) {
    switch (c) {
    case '\\':
      ss << "\\\\";
      break;
    case '"':
      ss << "\\\"";
      break;
    case '\n':
      ss << "\\n";
      break;
    case '\r':
      ss << "\\r";
      break;
    case '\t':
      ss << "\\t";
      break;
    default:
      ss << c;
      break;
    }
  }
  return ss.str();
}

std::optional<double> jsonAlanSayisiBul(const std::string &json,
                                        const std::string &alan) {
  const std::string etiket = "\"" + alan + "\":";
  std::size_t i = json.find(etiket);
  if (i == std::string::npos) {
    return std::nullopt;
  }
  i += etiket.size();
  while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) {
    ++i;
  }
  std::size_t j = i;
  if (j < json.size() && (json[j] == '-' || json[j] == '+')) {
    ++j;
  }
  bool nokta = false;
  while (j < json.size()) {
    const char c = json[j];
    if (std::isdigit(static_cast<unsigned char>(c))) {
      ++j;
      continue;
    }
    if (c == '.' && !nokta) {
      nokta = true;
      ++j;
      continue;
    }
    break;
  }
  if (j <= i) {
    return std::nullopt;
  }
  return std::stod(json.substr(i, j - i));
}

std::optional<std::pair<double, double>>
benchmarkBazHizBul(const std::string &baselineJsonl,
                   const std::string &dosyaYolu) {
  if (baselineJsonl.empty()) {
    return std::nullopt;
  }
  std::string baselineIcerik;
  try {
    baselineIcerik = dosyaOku(baselineJsonl);
  } catch (const std::exception &ex) {
    throw CliCikisHatasi(
        3, "Benchmark altyapi hatasi: baseline dosyasi okunamadi ('" +
               baselineJsonl + "'). " + ex.what());
  }
  std::istringstream ak(baselineIcerik);
  const std::string hedef = "\"dosya\":\"" + jsonKacis(dosyaYolu) + "\"";
  for (std::string satir; std::getline(ak, satir);) {
    if (satir.find(hedef) == std::string::npos) {
      continue;
    }
    const auto p50 = jsonAlanSayisiBul(satir, "p50_x");
    const auto p90 = jsonAlanSayisiBul(satir, "p90_x");
    if (p50.has_value() && p90.has_value()) {
      return std::make_pair(*p50, *p90);
    }
  }
  return std::nullopt;
}

enum class BenchmarkOlcumModu { Runtime, Full };

const char *benchmarkOlcumModuMetni(BenchmarkOlcumModu mod) {
  return mod == BenchmarkOlcumModu::Runtime ? "runtime" : "full";
}

BenchmarkOlcumModu benchmarkOlcumModuCoz(const std::string &ham) {
  const std::string metin = asciiKucuk(ham);
  if (metin == "runtime") {
    return BenchmarkOlcumModu::Runtime;
  }
  if (metin == "full") {
    return BenchmarkOlcumModu::Full;
  }
  throw std::runtime_error("Hata: --olcum-modu yalnizca runtime veya full "
                           "degerini alabilir.");
}

template <typename F> void warmupCalistir(F &&fonksiyon, int warmup) {
  for (int i = 0; i < warmup; ++i) {
    fonksiyon();
  }
}

int komutHiz(const std::string &dosyaYolu, int tekrar, bool jsonCikti,
             const std::string &baselineJsonl, double gateP50, double gateP90,
             BenchmarkOlcumModu olcumModu, int warmup) {
  if (dosyaYolu.size() < 3 || dosyaYolu.substr(dosyaYolu.size() - 3) != ".oh") {
    throw std::runtime_error("Hata: hiz komutu icin .oh dosyasi bekleniyor.");
  }
  if (warmup < 0) {
    throw std::runtime_error("Hata: hiz komutu icin warmup sifir veya pozitif "
                             "olmali.");
  }

  const std::string kod = dosyaOku(dosyaYolu);
  const double parseMs = tekOlcumMilisaniye([&]() {
    std::unique_ptr<ProgramNode> program = parseEt(kod);
    (void)program;
  });

  std::unique_ptr<ProgramNode> vmProgram = parseEt(kod);
  BytecodeChunk runtimeChunk;
  const double vmCompileMs = tekOlcumMilisaniye([&]() {
    Compiler derleyici;
    runtimeChunk = derleyici.derle(vmProgram.get());
  });

  std::unique_ptr<ProgramNode> yorumProgram;
  if (olcumModu == BenchmarkOlcumModu::Runtime) {
    yorumProgram = parseEt(kod);
  }

  std::ostringstream yut;
  auto *eskiCout = std::cout.rdbuf(yut.rdbuf());
  try {
    auto yorumlayiciCalisma = [&]() {
      Interpreter yorumlayici;
      if (olcumModu == BenchmarkOlcumModu::Runtime) {
        yorumlayici.calistir(yorumProgram.get());
      } else {
        kodCalistir(kod, yorumlayici);
      }
    };
    auto vmCalisma = [&]() {
      if (olcumModu == BenchmarkOlcumModu::Runtime) {
        VM vm;
        vm.calistir(runtimeChunk);
      } else {
        kodCalistirVM(kod);
      }
    };

    warmupCalistir(yorumlayiciCalisma, warmup);
    warmupCalistir(vmCalisma, warmup);

    const std::vector<double> yorumlayiciOlcumleri =
        dagilimOlcMilisaniye(yorumlayiciCalisma, tekrar);
    const std::vector<double> vmOlcumleri =
        dagilimOlcMilisaniye(vmCalisma, tekrar);

    std::cout.rdbuf(eskiCout);

    const double yorumlayiciToplam = toplamMilisaniye(yorumlayiciOlcumleri);
    const double vmToplam = toplamMilisaniye(vmOlcumleri);
    const double yorumlayiciP50 = yuzdeDilimi(yorumlayiciOlcumleri, 50.0);
    const double yorumlayiciP90 = yuzdeDilimi(yorumlayiciOlcumleri, 90.0);
    const double vmP50 = yuzdeDilimi(vmOlcumleri, 50.0);
    const double vmP90 = yuzdeDilimi(vmOlcumleri, 90.0);
    const double hizlanma =
        vmToplam > 0.0 ? (yorumlayiciToplam / vmToplam) : 0.0;
    const double p50Hizlanma = vmP50 > 0.0 ? (yorumlayiciP50 / vmP50) : 0.0;
    const double p90Hizlanma = vmP90 > 0.0 ? (yorumlayiciP90 / vmP90) : 0.0;
    const auto baseline = benchmarkBazHizBul(baselineJsonl, dosyaYolu);
    const double bazP50 = baseline.has_value() ? baseline->first : 0.0;
    const double bazP90 = baseline.has_value() ? baseline->second : 0.0;
    const double p50Oran = (baseline.has_value() && bazP50 > 0.0)
                               ? (p50Hizlanma / bazP50)
                               : p50Hizlanma;
    const double p90Oran = (baseline.has_value() && bazP90 > 0.0)
                               ? (p90Hizlanma / bazP90)
                               : p90Hizlanma;
    const bool gateP50Gecerli = gateP50 <= 0.0 || p50Oran >= gateP50;
    const bool gateP90Gecerli = gateP90 <= 0.0 || p90Oran >= gateP90;
    const bool gateGecerli = gateP50Gecerli && gateP90Gecerli;
    const char *gateResult = gateGecerli ? "pass" : "fail";

    if (jsonCikti) {
      std::cout << "{"
                << "\"dosya\":\"" << jsonKacis(dosyaYolu) << "\","
                << "\"tekrar\":" << tekrar << ","
                << "\"warmup\":" << warmup << ","
                << "\"olcum_modu\":\"" << benchmarkOlcumModuMetni(olcumModu)
                << "\","
                << "\"parse_ms\":" << parseMs << ","
                << "\"vm_compile_ms\":" << vmCompileMs << ","
                << "\"interpreter\":{\"toplam_ms\":" << yorumlayiciToplam
                << ",\"p50_ms\":" << yorumlayiciP50
                << ",\"p90_ms\":" << yorumlayiciP90 << "},"
                << "\"vm\":{\"toplam_ms\":" << vmToplam
                << ",\"p50_ms\":" << vmP50 << ",\"p90_ms\":" << vmP90 << "},"
                << "\"hizlanma\":{\"toplam_x\":" << hizlanma
                << ",\"p50_x\":" << p50Hizlanma << ",\"p90_x\":" << p90Hizlanma
                << "},"
                << "\"gate_result\":\"" << gateResult << "\","
                << "\"gate\":{\"p50_oran\":" << p50Oran
                << ",\"p90_oran\":" << p90Oran
                << ",\"gecerli\":" << (gateGecerli ? "true" : "false")
                << ",\"baseline\":\"" << jsonKacis(baselineJsonl) << "\"}"
                << "}\n";
      return gateGecerli ? 0 : 2;
    }

    std::cout << "Dosya: " << dosyaYolu << "\n";
    std::cout << "Tekrar: " << tekrar << "\n";
    std::cout << "Warmup: " << warmup << "\n";
    std::cout << "Olcum modu: " << benchmarkOlcumModuMetni(olcumModu) << "\n";
    std::cout << "Parse maliyeti: " << parseMs << " ms\n";
    std::cout << "VM compile maliyeti: " << vmCompileMs << " ms\n";
    std::cout << "Interpreter toplam: " << yorumlayiciToplam << " ms\n";
    std::cout << "Interpreter P50/P90: " << yorumlayiciP50 << " / "
              << yorumlayiciP90 << " ms\n";
    std::cout << "VM toplam: " << vmToplam << " ms\n";
    std::cout << "VM P50/P90: " << vmP50 << " / " << vmP90 << " ms\n";
    if (vmToplam > 0.0) {
      std::cout << "Hizlanma (toplam): " << hizlanma << "x\n";
    }
    if (vmP50 > 0.0) {
      std::cout << "Hizlanma (P50): " << p50Hizlanma << "x\n";
    }
    if (vmP90 > 0.0) {
      std::cout << "Hizlanma (P90): " << p90Hizlanma << "x\n";
    }
    if (!baselineJsonl.empty()) {
      std::cout << "Baseline: " << baselineJsonl << "\n";
      if (baseline.has_value()) {
        std::cout << "P50 oran (simdiki/baseline): " << p50Oran << "x\n";
        std::cout << "P90 oran (simdiki/baseline): " << p90Oran << "x\n";
      } else {
        std::cout << "[uyari] Baseline dosyasinda bu test bulunamadi, mutlak "
                     "hizlanma kullanildi.\n";
      }
    }
    if (gateP50 > 0.0 || gateP90 > 0.0) {
      std::cout << "Gate durumu: " << (gateGecerli ? "GECTI" : "BASARISIZ")
                << " (P50>=" << gateP50 << ", P90>=" << gateP90 << ")\n";
    }
    std::cout << "Gate sonucu: " << gateResult << "\n";
    return gateGecerli ? 0 : 2;
  } catch (...) {
    std::cout.rdbuf(eskiCout);
    throw;
  }
}

int komutObcCalistir(const std::string &obcDosyaYolu) {
  const std::vector<std::uint8_t> ham = dosyaOkuIkili(obcDosyaYolu);
  BytecodeChunk chunk = chunkCoz(ham);
  VM vm;
  vm.calistir(chunk);
  return 0;
}

int komutDerle(const std::string &kaynakYolu, const std::string &calisanExeYolu,
               const std::string &ciktiTemel) {
  namespace fs = std::filesystem;
  if (kaynakYolu.size() < 3 ||
      kaynakYolu.substr(kaynakYolu.size() - 3) != ".oh") {
    throw std::runtime_error("Hata: derle komutu .oh dosyasi bekler.");
  }

  const BytecodeChunk chunk = bytecodeDerle(dosyaOku(kaynakYolu));
  const std::vector<std::uint8_t> payload = chunkSerilestir(chunk);

  fs::path temel =
      ciktiTemel.empty() ? fs::path(kaynakYolu) : fs::path(ciktiTemel);
  if (temel.has_extension()) {
    temel.replace_extension("");
  } else if (ciktiTemel.empty()) {
    temel.replace_extension("");
  }

  fs::path obcYolu = temel;
  obcYolu.replace_extension(".obc");
  fs::path exeYolu = temel;
  exeYolu.replace_extension(".exe");
  fs::path metaYolu = temel;
  metaYolu.replace_extension(".obc.meta.json");

  dosyaYazIkili(obcYolu.string(), payload);
  paketliExeUret(calisanExeYolu, exeYolu.string(), payload);

  const std::uint32_t payloadCrc = crc32Hesapla(payload);
  std::ostringstream meta;
  meta << "{\n"
       << "  \"format\": \"orhun-obc-v1\",\n"
       << "  \"payload_size\": " << payload.size() << ",\n"
       << "  \"payload_crc32\": \"" << std::hex << std::setfill('0')
       << std::setw(8) << payloadCrc << "\",\n"
       << "  \"source_name\": \""
       << jsonKacis(fs::path(kaynakYolu).filename().u8string()) << "\"\n"
       << "}\n";
  dosyaYaz(metaYolu.string(), meta.str());

  std::cout << "Bytecode uretildi: " << obcYolu.string() << "\n";
  std::cout << "Paketli exe uretildi: " << exeYolu.string() << "\n";
  std::cout << "Metadata yazildi: " << metaYolu.string() << "\n";
  return 0;
}

std::string evetHayir(bool deger) { return deger ? "evet" : "hayir"; }

bool doctorOrtamDegiskeniAcik(const char *ad) {
  const char *v = std::getenv(ad);
  if (v == nullptr || *v == '\0') {
    return false;
  }
  std::string s(v);
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return !(s.empty() || s == "0" || s == "false" || s == "off" ||
           s == "hayir" || s == "no");
}

std::string doctorFfiVarsayilanPolitika(const std::string &kanal) {
  const char *env = std::getenv("ORHUN_FFI_POLICY");
  if (env != nullptr && *env != '\0') {
    std::string politika = asciiKucuk(solaSagaKirp(env));
    if (politika == "off" || politika == "allowlist" || politika == "full") {
      return politika;
    }
  }
  if (kanal == "stable") {
    return "allowlist";
  }
  return "full";
}

std::string doctorBuildCommit() {
  const char *env = std::getenv("ORHUN_BUILD_COMMIT");
  if (env != nullptr && *env != '\0') {
    return env;
  }
  return "unknown";
}

std::string jsonBool(bool deger) { return deger ? "true" : "false"; }

std::string jsonDizi(const std::vector<std::string> &degerler) {
  std::ostringstream ss;
  ss << "[";
  for (std::size_t i = 0; i < degerler.size(); ++i) {
    if (i > 0) {
      ss << ",";
    }
    ss << "\"" << jsonKacis(degerler[i]) << "\"";
  }
  ss << "]";
  return ss.str();
}

int komutDoctor(bool jsonCikti) {
  namespace fs = std::filesystem;
  const bool hasTests =
      fs::exists("tests/run_tests.ps1") || fs::exists("tests/run_tests.sh");
  const bool hasCases = fs::exists("tests/cases");
  const bool hasStdLib = fs::exists("Yerlesik.h");
  const bool hasCompiler = fs::exists("Compiler.cpp") && fs::exists("VM.cpp");
  const bool hasLspTool = fs::exists("tools/vscode/package.json") ||
                          fs::exists("tools/vscode-orhun/package.json");
  const bool lockVar = fs::exists("orhun.lock");
  const bool hasBenchmarkGate = fs::exists("tests/benchmark_gate.ps1") ||
                                fs::exists("tests/benchmark_gate.sh");
  const bool fallback = vmFallbackAcikMi();
  const std::string releaseChannel = vmFallbackKanaliniBul();
  const std::string fallbackKaynak = vmFallbackKaynakBilgisi();
  const std::string ffiPolitika = doctorFfiVarsayilanPolitika(releaseChannel);
  const bool komutKisitliVarsayilan =
      !(doctorOrtamDegiskeniAcik("ORHUN_UNSAFE") ||
        doctorOrtamDegiskeniAcik("ORHUN_SYSTEM_UNSAFE"));

  const bool gitVar = programYoldaVarMi("git");
  const bool lockV3 = [&]() {
    if (!lockVar) {
      return false;
    }
    try {
      const auto kayitlar = lockKayitlariniOku("orhun.lock");
      if (kayitlar.empty()) {
        return false;
      }
      for (const auto &k : kayitlar) {
        if (k.surum != "v3") {
          return false;
        }
      }
      return true;
    } catch (...) {
      return false;
    }
  }();
  std::vector<std::string> ciProfilleri;
  if (fs::exists(".github/workflows/ci.yml")) {
    ciProfilleri.push_back("ci");
  }
  if (fs::exists(".github/workflows/nightly.yml")) {
    ciProfilleri.push_back("nightly");
  }
  const bool saglikli = hasCompiler && hasStdLib && hasTests;

  if (jsonCikti) {
    std::cout << "{"
              << "\"version\":\"" << ORHUN_SURUM << "\","
              << "\"build\":\"" << ORHUN_INSA_NO << "\","
              << "\"commit\":\"" << jsonKacis(doctorBuildCommit()) << "\","
              << "\"channel\":\"" << jsonKacis(releaseChannel) << "\","
              << "\"fallback_default\":" << jsonBool(fallback) << ","
              << "\"fallback_source\":\"" << jsonKacis(fallbackKaynak) << "\","
              << "\"ci_profiles\":" << jsonDizi(ciProfilleri) << ","
              << "\"security_mode\":{"
              << "\"system_command_restricted_default\":"
              << jsonBool(komutKisitliVarsayilan) << ","
              << "\"ffi_policy_default\":\"" << jsonKacis(ffiPolitika) << "\","
              << "\"package_source_allowlist\":true"
              << "},"
              << "\"checks\":{"
              << "\"compiler_files\":" << jsonBool(hasCompiler) << ","
              << "\"stdlib_core\":" << jsonBool(hasStdLib) << ","
              << "\"test_infra\":" << jsonBool(hasTests && hasCases) << ","
              << "\"lsp_tools\":" << jsonBool(hasLspTool) << ","
              << "\"lock_exists\":" << jsonBool(lockVar) << ","
              << "\"lock_v3\":" << jsonBool(lockV3) << ","
              << "\"benchmark_gate_scripts\":" << jsonBool(hasBenchmarkGate)
              << ","
              << "\"git_access\":" << jsonBool(gitVar)
#ifdef _WIN32
              << ",\"windows_console_utf8\":"
              << jsonBool(GetConsoleOutputCP() == CP_UTF8 &&
                          GetConsoleCP() == CP_UTF8)
#endif
              << "},"
              << "\"status\":\"" << (saglikli ? "ready" : "missing") << "\""
              << "}\n";
    return saglikli ? 0 : 2;
  }

  std::cout << "Orhun Doctor Raporu\n";
  std::cout << "-------------------\n";
  std::cout << "VM derleyici dosyalari: " << evetHayir(hasCompiler) << "\n";
  std::cout << "StdLib cekirdegi (Yerlesik.h): " << evetHayir(hasStdLib)
            << "\n";
  std::cout << "Test altyapisi: " << evetHayir(hasTests && hasCases) << "\n";
  std::cout << "LSP/VSCode araclari: " << evetHayir(hasLspTool) << "\n";
  std::cout << "Paket lock dosyasi (orhun.lock): " << evetHayir(lockVar)
            << "\n";
  if (lockVar) {
    std::cout << "Lock surumu v3: " << evetHayir(lockV3) << "\n";
  }
  std::cout << "Benchmark gate scriptleri: " << evetHayir(hasBenchmarkGate)
            << "\n";
  std::cout << "Git erisimi: " << evetHayir(gitVar) << "\n";
  std::cout << "FFI varsayilan politika: " << ffiPolitika << "\n";
  std::cout << "sistem.komut varsayilan kisit: "
            << (komutKisitliVarsayilan ? "evet" : "hayir") << "\n";
  std::cout << "Release channel: " << releaseChannel << "\n";
  std::cout << "VM fallback karar kaynagi: " << fallbackKaynak << "\n";
  std::cout << "VM fallback varsayilan durumu: "
            << (fallback ? "acik (ORHUN_VM_FALLBACK=0 ile kapat)" : "kapali")
            << "\n";
#ifdef _WIN32
  std::cout << "Windows Console UTF-8: "
            << evetHayir(GetConsoleOutputCP() == CP_UTF8 &&
                         GetConsoleCP() == CP_UTF8)
            << "\n";
#endif
  std::cout << "Genel durum: " << (saglikli ? "hazir" : "eksikler var") << "\n";
  return saglikli ? 0 : 2;
}

std::string soldanBoslukKirp(std::string metin) {
  std::size_t i = 0;
  while (i < metin.size() &&
         std::isspace(static_cast<unsigned char>(metin[i]))) {
    ++i;
  }
  return metin.substr(i);
}

std::optional<std::string> lspMesajOku(std::istream &in) {
  std::string satir;
  std::size_t icerikUzunlugu = 0;
  bool uzunlukBulundu = false;

  while (std::getline(in, satir)) {
    if (!satir.empty() && satir.back() == '\r') {
      satir.pop_back();
    }
    if (satir.empty()) {
      break;
    }
    const std::string onEk = "Content-Length:";
    if (satir.rfind(onEk, 0) == 0) {
      std::string sayi = soldanBoslukKirp(satir.substr(onEk.size()));
      icerikUzunlugu = static_cast<std::size_t>(std::stoul(sayi));
      uzunlukBulundu = true;
    }
  }

  if (!uzunlukBulundu) {
    return std::nullopt;
  }

  std::string govde(icerikUzunlugu, '\0');
  in.read(govde.data(), static_cast<std::streamsize>(icerikUzunlugu));
  if (static_cast<std::size_t>(in.gcount()) != icerikUzunlugu) {
    return std::nullopt;
  }
  return govde;
}

std::optional<std::string> lspIdTokenBul(const std::string &json) {
  const std::size_t idPos = json.find("\"id\"");
  if (idPos == std::string::npos) {
    return std::nullopt;
  }
  std::size_t i = json.find(':', idPos + 4);
  if (i == std::string::npos) {
    return std::nullopt;
  }
  ++i;
  while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) {
    ++i;
  }
  if (i >= json.size()) {
    return std::nullopt;
  }

  if (json[i] == '"') {
    const std::size_t bas = i++;
    bool kacis = false;
    while (i < json.size()) {
      if (!kacis && json[i] == '"') {
        ++i;
        return json.substr(bas, i - bas);
      }
      if (!kacis && json[i] == '\\') {
        kacis = true;
      } else {
        kacis = false;
      }
      ++i;
    }
    return std::nullopt;
  }

  const std::size_t bas = i;
  while (i < json.size() && json[i] != ',' && json[i] != '}' &&
         json[i] != ']') {
    ++i;
  }
  return sagaBoslukKirp(json.substr(bas, i - bas));
}

bool lspMethodMu(const std::string &json, const std::string &method) {
  return json.find("\"method\":\"" + method + "\"") != std::string::npos ||
         json.find("\"method\": \"" + method + "\"") != std::string::npos;
}

void lspYanitYaz(std::ostream &out, const std::string &idToken,
                 const std::string &resultJson) {
  const std::string yuk = std::string("{\"jsonrpc\":\"2.0\",\"id\":") +
                          idToken + ",\"result\":" + resultJson + "}";
  out << "Content-Length: " << yuk.size() << "\r\n\r\n" << yuk;
  out.flush();
}

std::string lspTamamlamaSonucuJson() {
  struct TamamlamaOgesi {
    const char *etiket;
    int tur;
    const char *ayrinti;
  };

  static const std::vector<TamamlamaOgesi> ogeler = {
      {"olsun", 14, "Orhun anahtar kelimesi"},
      {"eğer", 14, "Orhun anahtar kelimesi"},
      {"ise", 14, "Orhun anahtar kelimesi"},
      {"değilse", 14, "Orhun anahtar kelimesi"},
      {"doğru", 14, "Orhun sabiti"},
      {"yanlış", 14, "Orhun sabiti"},
      {"eşit", 14, "Orhun operatörü"},
      {"eşit_değil", 14, "Orhun operatörü"},
      {"büyük", 14, "Orhun operatörü"},
      {"küçük", 14, "Orhun operatörü"},
      {"ve", 14, "Orhun operatörü"},
      {"veya", 14, "Orhun operatörü"},
      {"değil", 14, "Orhun operatörü"},
      {"tekrarla", 14, "Orhun anahtar kelimesi"},
      {"kez", 14, "Orhun anahtar kelimesi"},
      {"sürece", 14, "Orhun anahtar kelimesi"},
      {"işlev", 14, "Orhun anahtar kelimesi"},
      {"döndür", 14, "Orhun anahtar kelimesi"},
      {"tip", 14, "Orhun anahtar kelimesi"},
      {"yeni", 14, "Orhun anahtar kelimesi"},
      {"benim", 14, "Orhun anahtar kelimesi"},
      {"ust", 14, "Orhun anahtar kelimesi"},
      {"deneme", 14, "Orhun anahtar kelimesi"},
      {"yakala", 14, "Orhun anahtar kelimesi"},
      {"kır", 14, "Orhun anahtar kelimesi"},
      {"devam", 14, "Orhun anahtar kelimesi"},
      {"dahil_et", 14, "Orhun anahtar kelimesi"},
      {"için", 14, "Orhun anahtar kelimesi"},
      {"içinde", 14, "Orhun anahtar kelimesi"},
      {"yaz", 3, "Yerleşik çıktı işlevi"},
      {"yazdır", 3, "Yerleşik çıktı işlevi"},
      {"sor", 3, "Yerleşik girdi işlevi"},
      {"oku", 3, "Yerleşik girdi işlevi"},
      {"aralik", 3, "Yerleşik aralık işlevi"},
      {"aralık", 3, "Yerleşik aralık işlevi"},
      {"ilk", 3, "Yerleşik koleksiyon işlevi"},
      {"son", 3, "Yerleşik koleksiyon işlevi"},
      {"bos_mu", 3, "Yerleşik koleksiyon işlevi"},
      {"boş_mu", 3, "Yerleşik koleksiyon işlevi"},
      {"dolu_mu", 3, "Yerleşik koleksiyon işlevi"},
      {"uzunluk", 3, "Yerleşik koleksiyon işlevi"},
      {"haritala", 3, "Orhun koleksiyon yardımcısı"},
      {"filtrele", 3, "Orhun koleksiyon yardımcısı"},
      {"katla", 3, "Orhun koleksiyon yardımcısı"},
      {"benzersiz", 3, "Orhun koleksiyon yardımcısı"},
      {"numaralandir", 3, "Orhun koleksiyon yardımcısı"},
      {"eslestir", 3, "Orhun koleksiyon yardımcısı"},
      {"eşleştir", 3, "Orhun koleksiyon yardımcısı"},
      {"metne_cevir", 3, "Yerleşik dönüşüm işlevi"},
      {"sayiya_cevir", 3, "Yerleşik dönüşüm işlevi"},
      {"bekle", 3, "Yerleşik zaman işlevi"},
      {"json", 9, "Yerleşik modül"},
      {"metin", 9, "Yerleşik modül"},
      {"dosya", 9, "Yerleşik modül"},
      {"regex", 9, "Yerleşik modül"},
      {"gorev", 9, "Yerleşik modül"},
      {"veritabani", 9, "Yerleşik modül"}};
  std::ostringstream ss;
  ss << "{\"isIncomplete\":false,\"items\":[";
  for (std::size_t i = 0; i < ogeler.size(); ++i) {
    if (i > 0) {
      ss << ",";
    }
    ss << "{\"label\":\"" << jsonKacis(ogeler[i].etiket)
       << "\",\"kind\":" << ogeler[i].tur << ",\"detail\":\""
       << jsonKacis(ogeler[i].ayrinti) << "\"}";
  }
  ss << "]}";
  return ss.str();
}

std::optional<std::string> lspJsonStringAlanBul(const std::string &json,
                                                const std::string &alan,
                                                std::size_t baslangic = 0) {
  const std::string anahtar = "\"" + alan + "\"";
  std::size_t p = json.find(anahtar, baslangic);
  if (p == std::string::npos) {
    return std::nullopt;
  }
  p = json.find(':', p + anahtar.size());
  if (p == std::string::npos) {
    return std::nullopt;
  }
  ++p;
  while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) {
    ++p;
  }
  if (p >= json.size() || json[p] != '"') {
    return std::nullopt;
  }
  ++p;

  std::string sonuc;
  bool kacis = false;
  while (p < json.size()) {
    const char c = json[p++];
    if (kacis) {
      switch (c) {
      case 'n':
        sonuc.push_back('\n');
        break;
      case 'r':
        sonuc.push_back('\r');
        break;
      case 't':
        sonuc.push_back('\t');
        break;
      case '\\':
        sonuc.push_back('\\');
        break;
      case '"':
        sonuc.push_back('"');
        break;
      default:
        sonuc.push_back(c);
        break;
      }
      kacis = false;
      continue;
    }
    if (c == '\\') {
      kacis = true;
      continue;
    }
    if (c == '"') {
      return sonuc;
    }
    sonuc.push_back(c);
  }
  return std::nullopt;
}

std::vector<int> lspJsonSayiAlanlariBul(const std::string &json,
                                        const std::string &alan) {
  std::vector<int> sonuc;
  const std::string anahtar = "\"" + alan + "\"";
  std::size_t pos = 0;
  while (true) {
    pos = json.find(anahtar, pos);
    if (pos == std::string::npos) {
      break;
    }
    std::size_t i = json.find(':', pos + anahtar.size());
    if (i == std::string::npos) {
      break;
    }
    ++i;
    while (i < json.size() &&
           std::isspace(static_cast<unsigned char>(json[i]))) {
      ++i;
    }
    std::size_t j = i;
    if (j < json.size() && (json[j] == '-' || json[j] == '+')) {
      ++j;
    }
    while (j < json.size() &&
           std::isdigit(static_cast<unsigned char>(json[j]))) {
      ++j;
    }
    if (j > i) {
      sonuc.push_back(std::stoi(json.substr(i, j - i)));
    }
    pos = j;
  }
  return sonuc;
}

std::string lspDiagnosticItemsJsonCikar(const std::string &tanilamaJson) {
  const std::size_t itemsPos = tanilamaJson.find("\"items\":");
  if (itemsPos == std::string::npos) {
    return "[]";
  }
  const std::size_t bas = tanilamaJson.find('[', itemsPos);
  const std::size_t son = tanilamaJson.rfind(']');
  if (bas == std::string::npos || son == std::string::npos || son < bas) {
    return "[]";
  }
  return tanilamaJson.substr(bas, son - bas + 1);
}

std::string lspDiagnosticCevapJson(const std::string &itemsJson) {
  return std::string("{\"kind\":\"full\",\"items\":") + itemsJson + "}";
}

bool lspSatirSutunOfsetBul(const std::string &metin, int satir, int sutun,
                           std::size_t *ofset) {
  if (satir < 0 || sutun < 0 || ofset == nullptr) {
    return false;
  }

  std::size_t satirBasi = 0;
  for (int mevcutSatir = 0; mevcutSatir < satir; ++mevcutSatir) {
    const std::size_t yeniSatir = metin.find('\n', satirBasi);
    if (yeniSatir == std::string::npos) {
      return false;
    }
    satirBasi = yeniSatir + 1;
  }

  std::size_t satirSonu = metin.find('\n', satirBasi);
  if (satirSonu == std::string::npos) {
    satirSonu = metin.size();
  }
  const std::size_t satirUzunlugu = satirSonu - satirBasi;
  if (static_cast<std::size_t>(sutun) > satirUzunlugu) {
    return false;
  }

  *ofset = satirBasi + static_cast<std::size_t>(sutun);
  return true;
}

bool lspAralikDegisikligiUygula(std::string &metin, int basSatir, int basSutun,
                                int bitSatir, int bitSutun,
                                const std::string &yeniMetin) {
  std::size_t bas = 0;
  std::size_t bit = 0;
  if (!lspSatirSutunOfsetBul(metin, basSatir, basSutun, &bas) ||
      !lspSatirSutunOfsetBul(metin, bitSatir, bitSutun, &bit) || bit < bas) {
    return false;
  }
  metin.replace(bas, bit - bas, yeniMetin);
  return true;
}

struct LspDidChangeParcasi {
  bool aralikVar = false;
  int basSatir = 0;
  int basSutun = 0;
  int bitSatir = 0;
  int bitSutun = 0;
  std::string text;
};

std::vector<LspDidChangeParcasi>
lspDidChangeParcalariniCoz(const std::string &mesaj) {
  std::vector<LspDidChangeParcasi> sonuc;
  const std::size_t alanPos = mesaj.find("\"contentChanges\"");
  if (alanPos == std::string::npos) {
    return sonuc;
  }
  const std::size_t diziBas = mesaj.find('[', alanPos);
  if (diziBas == std::string::npos) {
    return sonuc;
  }

  bool metinIcinde = false;
  bool kacis = false;
  int diziDerinligi = 1;
  int nesneDerinligi = 0;
  std::size_t nesneBas = std::string::npos;

  for (std::size_t i = diziBas + 1; i < mesaj.size() && diziDerinligi > 0;
       ++i) {
    const char c = mesaj[i];
    if (metinIcinde) {
      if (kacis) {
        kacis = false;
      } else if (c == '\\') {
        kacis = true;
      } else if (c == '"') {
        metinIcinde = false;
      }
      continue;
    }

    if (c == '"') {
      metinIcinde = true;
      continue;
    }
    if (c == '[') {
      ++diziDerinligi;
      continue;
    }
    if (c == ']') {
      --diziDerinligi;
      continue;
    }
    if (c == '{') {
      if (nesneDerinligi == 0) {
        nesneBas = i;
      }
      ++nesneDerinligi;
      continue;
    }
    if (c == '}') {
      if (nesneDerinligi <= 0) {
        continue;
      }
      --nesneDerinligi;
      if (nesneDerinligi != 0 || nesneBas == std::string::npos) {
        continue;
      }

      const std::string degisim = mesaj.substr(nesneBas, i - nesneBas + 1);
      const auto text = lspJsonStringAlanBul(degisim, "text");
      nesneBas = std::string::npos;
      if (!text.has_value()) {
        continue;
      }

      LspDidChangeParcasi parca;
      parca.text = *text;
      const std::size_t rangePos = degisim.find("\"range\"");
      if (rangePos != std::string::npos) {
        const auto satirlar =
            lspJsonSayiAlanlariBul(degisim.substr(rangePos), "line");
        const auto karakterler =
            lspJsonSayiAlanlariBul(degisim.substr(rangePos), "character");
        if (satirlar.size() >= 2 && karakterler.size() >= 2) {
          parca.aralikVar = true;
          parca.basSatir = satirlar[0];
          parca.basSutun = karakterler[0];
          parca.bitSatir = satirlar[1];
          parca.bitSutun = karakterler[1];
        }
      }
      sonuc.push_back(std::move(parca));
    }
  }

  return sonuc;
}

std::vector<std::string> metniSatirlaraBol(const std::string &metin) {
  std::vector<std::string> satirlar;
  std::istringstream ak(metin);
  for (std::string satir; std::getline(ak, satir);) {
    if (!satir.empty() && satir.back() == '\r') {
      satir.pop_back();
    }
    satirlar.push_back(std::move(satir));
  }
  if (satirlar.empty()) {
    satirlar.emplace_back("");
  }
  return satirlar;
}

std::optional<std::string> satirdanTanimAdiBul(const std::string &satir,
                                               const std::string &onEk) {
  if (satir.rfind(onEk, 0) != 0) {
    return std::nullopt;
  }
  std::size_t i = onEk.size();
  while (i < satir.size() &&
         std::isspace(static_cast<unsigned char>(satir[i]))) {
    ++i;
  }
  std::size_t j = i;
  while (j < satir.size()) {
    const unsigned char c = static_cast<unsigned char>(satir[j]);
    if (!(std::isalnum(c) || c == '_' || c >= 128)) {
      break;
    }
    ++j;
  }
  if (j <= i) {
    return std::nullopt;
  }
  return satir.substr(i, j - i);
}

std::string lspDocumentSymbolJson(const std::string &uri,
                                  const std::string &metin) {
  const auto satirlar = metniSatirlaraBol(metin);
  struct Sembol {
    std::string ad;
    int kind = 13;
    std::size_t satir = 0;
  };
  std::vector<Sembol> semboller;

  for (std::size_t i = 0; i < satirlar.size(); ++i) {
    const std::string trim = soldanBoslukKirp(sagaBoslukKirp(satirlar[i]));
    if (trim.empty() || trim[0] == '#') {
      continue;
    }
    if (auto ad = satirdanTanimAdiBul(trim, "işlev "); ad.has_value()) {
      semboller.push_back({*ad, 12, i});
      continue;
    }
    if (auto ad = satirdanTanimAdiBul(trim, "islev "); ad.has_value()) {
      semboller.push_back({*ad, 12, i});
      continue;
    }
    if (auto ad = satirdanTanimAdiBul(trim, "tip "); ad.has_value()) {
      semboller.push_back({*ad, 5, i});
      continue;
    }
    const std::size_t olsunPos = trim.find(" olsun ");
    const std::size_t esitPos = trim.find(" = ");
    std::size_t pos = std::string::npos;
    if (olsunPos != std::string::npos) {
      pos = olsunPos;
    } else if (esitPos != std::string::npos) {
      pos = esitPos;
    }
    if (pos != std::string::npos) {
      std::string ad = sagaBoslukKirp(trim.substr(0, pos));
      if (!ad.empty()) {
        semboller.push_back({ad, 13, i});
      }
    }
  }

  std::ostringstream ss;
  ss << "[";
  for (std::size_t i = 0; i < semboller.size(); ++i) {
    if (i > 0) {
      ss << ",";
    }
    ss << "{\"name\":\"" << jsonKacis(semboller[i].ad)
       << "\",\"kind\":" << semboller[i].kind << ",\"location\":{\"uri\":\""
       << jsonKacis(uri)
       << "\",\"range\":{\"start\":{\"line\":" << semboller[i].satir
       << ",\"character\":0},\"end\":{\"line\":" << semboller[i].satir
       << ",\"character\":1}}}}";
  }
  ss << "]";
  return ss.str();
}

std::string lspWorkspaceSymbolJson(
    const std::unordered_map<std::string, std::string> &acikBelgeler,
    const std::string &sorgu) {
  const std::string sorguLc = asciiKucuk(solaSagaKirp(sorgu));
  std::ostringstream ss;
  ss << "[";
  bool ilk = true;
  for (const auto &[uri, metin] : acikBelgeler) {
    const auto satirlar = metniSatirlaraBol(metin);
    for (std::size_t i = 0; i < satirlar.size(); ++i) {
      const std::string trim = soldanBoslukKirp(sagaBoslukKirp(satirlar[i]));
      if (trim.empty() || trim[0] == '#') {
        continue;
      }
      std::string ad;
      int kind = 13;
      if (auto v = satirdanTanimAdiBul(trim, "işlev "); v.has_value()) {
        ad = *v;
        kind = 12;
      } else if (auto v = satirdanTanimAdiBul(trim, "islev "); v.has_value()) {
        ad = *v;
        kind = 12;
      } else if (auto v = satirdanTanimAdiBul(trim, "tip "); v.has_value()) {
        ad = *v;
        kind = 5;
      } else {
        const std::size_t olsunPos = trim.find(" olsun ");
        const std::size_t esitPos = trim.find(" = ");
        const std::size_t pos =
            (olsunPos != std::string::npos) ? olsunPos : esitPos;
        if (pos != std::string::npos) {
          ad = sagaBoslukKirp(trim.substr(0, pos));
          kind = 13;
        }
      }
      if (ad.empty()) {
        continue;
      }
      if (!sorguLc.empty() &&
          asciiKucuk(ad).find(sorguLc) == std::string::npos) {
        continue;
      }
      if (!ilk) {
        ss << ",";
      }
      ilk = false;
      ss << "{\"name\":\"" << jsonKacis(ad) << "\",\"kind\":" << kind
         << ",\"location\":{\"uri\":\"" << jsonKacis(uri)
         << "\",\"range\":{\"start\":{\"line\":" << i
         << ",\"character\":0},\"end\":{\"line\":" << i
         << ",\"character\":1}}}}";
    }
  }
  ss << "]";
  return ss.str();
}

int hataMesajindanSayiBul(const std::string &mesaj,
                          const std::vector<std::string> &etiketler) {
  for (const auto &etiket : etiketler) {
    std::size_t p = mesaj.find(etiket);
    if (p == std::string::npos) {
      continue;
    }
    p += etiket.size();
    std::size_t j = p;
    while (j < mesaj.size() &&
           std::isdigit(static_cast<unsigned char>(mesaj[j]))) {
      ++j;
    }
    if (j == p) {
      continue;
    }
    return std::stoi(mesaj.substr(p, j - p));
  }
  return 0;
}

std::pair<int, int> hataMesajindanKonumBul(const std::string &mesaj) {
  const int satirHam =
      hataMesajindanSayiBul(mesaj, {"Satır ", "Satir ", "satır ", "satir "});
  const int sutunHam =
      hataMesajindanSayiBul(mesaj, {"Sütun ", "Sutun ", "sütun ", "sutun "});

  const int satir = satirHam > 0 ? satirHam - 1 : 0;
  const int sutun = sutunHam > 0 ? sutunHam - 1 : 0;
  return {satir, sutun};
}

std::string lspDiagnosticJson(const std::string &metin) {
  try {
    static_cast<void>(bytecodeDerle(metin));
    return "{\"kind\":\"full\",\"items\":[]}";
  } catch (const std::exception &ex) {
    const std::string mesaj = ex.what();
    const auto [satir, sutun] = hataMesajindanKonumBul(mesaj);
    std::ostringstream ss;
    ss << "{\"kind\":\"full\",\"items\":[{\"range\":{\"start\":{\"line\":"
       << satir << ",\"character\":" << sutun << "},\"end\":{\"line\":"
       << satir << ",\"character\":" << (sutun + 1)
       << "}},\"severity\":1,\"source\":\"orhun\",\"message\":"
          "\""
       << jsonKacis(mesaj) << "\"}]}";
    return ss.str();
  }
}

bool lspKelimeKarakteri(unsigned char c) {
  return std::isalnum(c) || c == '_' || c >= 128;
}

struct LspKelimeKonumu {
  std::string kelime;
  int satir = 0;
  int baslangic = 0;
  int bitis = 0;
};

std::optional<LspKelimeKonumu>
lspKelimeKonumuBul(const std::vector<std::string> &satirlar, int satirNo,
                   int karakterNo) {
  if (satirNo < 0 || static_cast<std::size_t>(satirNo) >= satirlar.size()) {
    return std::nullopt;
  }
  const std::string &satir = satirlar[static_cast<std::size_t>(satirNo)];
  if (satir.empty()) {
    return std::nullopt;
  }
  if (karakterNo < 0) {
    karakterNo = 0;
  }
  std::size_t idx =
      static_cast<std::size_t>(std::min<int>(karakterNo, satir.size() - 1));
  if (!lspKelimeKarakteri(static_cast<unsigned char>(satir[idx]))) {
    if (idx > 0 &&
        lspKelimeKarakteri(static_cast<unsigned char>(satir[idx - 1]))) {
      --idx;
    } else {
      return std::nullopt;
    }
  }
  std::size_t sol = idx;
  while (sol > 0 &&
         lspKelimeKarakteri(static_cast<unsigned char>(satir[sol - 1]))) {
    --sol;
  }
  std::size_t sag = idx;
  while (sag + 1 < satir.size() &&
         lspKelimeKarakteri(static_cast<unsigned char>(satir[sag + 1]))) {
    ++sag;
  }
  if (sag < sol) {
    return std::nullopt;
  }
  return LspKelimeKonumu{satir.substr(sol, sag - sol + 1), satirNo,
                         static_cast<int>(sol), static_cast<int>(sag + 1)};
}

bool lspTanimSatiriMi(const std::string &trim, const std::string &aranan) {
  return trim.rfind("işlev " + aranan, 0) == 0 ||
         trim.rfind("islev " + aranan, 0) == 0 ||
         trim.rfind("tip " + aranan, 0) == 0 ||
         trim.rfind(aranan + " olsun ", 0) == 0 ||
         trim.rfind(aranan + " = ", 0) == 0;
}

std::optional<std::size_t>
lspTanimSatiriBul(const std::vector<std::string> &satirlar,
                  const std::string &aranan) {
  for (std::size_t i = 0; i < satirlar.size(); ++i) {
    const std::string trim = soldanBoslukKirp(sagaBoslukKirp(satirlar[i]));
    if (lspTanimSatiriMi(trim, aranan)) {
      return i;
    }
  }
  return std::nullopt;
}

std::vector<std::string> lspParametreleriBol(const std::string &ham) {
  std::vector<std::string> parametreler;
  std::istringstream ak(ham);
  for (std::string parca; std::getline(ak, parca, ',');) {
    std::string ad = solaSagaKirp(parca);
    const std::size_t olsunPos = ad.find(" olsun ");
    const std::size_t esitPos = ad.find('=');
    std::size_t kesim = std::string::npos;
    if (olsunPos != std::string::npos) {
      kesim = olsunPos;
    } else if (esitPos != std::string::npos) {
      kesim = esitPos;
    }
    if (kesim != std::string::npos) {
      ad = sagaBoslukKirp(ad.substr(0, kesim));
    }
    if (!ad.empty()) {
      parametreler.push_back(ad);
    }
  }
  return parametreler;
}

bool lspIslevImzasiCoz(const std::string &trim, const std::string &ad,
                       std::string *etiket,
                       std::vector<std::string> *parametreler) {
  const std::string onEk1 = "işlev " + ad;
  const std::string onEk2 = "islev " + ad;
  std::size_t imzaBaslangic = std::string::npos;
  if (trim.rfind(onEk1, 0) == 0) {
    imzaBaslangic = onEk1.size();
  } else if (trim.rfind(onEk2, 0) == 0) {
    imzaBaslangic = onEk2.size();
  } else {
    return false;
  }
  const std::size_t ac = trim.find('(', imzaBaslangic);
  const std::size_t kap = trim.find(')', ac == std::string::npos ? 0 : ac + 1);
  if (ac == std::string::npos || kap == std::string::npos || kap < ac) {
    return false;
  }
  const std::string hamParametre = trim.substr(ac + 1, kap - ac - 1);
  if (etiket != nullptr) {
    *etiket = ad + "(" + hamParametre + ")";
  }
  if (parametreler != nullptr) {
    *parametreler = lspParametreleriBol(hamParametre);
  }
  return true;
}

bool lspYerlesikImzasiCoz(const std::string &ad, std::string *etiket,
                          std::vector<std::string> *parametreler) {
  struct Imza {
    const char *ad;
    const char *etiket;
    std::vector<std::string> parametreler;
  };

  static const std::vector<Imza> imzalar = {
      {"yaz", "yaz(deger)", {"deger"}},
      {"yazdır", "yazdır(deger)", {"deger"}},
      {"sor", "sor(soru)", {"soru"}},
      {"oku", "oku(soru)", {"soru"}},
      {"aralik", "aralik([baslangic], bitis, [adim])",
       {"[baslangic]", "bitis", "[adim]"}},
      {"aralık", "aralık([baslangic], bitis, [adim])",
       {"[baslangic]", "bitis", "[adim]"}},
      {"ilk", "ilk(liste, [yedek])", {"liste", "[yedek]"}},
      {"son", "son(liste, [yedek])", {"liste", "[yedek]"}},
      {"bos_mu", "bos_mu(deger)", {"deger"}},
      {"boş_mu", "boş_mu(deger)", {"deger"}},
      {"dolu_mu", "dolu_mu(deger)", {"deger"}},
      {"uzunluk", "uzunluk(deger)", {"deger"}},
      {"haritala", "haritala(liste, donusturucu)",
       {"liste", "donusturucu"}},
      {"filtrele", "filtrele(liste, kosul)", {"liste", "kosul"}},
      {"katla", "katla(liste, baslangic, birlestirici)",
       {"liste", "baslangic", "birlestirici"}},
      {"benzersiz", "benzersiz(liste)", {"liste"}},
      {"numaralandir", "numaralandir(liste, [baslangic])",
       {"liste", "[baslangic]"}},
      {"eslestir", "eslestir(sol, sag)", {"sol", "sag"}},
      {"eşleştir", "eşleştir(sol, sag)", {"sol", "sag"}},
      {"metne_cevir", "metne_cevir(deger)", {"deger"}},
      {"sayiya_cevir", "sayiya_cevir(metin)", {"metin"}},
      {"bekle", "bekle(saniye)", {"saniye"}}};

  for (const Imza &imza : imzalar) {
    if (ad != imza.ad) {
      continue;
    }
    if (etiket != nullptr) {
      *etiket = imza.etiket;
    }
    if (parametreler != nullptr) {
      *parametreler = imza.parametreler;
    }
    return true;
  }
  return false;
}

std::string lspHoverJson(const std::string &metin, int satirNo, int karakterNo) {
  const auto satirlar = metniSatirlaraBol(metin);
  const auto konum = lspKelimeKonumuBul(satirlar, satirNo, karakterNo);
  if (!konum.has_value()) {
    return "null";
  }

  std::string aciklama = "Sembol: `" + konum->kelime + "`";
  const auto tanimSatiri = lspTanimSatiriBul(satirlar, konum->kelime);
  if (tanimSatiri.has_value()) {
    const std::string trim =
        soldanBoslukKirp(sagaBoslukKirp(satirlar[*tanimSatiri]));
    std::string islevEtiketi;
    if (lspIslevImzasiCoz(trim, konum->kelime, &islevEtiketi, nullptr)) {
      aciklama = "Islev: `" + islevEtiketi + "`";
    } else if (trim.rfind("tip " + konum->kelime, 0) == 0) {
      aciklama = "Tip: `" + konum->kelime + "`";
    } else if (trim.rfind(konum->kelime + " olsun ", 0) == 0 ||
               trim.rfind(konum->kelime + " = ", 0) == 0) {
      aciklama = "Degisken: `" + konum->kelime + "`";
    }
  }

  std::ostringstream ss;
  ss << "{\"contents\":{\"kind\":\"markdown\",\"value\":\""
     << jsonKacis(aciklama) << "\"},\"range\":{\"start\":{\"line\":"
     << konum->satir << ",\"character\":" << konum->baslangic
     << "},\"end\":{\"line\":" << konum->satir
     << ",\"character\":" << konum->bitis << "}}}";
  return ss.str();
}

std::optional<std::string> lspCagriAdiBul(const std::string &satir,
                                          int karakterNo,
                                          int *aktifParametre) {
  if (satir.empty()) {
    return std::nullopt;
  }
  const int konum = std::clamp(karakterNo, 0, static_cast<int>(satir.size()));
  int derinlik = 0;
  int acilis = -1;
  for (int i = konum - 1; i >= 0; --i) {
    const char c = satir[static_cast<std::size_t>(i)];
    if (c == ')') {
      ++derinlik;
      continue;
    }
    if (c == '(') {
      if (derinlik == 0) {
        acilis = i;
        break;
      }
      --derinlik;
    }
  }
  if (acilis < 0) {
    return std::nullopt;
  }

  int son = acilis - 1;
  while (son >= 0 &&
         std::isspace(static_cast<unsigned char>(satir[static_cast<std::size_t>(
             son)]))) {
    --son;
  }
  if (son < 0) {
    return std::nullopt;
  }
  int bas = son;
  while (bas >= 0 && lspKelimeKarakteri(static_cast<unsigned char>(
                         satir[static_cast<std::size_t>(bas)]))) {
    --bas;
  }
  ++bas;
  if (bas > son) {
    return std::nullopt;
  }

  int parametre = 0;
  derinlik = 0;
  for (int i = acilis + 1; i < konum; ++i) {
    const char c = satir[static_cast<std::size_t>(i)];
    if (c == '(') {
      ++derinlik;
      continue;
    }
    if (c == ')') {
      if (derinlik > 0) {
        --derinlik;
      }
      continue;
    }
    if (c == ',' && derinlik == 0) {
      ++parametre;
    }
  }
  if (aktifParametre != nullptr) {
    *aktifParametre = parametre;
  }
  return satir.substr(static_cast<std::size_t>(bas),
                      static_cast<std::size_t>(son - bas + 1));
}

std::string lspSignatureHelpJson(const std::string &metin, int satirNo,
                                 int karakterNo) {
  const auto satirlar = metniSatirlaraBol(metin);
  if (satirNo < 0 || static_cast<std::size_t>(satirNo) >= satirlar.size()) {
    return "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}";
  }
  int aktifParametre = 0;
  const auto cagriAdi =
      lspCagriAdiBul(satirlar[static_cast<std::size_t>(satirNo)], karakterNo,
                     &aktifParametre);
  if (!cagriAdi.has_value() || cagriAdi->empty()) {
    return "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}";
  }

  std::string etiket = *cagriAdi + "(...)";
  std::vector<std::string> parametreler;
  bool imzaBulundu = false;
  for (const std::string &satir : satirlar) {
    const std::string trim = soldanBoslukKirp(sagaBoslukKirp(satir));
    if (lspIslevImzasiCoz(trim, *cagriAdi, &etiket, &parametreler)) {
      imzaBulundu = true;
      break;
    }
  }
  if (!imzaBulundu) {
    lspYerlesikImzasiCoz(*cagriAdi, &etiket, &parametreler);
  }

  if (aktifParametre < 0) {
    aktifParametre = 0;
  }
  if (!parametreler.empty() &&
      aktifParametre >= static_cast<int>(parametreler.size())) {
    aktifParametre = static_cast<int>(parametreler.size() - 1);
  }

  std::ostringstream ss;
  ss << "{\"signatures\":[{\"label\":\"" << jsonKacis(etiket)
     << "\",\"parameters\":[";
  for (std::size_t i = 0; i < parametreler.size(); ++i) {
    if (i > 0) {
      ss << ",";
    }
    ss << "{\"label\":\"" << jsonKacis(parametreler[i]) << "\"}";
  }
  ss << "]}],\"activeSignature\":0,\"activeParameter\":" << aktifParametre
     << "}";
  return ss.str();
}

std::vector<LspKelimeKonumu>
lspKelimeKonumlariniBul(const std::vector<std::string> &satirlar,
                        const std::string &aranan) {
  std::vector<LspKelimeKonumu> konumlar;
  if (aranan.empty()) {
    return konumlar;
  }
  for (std::size_t satirNo = 0; satirNo < satirlar.size(); ++satirNo) {
    const std::string &satir = satirlar[satirNo];
    std::size_t pos = 0;
    while (true) {
      pos = satir.find(aranan, pos);
      if (pos == std::string::npos) {
        break;
      }
      const bool solSinir =
          pos == 0 ||
          !lspKelimeKarakteri(static_cast<unsigned char>(satir[pos - 1]));
      const std::size_t son = pos + aranan.size();
      const bool sagSinir =
          son >= satir.size() ||
          !lspKelimeKarakteri(static_cast<unsigned char>(satir[son]));
      if (solSinir && sagSinir) {
        konumlar.push_back(
            LspKelimeKonumu{aranan, static_cast<int>(satirNo),
                            static_cast<int>(pos), static_cast<int>(son)});
      }
      pos = son;
    }
  }
  return konumlar;
}

std::string lspReferencesJson(
    const std::unordered_map<std::string, std::string> &acikBelgeler,
    const std::string &hedefUri, int satirNo, int karakterNo,
    bool includeDeclaration) {
  const auto itHedef = acikBelgeler.find(hedefUri);
  if (itHedef == acikBelgeler.end()) {
    return "[]";
  }
  const auto satirlar = metniSatirlaraBol(itHedef->second);
  const auto konum = lspKelimeKonumuBul(satirlar, satirNo, karakterNo);
  if (!konum.has_value()) {
    return "[]";
  }
  const std::string aranan = konum->kelime;

  std::ostringstream ss;
  ss << "[";
  bool ilk = true;
  for (const auto &[uri, metin] : acikBelgeler) {
    const auto docSatirlari = metniSatirlaraBol(metin);
    const auto bulunanlar = lspKelimeKonumlariniBul(docSatirlari, aranan);
    for (const auto &k : bulunanlar) {
      const std::string trim = soldanBoslukKirp(
          sagaBoslukKirp(docSatirlari[static_cast<std::size_t>(k.satir)]));
      if (!includeDeclaration && lspTanimSatiriMi(trim, aranan)) {
        continue;
      }
      if (!ilk) {
        ss << ",";
      }
      ilk = false;
      ss << "{\"uri\":\"" << jsonKacis(uri)
         << "\",\"range\":{\"start\":{\"line\":" << k.satir
         << ",\"character\":" << k.baslangic
         << "},\"end\":{\"line\":" << k.satir << ",\"character\":" << k.bitis
         << "}}}";
    }
  }
  ss << "]";
  return ss.str();
}

bool lspKimlikGecerliMi(const std::string &ad) {
  if (ad.empty()) {
    return false;
  }
  for (unsigned char c : ad) {
    if (!lspKelimeKarakteri(c)) {
      return false;
    }
  }
  return true;
}

std::string lspRenameJson(
    const std::unordered_map<std::string, std::string> &acikBelgeler,
    const std::string &hedefUri, int satirNo, int karakterNo,
    const std::string &yeniAd) {
  if (!lspKimlikGecerliMi(yeniAd)) {
    return "null";
  }
  const auto itHedef = acikBelgeler.find(hedefUri);
  if (itHedef == acikBelgeler.end()) {
    return "null";
  }
  const auto satirlar = metniSatirlaraBol(itHedef->second);
  const auto konum = lspKelimeKonumuBul(satirlar, satirNo, karakterNo);
  if (!konum.has_value() || konum->kelime.empty()) {
    return "null";
  }
  const std::string eskiAd = konum->kelime;

  std::ostringstream ss;
  ss << "{\"changes\":{";
  bool ilkBelge = true;
  for (const auto &[uri, metin] : acikBelgeler) {
    const auto docSatirlari = metniSatirlaraBol(metin);
    const auto bulunanlar = lspKelimeKonumlariniBul(docSatirlari, eskiAd);
    if (bulunanlar.empty()) {
      continue;
    }
    if (!ilkBelge) {
      ss << ",";
    }
    ilkBelge = false;
    ss << "\"" << jsonKacis(uri) << "\":[";
    for (std::size_t i = 0; i < bulunanlar.size(); ++i) {
      const auto &k = bulunanlar[i];
      if (i > 0) {
        ss << ",";
      }
      ss << "{\"range\":{\"start\":{\"line\":" << k.satir
         << ",\"character\":" << k.baslangic
         << "},\"end\":{\"line\":" << k.satir << ",\"character\":" << k.bitis
         << "}},\"newText\":\"" << jsonKacis(yeniAd) << "\"}";
    }
    ss << "]";
  }
  ss << "}}";
  return ss.str();
}

std::string lspDefinitionCokluJson(
    const std::unordered_map<std::string, std::string> &acikBelgeler,
    const std::string &hedefUri, int satirNo, int karakterNo) {
  const auto itHedef = acikBelgeler.find(hedefUri);
  if (itHedef == acikBelgeler.end()) {
    return "[]";
  }
  const auto satirlar = metniSatirlaraBol(itHedef->second);
  const auto konum = lspKelimeKonumuBul(satirlar, satirNo, karakterNo);
  if (!konum.has_value() || konum->kelime.empty()) {
    return "[]";
  }
  const std::string aranan = konum->kelime;

  std::ostringstream ss;
  ss << "[";
  bool ilk = true;
  for (const auto &[uri, metin] : acikBelgeler) {
    const auto docSatirlar = metniSatirlaraBol(metin);
    for (std::size_t i = 0; i < docSatirlar.size(); ++i) {
      const std::string trim =
          soldanBoslukKirp(sagaBoslukKirp(docSatirlar[i]));
      if (!lspTanimSatiriMi(trim, aranan)) {
        continue;
      }
      if (!ilk) {
        ss << ",";
      }
      ilk = false;
      ss << "{\"uri\":\"" << jsonKacis(uri)
         << "\",\"range\":{\"start\":{\"line\":" << i
         << ",\"character\":0},\"end\":{\"line\":" << i
         << ",\"character\":1}}}";
    }
  }
  ss << "]";
  return ss.str();
}

std::string lspDosyaYolundanUri(const std::filesystem::path &dosyaYolu) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path tam = fs::weakly_canonical(dosyaYolu, ec);
  if (ec) {
    tam = fs::absolute(dosyaYolu, ec);
  }
  std::string yol = tam.generic_string();
#ifdef _WIN32
  if (yol.size() >= 2 && yol[1] == ':') {
    return "file:///" + yol;
  }
#endif
  return "file://" + yol;
}

void lspWorkspaceBelgeleriniYukle(
    const std::filesystem::path &workspaceRoot,
    std::unordered_map<std::string, std::string> &acikBelgeler) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::exists(workspaceRoot, ec) || ec || !fs::is_directory(workspaceRoot, ec)) {
    return;
  }
  fs::recursive_directory_iterator son;
  for (fs::recursive_directory_iterator it(
           workspaceRoot, fs::directory_options::skip_permission_denied);
       it != son; ++it) {
    const fs::path yol = it->path();
    if (it->is_directory(ec)) {
      if (!ec) {
        const std::string ad = asciiKucuk(yol.filename().string());
        if (ad == ".git" || ad == "build" || ad == "coverage" || ad == "artifacts") {
          it.disable_recursion_pending();
        }
      }
      continue;
    }
    if (!it->is_regular_file(ec) || ec) {
      continue;
    }
    if (yol.extension() != ".oh") {
      continue;
    }
    const std::string uri = lspDosyaYolundanUri(yol);
    if (acikBelgeler.find(uri) != acikBelgeler.end()) {
      continue;
    }
    try {
      acikBelgeler.emplace(uri, dosyaOku(yol.string()));
    } catch (...) {
      // Ignore unreadable workspace files; LSP should continue.
    }
  }
}

void lspBildirimYaz(std::ostream &out, const std::string &method,
                    const std::string &paramsJson) {
  const std::string yuk = std::string("{\"jsonrpc\":\"2.0\",\"method\":\"") +
                          method + "\",\"params\":" + paramsJson + "}";
  out << "Content-Length: " << yuk.size() << "\r\n\r\n" << yuk;
  out.flush();
}

int komutLsp(bool stdioModu, const std::string &workspaceRoot) {
  (void)stdioModu;
  namespace fs = std::filesystem;
  fs::path kok = workspaceRoot.empty() ? fs::current_path() : fs::path(workspaceRoot);
  std::error_code ec;
  if (!fs::exists(kok, ec) || ec || !fs::is_directory(kok, ec)) {
    throw std::runtime_error(
        "Hata: --workspace-root gecersiz veya erisilemez: " + kok.string());
  }
  bool shutdownIstendi = false;
  std::unordered_map<std::string, std::string> acikBelgeler;
  std::unordered_map<std::string, std::string> tanilamaOnbellegi;
  lspWorkspaceBelgeleriniYukle(kok, acikBelgeler);
  auto tanilamaYayinla = [&](const std::string &uri, bool yenidenHesapla) {
    auto belge = acikBelgeler.find(uri);
    const std::string metin = belge == acikBelgeler.end() ? "" : belge->second;
    std::string itemsJson;
    if (!yenidenHesapla) {
      const auto cache = tanilamaOnbellegi.find(uri);
      if (cache != tanilamaOnbellegi.end()) {
        itemsJson = cache->second;
      }
    }
    if (itemsJson.empty() && (yenidenHesapla || tanilamaOnbellegi.find(uri) ==
                                                  tanilamaOnbellegi.end())) {
      itemsJson = lspDiagnosticItemsJsonCikar(lspDiagnosticJson(metin));
      tanilamaOnbellegi[uri] = itemsJson;
    }
    if (itemsJson.empty()) {
      itemsJson = "[]";
    }
    lspBildirimYaz(std::cout, "textDocument/publishDiagnostics",
                   "{\"uri\":\"" + jsonKacis(uri) +
                       "\",\"diagnostics\":" + itemsJson + "}");
  };
  while (true) {
    std::optional<std::string> gelen = lspMesajOku(std::cin);
    if (!gelen.has_value()) {
      break;
    }
    const std::string &mesaj = *gelen;
    const std::optional<std::string> idToken = lspIdTokenBul(mesaj);

    if (lspMethodMu(mesaj, "initialize")) {
      if (idToken.has_value()) {
        lspYanitYaz(std::cout, *idToken,
                    "{\"capabilities\":{\"textDocumentSync\":2,"
                    "\"completionProvider\":{\"resolveProvider\":false},"
                    "\"hoverProvider\":true,"
                    "\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\"]},"
                    "\"definitionProvider\":true,"
                    "\"referencesProvider\":true,"
                    "\"renameProvider\":{\"prepareProvider\":false},"
                    "\"documentSymbolProvider\":true,"
                    "\"workspaceSymbolProvider\":true,"
                    "\"diagnosticProvider\":{\"interFileDependencies\":false,"
                    "\"workspaceDiagnostics\":false}}}");
      }
      continue;
    }
    if (lspMethodMu(mesaj, "textDocument/didOpen")) {
      auto uri = lspJsonStringAlanBul(mesaj, "uri");
      auto text = lspJsonStringAlanBul(mesaj, "text");
      if (uri.has_value() && text.has_value()) {
        acikBelgeler[*uri] = *text;
        tanilamaYayinla(*uri, true);
      }
      continue;
    }
    if (lspMethodMu(mesaj, "textDocument/didChange")) {
      auto uri = lspJsonStringAlanBul(mesaj, "uri");
      if (uri.has_value()) {
        std::string guncelMetin = acikBelgeler[*uri];
        bool degisimVar = false;
        const auto degisiklikler = lspDidChangeParcalariniCoz(mesaj);
        if (!degisiklikler.empty()) {
          for (const auto &degisim : degisiklikler) {
            if (degisim.aralikVar) {
              const bool uygulandi = lspAralikDegisikligiUygula(
                  guncelMetin, degisim.basSatir, degisim.basSutun,
                  degisim.bitSatir, degisim.bitSutun, degisim.text);
              if (uygulandi) {
                degisimVar = true;
                continue;
              }
            }
            if (guncelMetin != degisim.text) {
              guncelMetin = degisim.text;
              degisimVar = true;
            }
          }
        } else {
          auto text = lspJsonStringAlanBul(mesaj, "text");
          if (text.has_value() && guncelMetin != *text) {
            guncelMetin = *text;
            degisimVar = true;
          }
        }

        acikBelgeler[*uri] = guncelMetin;
        tanilamaYayinla(*uri, degisimVar);
      }
      continue;
    }
    if (lspMethodMu(mesaj, "initialized")) {
      continue;
    }
    if (lspMethodMu(mesaj, "shutdown")) {
      shutdownIstendi = true;
      if (idToken.has_value()) {
        lspYanitYaz(std::cout, *idToken, "null");
      }
      continue;
    }
    if (lspMethodMu(mesaj, "exit")) {
      return shutdownIstendi ? 0 : 1;
    }
    if (lspMethodMu(mesaj, "textDocument/completion")) {
      if (idToken.has_value()) {
        lspYanitYaz(std::cout, *idToken, lspTamamlamaSonucuJson());
      }
      continue;
    }
    if (lspMethodMu(mesaj, "textDocument/documentSymbol")) {
      if (idToken.has_value()) {
        const auto uri = lspJsonStringAlanBul(mesaj, "uri");
        if (!uri.has_value()) {
          lspYanitYaz(std::cout, *idToken, "[]");
          continue;
        }
        const auto it = acikBelgeler.find(*uri);
        const std::string metin = it == acikBelgeler.end() ? "" : it->second;
        lspYanitYaz(std::cout, *idToken, lspDocumentSymbolJson(*uri, metin));
      }
      continue;
    }
    if (lspMethodMu(mesaj, "workspace/symbol")) {
      if (idToken.has_value()) {
        const auto sorgu = lspJsonStringAlanBul(mesaj, "query");
        lspYanitYaz(std::cout, *idToken,
                    lspWorkspaceSymbolJson(acikBelgeler, sorgu.value_or("")));
      }
      continue;
    }
    if (lspMethodMu(mesaj, "textDocument/diagnostic")) {
      if (idToken.has_value()) {
        const auto uri = lspJsonStringAlanBul(mesaj, "uri");
        if (!uri.has_value()) {
          lspYanitYaz(std::cout, *idToken, "{\"kind\":\"full\",\"items\":[]}");
          continue;
        }
        auto cache = tanilamaOnbellegi.find(*uri);
        if (cache == tanilamaOnbellegi.end()) {
          const auto it = acikBelgeler.find(*uri);
          const std::string metin = it == acikBelgeler.end() ? "" : it->second;
          tanilamaOnbellegi[*uri] =
              lspDiagnosticItemsJsonCikar(lspDiagnosticJson(metin));
          cache = tanilamaOnbellegi.find(*uri);
        }
        lspYanitYaz(std::cout, *idToken,
                    lspDiagnosticCevapJson(cache->second));
      }
      continue;
    }
    if (lspMethodMu(mesaj, "textDocument/definition")) {
      if (idToken.has_value()) {
        const auto uri = lspJsonStringAlanBul(mesaj, "uri");
        const auto satirlar = lspJsonSayiAlanlariBul(mesaj, "line");
        const auto karakterler = lspJsonSayiAlanlariBul(mesaj, "character");
        if (!uri.has_value() || satirlar.empty() || karakterler.empty()) {
          lspYanitYaz(std::cout, *idToken, "[]");
          continue;
        }
        lspYanitYaz(std::cout, *idToken,
                    lspDefinitionCokluJson(acikBelgeler, *uri,
                                           satirlar.front(),
                                           karakterler.front()));
      }
      continue;
    }
    if (lspMethodMu(mesaj, "textDocument/hover")) {
      if (idToken.has_value()) {
        const auto uri = lspJsonStringAlanBul(mesaj, "uri");
        const auto satirlar = lspJsonSayiAlanlariBul(mesaj, "line");
        const auto karakterler = lspJsonSayiAlanlariBul(mesaj, "character");
        if (!uri.has_value() || satirlar.empty() || karakterler.empty()) {
          lspYanitYaz(std::cout, *idToken, "null");
          continue;
        }
        const auto it = acikBelgeler.find(*uri);
        const std::string metin = it == acikBelgeler.end() ? "" : it->second;
        lspYanitYaz(std::cout, *idToken,
                    lspHoverJson(metin, satirlar.front(), karakterler.front()));
      }
      continue;
    }
    if (lspMethodMu(mesaj, "textDocument/signatureHelp")) {
      if (idToken.has_value()) {
        const auto uri = lspJsonStringAlanBul(mesaj, "uri");
        const auto satirlar = lspJsonSayiAlanlariBul(mesaj, "line");
        const auto karakterler = lspJsonSayiAlanlariBul(mesaj, "character");
        if (!uri.has_value() || satirlar.empty() || karakterler.empty()) {
          lspYanitYaz(
              std::cout, *idToken,
              "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}");
          continue;
        }
        const auto it = acikBelgeler.find(*uri);
        const std::string metin = it == acikBelgeler.end() ? "" : it->second;
        lspYanitYaz(std::cout, *idToken,
                    lspSignatureHelpJson(metin, satirlar.front(),
                                         karakterler.front()));
      }
      continue;
    }
    if (lspMethodMu(mesaj, "textDocument/references")) {
      if (idToken.has_value()) {
        const auto uri = lspJsonStringAlanBul(mesaj, "uri");
        const auto satirlar = lspJsonSayiAlanlariBul(mesaj, "line");
        const auto karakterler = lspJsonSayiAlanlariBul(mesaj, "character");
        const bool includeDeclaration =
            mesaj.find("\"includeDeclaration\":false") == std::string::npos;
        if (!uri.has_value() || satirlar.empty() || karakterler.empty()) {
          lspYanitYaz(std::cout, *idToken, "[]");
          continue;
        }
        lspYanitYaz(
            std::cout, *idToken,
            lspReferencesJson(acikBelgeler, *uri, satirlar.front(),
                              karakterler.front(), includeDeclaration));
      }
      continue;
    }
    if (lspMethodMu(mesaj, "textDocument/rename")) {
      if (idToken.has_value()) {
        const auto uri = lspJsonStringAlanBul(mesaj, "uri");
        const auto satirlar = lspJsonSayiAlanlariBul(mesaj, "line");
        const auto karakterler = lspJsonSayiAlanlariBul(mesaj, "character");
        const auto yeniAd = lspJsonStringAlanBul(mesaj, "newName");
        if (!uri.has_value() || satirlar.empty() || karakterler.empty() ||
            !yeniAd.has_value()) {
          lspYanitYaz(std::cout, *idToken, "null");
          continue;
        }
        lspYanitYaz(std::cout, *idToken,
                    lspRenameJson(acikBelgeler, *uri, satirlar.front(),
                                  karakterler.front(), *yeniAd));
      }
      continue;
    }
    if (idToken.has_value()) {
      lspYanitYaz(std::cout, *idToken, "null");
    }
  }
  return 0;
}

} // namespace

int main(int argc, char *argv[]) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif

  try {
    std::optional<bool> turkceKatiCli;
    int yazma = 1;
    for (int i = 1; i < argc; ++i) {
      const std::string secenek = argv[i];
      if (secenek == "--turkce-kati") {
        turkceKatiCli = true;
        continue;
      }
      if (secenek.rfind("--turkce-kati=", 0) == 0) {
        std::string deger = secenek.substr(14);
        std::transform(
            deger.begin(), deger.end(), deger.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (deger == "1" || deger == "true" || deger == "on" ||
            deger == "evet") {
          turkceKatiCli = true;
          continue;
        }
        if (deger == "0" || deger == "false" || deger == "off" ||
            deger == "hayir" || deger == "no") {
          turkceKatiCli = false;
          continue;
        }
        throw std::runtime_error(
            "Hata: --turkce-kati yalnizca true/false (veya 1/0) alir.");
      }
      argv[yazma++] = argv[i];
    }
    argc = yazma;

    const bool turkceKatiEtkin =
        turkceKatiCli.value_or(doctorOrtamDegiskeniAcik("ORHUN_TURKCE_KATI"));
    Lexer::setTurkceKatiVarsayilan(turkceKatiEtkin);

    auto dahiliKomutMu = [](const std::string &deger) {
      return deger == "fmt" || deger == "lex" || deger == "tokenler" ||
             deger == "parse" || deger == "ast" || deger == "baytkod" ||
             deger == "bytecode" ||
             deger == "paket" || deger == "vm" || deger == "vm-kati" ||
             deger == "yorumla" ||
             deger == "obc" || deger == "derle" || deger == "hiz" ||
             deger == "lint" || deger == "lsp" || deger == "doctor" ||
             deger == "surum";
    };

    if (argc < 2) {
      if (gomuluPaketiCalistir(argv[0])) {
        return 0;
      }
      return replCalistir();
    }

    const std::string komut = argv[1];
    if (komut == "lex" || komut == "tokenler") {
      if (argc < 3) {
        throw std::runtime_error("Hata: lex komutu icin dosya adi bekleniyor.");
      }
      bool jsonCikti = false;
      for (int i = 3; i < argc; ++i) {
        const std::string secenek = argv[i];
        if (secenek == "--json") {
          jsonCikti = true;
          continue;
        }
        throw std::runtime_error(
            "Hata: lex secenekleri yalnizca '--json' olabilir.");
      }
      return komutLex(argv[2], jsonCikti);
    }

    if (komut == "parse" || komut == "ast") {
      if (argc < 3) {
        throw std::runtime_error(
            "Hata: parse komutu icin dosya adi bekleniyor.");
      }
      bool jsonCikti = false;
      for (int i = 3; i < argc; ++i) {
        const std::string secenek = argv[i];
        if (secenek == "--json") {
          jsonCikti = true;
          continue;
        }
        throw std::runtime_error(
            "Hata: parse secenekleri yalnizca '--json' olabilir.");
      }
      return komutParse(argv[2], jsonCikti);
    }

    if (komut == "baytkod" || komut == "bytecode") {
      if (argc < 3) {
        throw std::runtime_error(
            "Hata: baytkod komutu icin dosya adi bekleniyor.");
      }
      bool jsonCikti = false;
      for (int i = 3; i < argc; ++i) {
        const std::string secenek = argv[i];
        if (secenek == "--json") {
          jsonCikti = true;
          continue;
        }
        throw std::runtime_error(
            "Hata: baytkod secenekleri yalnizca '--json' olabilir.");
      }
      return komutBytecode(argv[2], jsonCikti);
    }

    if (komut == "fmt") {
      if (argc < 3) {
        throw std::runtime_error("Hata: fmt komutu icin dosya adi bekleniyor.");
      }
      bool checkModu = false;
      bool jsonCikti = false;
      for (int i = 3; i < argc; ++i) {
        const std::string secenek = argv[i];
        if (secenek == "--check") {
          checkModu = true;
          continue;
        }
        if (secenek == "--json") {
          jsonCikti = true;
          continue;
        }
        throw std::runtime_error(
            "Hata: fmt secenekleri yalnizca '--check' ve '--json' olabilir.");
      }
      return komutFmt(argv[2], checkModu, jsonCikti);
    }

    if (komut == "surum") {
      std::cout << "Orhun v" << ORHUN_SURUM << " (insa " << ORHUN_INSA_NO
                << ")\n";
      return 0;
    }

    if (komut == "lint") {
      if (argc < 3) {
        throw std::runtime_error(
            "Hata: lint komutu icin dosya adi bekleniyor.");
      }
      bool strict = false;
      bool jsonCikti = false;
      for (int i = 3; i < argc; ++i) {
        const std::string secenek = argv[i];
        if (secenek == "--strict") {
          strict = true;
          continue;
        }
        if (secenek == "--json") {
          jsonCikti = true;
          continue;
        }
        throw std::runtime_error("Hata: bilinmeyen lint secenegi. "
                                 "Yalnizca '--strict' ve '--json' "
                                 "destekleniyor.");
      }
      return komutLint(argv[2], strict, jsonCikti);
    }

    if (komut == "paket") {
      if (argc < 3) {
        throw std::runtime_error(
            "Hata: paket komutlari: yeni | kur | ekle | liste | dogrula | ara "
            "| lock-guncelle | depo-baslat | depo-ekle");
      }

      const std::string alt = argv[2];
      if (alt == "yeni") {
        if (argc < 4) {
          throw std::runtime_error(
              "Hata: paket yeni icin proje adi bekleniyor.");
        }
        return komutPaketYeni(argv[3]);
      }

      if (alt == "kur") {
        if (argc < 4) {
          throw std::runtime_error(
              "Hata: paket kur <kaynak> [paket_adi] [--ref <deger>] "
              "[--no-lock] kullanin.");
        }
        std::string hedefAdi;
        std::string kaynakRef;
        bool noLock = false;
        for (int i = 4; i < argc; ++i) {
          const std::string secenek = argv[i];
          if (secenek == "--no-lock") {
            noLock = true;
            continue;
          }
          if (secenek == "--ref") {
            if (i + 1 >= argc) {
              throw std::runtime_error("Hata: --ref icin deger bekleniyor.");
            }
            kaynakRef = argv[++i];
            continue;
          }
          if (secenek.rfind("--ref=", 0) == 0) {
            kaynakRef = secenek.substr(6);
            continue;
          }
          if (hedefAdi.empty()) {
            hedefAdi = secenek;
            continue;
          }
          throw std::runtime_error(
              "Hata: paket kur yalnizca [paket_adi], [--ref <deger>] ve "
              "[--no-lock] alir.");
        }
        return komutPaketKur(argv[3], hedefAdi, kaynakRef, noLock);
      }

      if (alt == "ekle") {
        if (argc < 4) {
          throw std::runtime_error(
              "Hata: paket ekle <kaynak> [paket_adi] [--ref <deger>] "
              "[--no-lock] kullanin.");
        }
        std::string hedefAdi;
        std::string kaynakRef;
        bool noLock = false;
        for (int i = 4; i < argc; ++i) {
          const std::string secenek = argv[i];
          if (secenek == "--no-lock") {
            noLock = true;
            continue;
          }
          if (secenek == "--ref") {
            if (i + 1 >= argc) {
              throw std::runtime_error("Hata: --ref icin deger bekleniyor.");
            }
            kaynakRef = argv[++i];
            continue;
          }
          if (secenek.rfind("--ref=", 0) == 0) {
            kaynakRef = secenek.substr(6);
            continue;
          }
          if (hedefAdi.empty()) {
            hedefAdi = secenek;
            continue;
          }
          throw std::runtime_error(
              "Hata: paket ekle yalnizca [paket_adi], [--ref <deger>] ve "
              "[--no-lock] alir.");
        }
        return komutPaketKur(argv[3], hedefAdi, kaynakRef, noLock);
      }

      if (alt == "liste") {
        return komutPaketListe();
      }

      if (alt == "dogrula") {
        return komutPaketDogrula();
      }

      if (alt == "lock-guncelle") {
        return komutPaketLockGuncelle();
      }

      if (alt == "ara") {
        if (argc < 4) {
          throw std::runtime_error("Hata: paket ara <kelime> kullanin.");
        }
        return komutPaketAra(argv[3]);
      }

      if (alt == "depo-baslat") {
        const std::string klasor = argc >= 4 ? argv[3] : "";
        return komutPaketDepoBaslat(klasor);
      }

      if (alt == "depo-ekle") {
        if (argc < 5) {
          throw std::runtime_error(
              "Hata: paket depo-ekle <ad> <kaynak> [aciklama] kullanin.");
        }
        std::string aciklama;
        if (argc >= 6) {
          aciklama = argv[5];
          for (int i = 6; i < argc; ++i) {
            aciklama += " ";
            aciklama += argv[i];
          }
        }
        return komutPaketDepoEkle(argv[3], argv[4], aciklama);
      }

      throw std::runtime_error(
          "Hata: bilinmeyen paket komutu. 'yeni', 'kur', 'ekle', 'liste', "
          "'dogrula', 'ara', 'lock-guncelle', 'depo-baslat' veya 'depo-ekle' "
          "kullanin.");
    }

    if (komut == "vm") {
      if (argc < 3) {
        throw std::runtime_error(
            "Hata: vm komutu icin .oh dosyasi bekleniyor.");
      }
      return dosyaCalistirVM(argv[2], false);
    }

    if (komut == "vm-kati") {
      if (argc < 3) {
        throw std::runtime_error(
            "Hata: vm-kati komutu icin .oh dosyasi bekleniyor.");
      }
      return dosyaCalistirVM(argv[2], true);
    }

    if (komut == "yorumla") {
      if (argc < 3) {
        throw std::runtime_error(
            "Hata: yorumla komutu icin .oh dosyasi bekleniyor.");
      }
      return dosyaCalistirYorumlayici(argv[2]);
    }

    if (komut == "obc") {
      if (argc < 3) {
        throw std::runtime_error(
            "Hata: obc komutu icin .obc dosyasi bekleniyor.");
      }
      return komutObcCalistir(argv[2]);
    }

    if (komut == "derle") {
      if (argc < 3) {
        throw std::runtime_error(
            "Hata: derle komutu icin kaynak .oh bekleniyor.");
      }
      const std::string ciktiTemel = argc >= 4 ? argv[3] : "";
      return komutDerle(argv[2], argv[0], ciktiTemel);
    }

    if (komut == "hiz") {
      if (argc < 3) {
        throw std::runtime_error(
            "Hata: hiz komutu icin kaynak .oh bekleniyor.");
      }
      int tekrar = 20;
      int warmup = 10;
      bool json = false;
      std::string baselineJsonl;
      BenchmarkOlcumModu olcumModu = BenchmarkOlcumModu::Runtime;
      double gateP50 = 0.0;
      double gateP90 = 0.0;
      for (int i = 3; i < argc; ++i) {
        const std::string secenek = argv[i];
        if (secenek == "--json") {
          json = true;
          continue;
        }
        if (secenek == "--baseline") {
          if (i + 1 >= argc) {
            throw std::runtime_error(
                "Hata: --baseline icin dosya yolu bekleniyor.");
          }
          baselineJsonl = argv[++i];
          continue;
        }
        if (secenek.rfind("--baseline=", 0) == 0) {
          baselineJsonl = secenek.substr(11);
          continue;
        }
        if (secenek == "--warmup") {
          if (i + 1 >= argc) {
            throw std::runtime_error("Hata: --warmup icin sayi bekleniyor.");
          }
          warmup = std::stoi(argv[++i]);
          continue;
        }
        if (secenek.rfind("--warmup=", 0) == 0) {
          warmup = std::stoi(secenek.substr(9));
          continue;
        }
        if (secenek == "--olcum-modu") {
          if (i + 1 >= argc) {
            throw std::runtime_error(
                "Hata: --olcum-modu icin runtime/full bekleniyor.");
          }
          olcumModu = benchmarkOlcumModuCoz(argv[++i]);
          continue;
        }
        if (secenek.rfind("--olcum-modu=", 0) == 0) {
          olcumModu = benchmarkOlcumModuCoz(secenek.substr(13));
          continue;
        }
        if (secenek == "--gate-p50") {
          if (i + 1 >= argc) {
            throw std::runtime_error("Hata: --gate-p50 icin sayi bekleniyor.");
          }
          gateP50 = std::stod(argv[++i]);
          continue;
        }
        if (secenek == "--gate-p90") {
          if (i + 1 >= argc) {
            throw std::runtime_error("Hata: --gate-p90 icin sayi bekleniyor.");
          }
          gateP90 = std::stod(argv[++i]);
          continue;
        }
        if (secenek.rfind("--gate-p50=", 0) == 0) {
          gateP50 = std::stod(secenek.substr(11));
          continue;
        }
        if (secenek.rfind("--gate-p90=", 0) == 0) {
          gateP90 = std::stod(secenek.substr(11));
          continue;
        }
        if (secenek.rfind("--tekrar=", 0) == 0) {
          tekrar = std::stoi(secenek.substr(9));
          continue;
        }
        if (!secenek.empty() && secenek[0] != '-') {
          tekrar = std::stoi(secenek);
          continue;
        }
        throw std::runtime_error(
            "Hata: hiz secenekleri: [tekrar] [--json] [--tekrar=N] [--baseline "
            "dosya.jsonl] [--warmup=N] [--olcum-modu=runtime|full] "
            "[--gate-p50=X] [--gate-p90=Y]");
      }
      return komutHiz(argv[2], tekrar, json, baselineJsonl, gateP50, gateP90,
                      olcumModu, warmup);
    }

    if (komut == "lsp") {
      bool stdioModu = true;
      std::string workspaceRoot;
      for (int i = 2; i < argc; ++i) {
        const std::string secenek = argv[i];
        if (secenek == "--stdio") {
          continue;
        }
        if (secenek == "--workspace-root") {
          if (i + 1 >= argc) {
            throw std::runtime_error(
                "Hata: --workspace-root icin klasor yolu bekleniyor.");
          }
          workspaceRoot = argv[++i];
          continue;
        }
        if (secenek.rfind("--workspace-root=", 0) == 0) {
          workspaceRoot = secenek.substr(17);
          continue;
        }
        throw std::runtime_error(
            "Hata: lsp secenekleri yalnizca '--stdio' ve "
            "'--workspace-root <klasor>' olabilir.");
      }
      return komutLsp(stdioModu, workspaceRoot);
    }

    if (komut == "doctor") {
      bool jsonCikti = false;
      for (int i = 2; i < argc; ++i) {
        const std::string secenek = argv[i];
        if (secenek == "--json") {
          jsonCikti = true;
          continue;
        }
        throw std::runtime_error(
            "Hata: doctor secenekleri yalnizca '--json' olabilir.");
      }
      return komutDoctor(jsonCikti);
    }

    // Paketli exe, dahili komut dışındaki çağrılarda gömülü payload'ı
    // çalıştırır. Böylece "oyun.exe --mod hızlı" gibi kullanımda ana script
    // devreye girer.
    if (!dahiliKomutMu(komut) && gomuluPaketiCalistir(argv[0])) {
      return 0;
    }

    // Varsayilan motor VM'dir; desteklenmeyen ozellikte otomatik Interpreter
    // fallback yapilir.
    return dosyaCalistirVM(komut, false);
  } catch (const CliCikisHatasi &ex) {
    std::cerr << ex.what() << '\n';
    return ex.kod();
  } catch (const std::exception &ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}
