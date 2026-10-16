// Compression des blocs des sauvegardes et des cartes, réécrite pour l'hôte (formats/sav.md,
// « Compression ») :
//
//   - FUN_005c6300(&crc, &longueur, données, sortie, taille) : appelée par FUN_005c5f80, qui écrit
//     un bloc compressé (sauvegardes, FUN_0050e2c0 ; cartes, FUN_005118c0). Elle prend sur la pile
//     les paramètres des fonctions de lecture et d'écriture (0x34 octets) et la structure de
//     travail de PKWARE DCL implode (0x8DD8 octets), puis appelle implode ;
//   - FUN_005c96c0 implode(lecture, écriture, structure, paramètres, type, taille du dictionnaire),
//     et ses aides : FUN_005c9860 (boucle de compression), FUN_005c9be0 (recherche d'une
//     répétition), FUN_005ca0a0 (tri des positions par paire d'octets), FUN_005c9f20 (émission de
//     bits), FUN_005ca020 (vidage du tampon de sortie) ;
//   - FUN_005c61a0 / FUN_005c6240 : lecture des données et écriture du flux, avec le CRC 32 des
//     octets lus (FUN_005c9680, table en 0x00662038).
//
// Émulée, la compression figeait le jeu 3,6 s à chaque sauvegarde d'une grande ville. Ici, tout se
// passe dans la mémoire du jeu, aux adresses où l'original travaille : la structure de travail
// occupe la pile sous l'adresse de retour, et en fin de flux la recherche des répétitions lit
// au-delà des données, dans ce que le bloc précédent y a laissé (formats/sav.md). Elle est donc
// laissée exactement dans l'état où l'original la laisse, octet par octet, ainsi que les
// paramètres et ce qu'écrit FUN_005cbcd0 (sondage de la pile) dans le cadre ; seuls les cadres des
// fonctions appelées, sous la pile de FUN_005c6300, ne sont pas écrits. La structure suit celle de
// TCmpStruct (StormLib, implode.c), dont les décalages sont ceux de l'original.
//
// Adresses : win32/game_gog.h. Le code n'est remplacé que si ses octets sont ceux attendus
// (empreinte FNV-1a, comparée avant toute modification).

#include <cstdint>
#include <cstring>

#include "win32/game_gog.h"
#include "win32/native.h"
#include "win32/process.h"

namespace zeus::win32 {
namespace {

using namespace gog;

// Décalages dans la structure de travail (TCmpStruct).
constexpr uint32_t kDistance = 0x0000;    // distance de la répétition trouvée, moins 1
constexpr uint32_t kOutBytes = 0x0004;    // octets pleins dans le tampon de sortie
constexpr uint32_t kOutBits = 0x0008;     // bits déjà pris dans l'octet suivant
constexpr uint32_t kDictBits = 0x000C;    // 4, 5 ou 6
constexpr uint32_t kDictMask = 0x0010;    // 0x0F, 0x1F ou 0x3F
constexpr uint32_t kType = 0x0014;        // 0 binaire, 1 texte
constexpr uint32_t kDictSize = 0x0018;    // 0x400, 0x800 ou 0x1000
constexpr uint32_t kDistBits = 0x001C;    // 64 octets
constexpr uint32_t kDistCodes = 0x005C;   // 64 octets
constexpr uint32_t kChBits = 0x009C;      // 0x306 octets : littéraux puis longueurs
constexpr uint32_t kChCodes = 0x03A2;     // 0x306 mots
constexpr uint32_t kParam = 0x09B0;
constexpr uint32_t kReadFn = 0x09B4, kWriteFn = 0x09B8;
constexpr uint32_t kPrefix = 0x09BC;      // table des préfixes de la répétition en cours, mots
constexpr uint32_t kPairIndex = 0x0DC8;   // 0x900 mots : premier indice de chaque paire dans kPairOffs
constexpr uint32_t kOutBuff = 0x1FCA;     // 0x802 octets
constexpr uint32_t kWorkBuff = 0x27CC;    // dictionnaire + 0x204 + bloc lu (0x2204 octets)
constexpr uint32_t kPairOffs = 0x49D0;    // 0x2204 mots : positions triées par paire d'octets
constexpr uint32_t kWorkSize = 0x8DD8;
constexpr uint32_t kMaxRep = 0x204;

// Paramètres de FUN_005c61a0 / FUN_005c6240 (0x34 octets, juste sous la structure).
constexpr uint32_t kInPtr = 0x00, kInPos = 0x04, kInLen = 0x08, kOutPtr = 0x0C, kOutPos = 0x10, kOutCap = 0x14;
constexpr uint32_t kMode = 0x24, kCrc = 0x28, kTotal = 0x2C, kError = 0x30;
constexpr uint32_t kParamSize = 0x34;

class Implode {
public:
    Implode(Cpu& cpu, uint32_t work, uint32_t param)
        : m_(cpu.host(0)), w_(cpu.host(work)), wb_(w_ + kWorkBuff), p_(cpu.host(param)) {}

    // FUN_005c96c0 ; renvoie 0, ou 1 / 2 pour une taille de dictionnaire / un type refusés.
    uint32_t run(uint32_t param, uint32_t type_ptr, uint32_t dict_ptr) {
        put32(kReadFn, kSaveRead);
        put32(kWriteFn, kSaveWrite);
        put32(kDictSize, mem32(dict_ptr));
        put32(kType, mem32(type_ptr));
        put32(kParam, param);
        put32(kDictBits, 4);
        put32(kDictMask, 0x0F);
        switch (mem32(dict_ptr)) {
            case 0x1000:
                put32(kDictBits, get32(kDictBits) + 1);
                put32(kDictMask, get32(kDictMask) | 0x20);
                [[fallthrough]];
            case 0x800:
                put32(kDictBits, get32(kDictBits) + 1);
                put32(kDictMask, get32(kDictMask) | 0x10);
                [[fallthrough]];
            case 0x400:
                break;
            default:
                return 1;
        }
        uint32_t n = 0;
        switch (mem32(type_ptr)) {
            case 0:  // binaire : 9 bits par littéral
                for (; n < 0x100; ++n) {
                    w_[kChBits + n] = 9;
                    put16(kChCodes + 2 * n, static_cast<uint16_t>(n * 2));
                }
                break;
            case 1:  // texte : codes de la table de l'original
                for (; n < 0x100; ++n) {
                    w_[kChBits + n] = static_cast<uint8_t>(m_[kImplodeAsciiBits + n] + 1);
                    put16(kChCodes + 2 * n, static_cast<uint16_t>(mem16(kImplodeAsciiCodes + 2 * n) * 2));
                }
                break;
            default:
                return 2;
        }
        // Longueurs 2 à 0x207 : bit de répétition, code de la longueur, bits supplémentaires.
        for (uint32_t i = 0; i < 0x10; ++i) {
            const uint32_t extra = m_[kImplodeExLenBits + i], len_bits = m_[kImplodeLenBits + i];
            for (uint32_t e = 0; e < (1u << extra); ++e, ++n) {
                w_[kChBits + n] = static_cast<uint8_t>(extra + len_bits + 1);
                put16(kChCodes + 2 * n, static_cast<uint16_t>((e << (len_bits + 1)) | (m_[kImplodeLenCode + i] * 2u) | 1u));
            }
        }
        std::memcpy(w_ + kDistCodes, m_ + kImplodeDistCodes, 0x40);
        std::memcpy(w_ + kDistBits, m_ + kImplodeDistBits, 0x40);
        compress();
        return 0;
    }

private:
    uint8_t* m_;   // mémoire du jeu
    uint8_t* w_;   // structure de travail
    uint8_t* wb_;  // son tampon de travail
    uint8_t* p_;   // paramètres de lecture et d'écriture

    uint32_t mem32(uint32_t address) const { return load32(m_ + address); }
    uint16_t mem16(uint32_t address) const {
        uint16_t v;
        std::memcpy(&v, m_ + address, 2);
        return v;
    }
    static uint32_t load32(const uint8_t* p) {
        uint32_t v;
        std::memcpy(&v, p, 4);
        return v;
    }
    static void store32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
    uint32_t get32(uint32_t off) const { return load32(w_ + off); }
    void put32(uint32_t off, uint32_t v) { store32(w_ + off, v); }
    uint16_t get16(uint32_t off) const {
        uint16_t v;
        std::memcpy(&v, w_ + off, 2);
        return v;
    }
    void put16(uint32_t off, uint16_t v) { std::memcpy(w_ + off, &v, 2); }
    uint32_t param(uint32_t off) const { return load32(p_ + off); }
    void set_param(uint32_t off, uint32_t v) { store32(p_ + off, v); }
    const uint8_t* pair_pos(uint32_t index) const { return wb_ + get16(kPairOffs + 2 * index); }

    // FUN_005c9680 : CRC 32 de `size` octets.
    uint32_t crc(const uint8_t* data, uint32_t size, uint32_t value) const {
        for (uint32_t i = 0; i < size; ++i) value = load32(m_ + kImplodeCrcTable + 4 * ((data[i] ^ value) & 0xFF)) ^ (value >> 8);
        return value;
    }

    // FUN_005c61a0 (mode 1, celui de FUN_005c6300) : au plus `size` octets des données.
    uint32_t read(uint8_t* buf, uint32_t size) {
        if (param(kError) == 1) return 0;
        const uint32_t pos = param(kInPos), len = param(kInLen);
        uint32_t n = 0;
        if (pos < len) {
            n = len - pos < size ? len - pos : size;
            std::memcpy(buf, m_ + param(kInPtr) + pos, n);
            set_param(kInPos, pos + n);
        }
        set_param(kCrc, crc(buf, n, param(kCrc)));
        return n;
    }

    // FUN_005c6240 (mode 1) : ajoute `size` octets du flux à la sortie, ou lève l'erreur (le flux
    // dépasse la taille des données) sans rien écrire.
    void write(const uint8_t* buf, uint32_t size) {
        if (param(kError) == 1) return;
        set_param(kTotal, param(kTotal) + size);
        const uint32_t pos = param(kOutPos), cap = param(kOutCap);
        if (pos >= cap || cap - pos < size) {
            set_param(kError, 1);  // après FUN_00401000, qui ne fait rien
            return;
        }
        std::memcpy(m_ + param(kOutPtr) + pos, buf, size);
        set_param(kOutPos, pos + size);
    }

    // FUN_005ca020.
    void flush() {
        write(w_ + kOutBuff, 0x800);
        const uint8_t first = w_[kOutBuff + 0x800];
        const uint32_t bytes = get32(kOutBytes) - 0x800;
        const uint8_t last = w_[kOutBuff + 0x800 + bytes];
        put32(kOutBytes, bytes);
        std::memset(w_ + kOutBuff, 0, 0x802);
        if (bytes != 0) w_[kOutBuff] = first;
        if (get32(kOutBits) != 0) w_[kOutBuff + bytes] = last;
    }

    // FUN_005c9f20 : `nbits` bits de `bits` (premier émis : le bit 0), 16 au plus.
    void output(uint32_t nbits, uint32_t bits) {
        if (nbits > 8) {
            output(8, bits);
            nbits -= 8;
            bits >>= 8;
        }
        const uint32_t used = get32(kOutBits);
        uint32_t bytes = get32(kOutBytes);
        w_[kOutBuff + bytes] |= static_cast<uint8_t>((bits & 0xFF) << used);
        const uint32_t total = used + nbits;
        if (total > 8) {
            put32(kOutBytes, ++bytes);
            w_[kOutBuff + bytes] = static_cast<uint8_t>(bits >> (8 - used));
            put32(kOutBits, total & 7);
        } else {
            put32(kOutBits, total & 7);
            if ((total & 7) == 0) put32(kOutBytes, ++bytes);
        }
        if (bytes >= 0x800) flush();
    }

    void literal(const uint8_t* at) { output(w_[kChBits + *at], get16(kChCodes + 2 * *at)); }

    // FUN_005ca0a0 : trie les positions [begin, end[ par paire d'octets 4 × b0 + 5 × b1.
    void sort(const uint8_t* begin, const uint8_t* end) {
        std::memset(w_ + kPairIndex, 0, 0x1200);
        const uint8_t* p = begin;
        do {
            const uint32_t h = p[0] * 4u + p[1] * 5u;
            put16(kPairIndex + 2 * h, static_cast<uint16_t>(get16(kPairIndex + 2 * h) + 1));
            ++p;
        } while (end > p);
        uint16_t sum = 0;
        for (uint32_t h = 0; h < 0x900; ++h) {
            sum = static_cast<uint16_t>(sum + get16(kPairIndex + 2 * h));
            put16(kPairIndex + 2 * h, sum);
        }
        p = end - 1;
        do {
            const uint32_t h = p[0] * 4u + p[1] * 5u;
            const uint16_t i = static_cast<uint16_t>(get16(kPairIndex + 2 * h) - 1);
            put16(kPairIndex + 2 * h, i);
            put16(kPairOffs + 2u * i, static_cast<uint16_t>(p - wb_));
            --p;
        } while (begin <= p);
    }

    // Prolonge la table des préfixes de la répétition jusqu'à la longueur `upto` (FUN_005c9be0,
    // 0x005C9D47 et 0x005C9EBE) ; `k` et `di` sont les mots 16 bits signés de l'original.
    void extend_prefix(const uint8_t* in, int16_t& k, uint16_t& di, int32_t upto) {
        do {
            if (in[k] != in[static_cast<int16_t>(di)]) {
                di = get16(kPrefix + 2 * static_cast<int32_t>(static_cast<int16_t>(di)));
                if (di != 0xFFFF) continue;
            }
            ++k;
            ++di;
            put16(kPrefix + 2 * k, di);
        } while (k < upto);
    }

    // FUN_005c9be0 : longueur de la meilleure répétition à `in` (0 : aucune), distance moins 1
    // dans kDistance.
    uint32_t find_rep(const uint8_t* in) {
        int32_t rep = 1;
        const uint32_t pair = kPairIndex + 2 * (in[0] * 4u + in[1] * 5u);
        const uint32_t min_offs = static_cast<uint32_t>(in - wb_) - get32(kDictSize) + 1;
        uint32_t idx = get16(pair);
        if (get16(kPairOffs + 2 * idx) < min_offs) {
            do ++idx;
            while (get16(kPairOffs + 2 * idx) < min_offs);
            put16(pair, static_cast<uint16_t>(idx));
        }
        const uint8_t* limit = in - 1;
        const uint8_t* prev = pair_pos(idx);
        if (limit <= prev) return 0;
        // Première passe : la plus ancienne des plus longues, arrêt au-delà de 10 octets.
        for (;;) {
            if (prev[rep - 1] == in[rep - 1] && prev[0] == in[0]) {
                const uint8_t* a = prev + 1;
                const uint8_t* b = in + 1;
                int32_t equal = 2;
                for (;;) {
                    ++a;
                    ++b;
                    if (*a != *b) break;
                    if (++equal >= static_cast<int32_t>(kMaxRep)) break;
                }
                if (equal >= rep) {
                    put32(kDistance, static_cast<uint32_t>(in - a) + static_cast<uint32_t>(equal) - 1);
                    rep = equal;
                    if (equal > 10) break;
                }
            }
            ++idx;
            prev = pair_pos(idx);
            if (limit <= prev) return rep >= 2 ? static_cast<uint32_t>(rep) : 0;
        }
        if (rep == static_cast<int32_t>(kMaxRep)) {
            put32(kDistance, get32(kDistance) - 1);
            return static_cast<uint32_t>(rep);
        }
        if (pair_pos(idx + 1) >= limit) return static_cast<uint32_t>(rep);
        // Seconde passe : une occurrence plus récente au moins aussi longue, sans recomparer le
        // préfixe déjà connu (table des préfixes, à la Knuth-Morris-Pratt).
        uint16_t di = 0;
        int16_t k = 1;
        put16(kPrefix, 0xFFFF);
        put16(kPrefix + 2, 0);
        extend_prefix(in, k, di, rep);
        int32_t s = rep;
        const uint8_t* prev_end = pair_pos(idx) + rep;
        for (;;) {
            s = static_cast<int16_t>(get16(kPrefix + 2 * s));
            if (s == -1) s = 0;
            const uint8_t* cand;
            do {
                cand = pair_pos(++idx);
                if (cand >= limit) return static_cast<uint32_t>(rep);
            } while (cand + s < prev_end);
            const uint8_t pre_last = in[rep - 2];
            if (cand[rep - 2] == pre_last) {
                if (cand + s != prev_end) {
                    s = 0;
                    prev_end = cand;
                }
            } else {
                for (;;) {
                    cand = pair_pos(++idx);
                    if (cand >= limit) return static_cast<uint32_t>(rep);
                    if (cand[rep - 2] == pre_last && in[0] == cand[0]) break;
                }
                s = 2;
                prev_end = cand + 2;
            }
            if (in[s] == *prev_end) {
                for (;;) {
                    if (++s >= static_cast<int32_t>(kMaxRep)) break;
                    ++prev_end;
                    if (in[s] != *prev_end) break;
                }
            }
            if (s < rep) continue;
            put32(kDistance, static_cast<uint32_t>(in - cand) - 1);
            if (s <= rep) continue;
            rep = s;
            if (s == static_cast<int32_t>(kMaxRep)) return static_cast<uint32_t>(rep);
            extend_prefix(in, k, di, s);
        }
    }

    // FUN_005c9860.
    void compress() {
        const uint32_t dict = get32(kDictSize);
        w_[kOutBuff] = static_cast<uint8_t>(get32(kType));
        w_[kOutBuff + 1] = static_cast<uint8_t>(get32(kDictBits));
        put32(kOutBytes, 2);
        std::memset(w_ + kOutBuff + 2, 0, 0x800);
        put32(kOutBits, 0);
        const uint8_t* in = wb_ + dict + kMaxRep;
        int phase = 0;
        bool ended = false;
        for (;;) {
            uint32_t total = 0;
            for (uint32_t want = 0x1000; want != 0;) {
                const uint32_t n = read(wb_ + dict + kMaxRep + total, want);
                if (n == 0) {
                    if (total == 0 && phase == 0) return finish();
                    ended = true;
                    break;
                }
                total += n;
                want -= n;
            }
            const uint8_t* end = wb_ + dict + total + (ended ? kMaxRep : 0);
            if (phase == 0) {
                sort(in, end + 1);
                ++phase;
                if (dict != 0x1000) ++phase;
            } else if (phase == 1) {
                sort(in - dict + kMaxRep, end + 1);
                ++phase;
            } else {
                sort(in - dict, end + 1);
            }
            while (end > in) {
                uint32_t rep = find_rep(in);
                bool repeat = false;
                while (rep != 0) {
                    if (rep == 2 && get32(kDistance) >= 0x100) break;
                    if (ended && in + rep > end) {
                        rep = static_cast<uint32_t>(end - in);
                        if (rep < 2 || (rep == 2 && get32(kDistance) >= 0x100)) break;
                        repeat = true;
                        break;
                    }
                    if (rep >= 8 || in + 1 >= end) {
                        repeat = true;
                        break;
                    }
                    // Une répétition plus longue un octet plus loin ?
                    const uint32_t saved_rep = rep, saved_distance = get32(kDistance);
                    rep = find_rep(in + 1);
                    if (saved_rep < rep && (saved_rep + 1 < rep || saved_distance > 0x80)) {
                        literal(in++);
                        continue;
                    }
                    rep = saved_rep;
                    put32(kDistance, saved_distance);
                    repeat = true;
                    break;
                }
                if (!repeat) {
                    literal(in++);
                    continue;
                }
                output(w_[kChBits + rep + 0xFE], get16(kChCodes + 2 * (rep + 0xFE)));
                const uint32_t distance = get32(kDistance);
                if (rep == 2) {
                    output(w_[kDistBits + (distance >> 2)], w_[kDistCodes + (distance >> 2)]);
                    output(2, distance & 3);
                } else {
                    const uint32_t bits = get32(kDictBits);
                    output(w_[kDistBits + (distance >> bits)], w_[kDistCodes + (distance >> bits)]);
                    output(bits, get32(kDictMask) & distance);
                }
                in += rep;
            }
            if (ended) return finish();
            in -= 0x1000;
            std::memmove(wb_, wb_ + 0x1000, dict + kMaxRep);
        }
    }

    // Fin du flux (longueur 519) et dernier envoi.
    void finish() {
        output(w_[kChBits + 0x305], get16(kChCodes + 2 * 0x305));
        if (get32(kOutBits) != 0) put32(kOutBytes, get32(kOutBytes) + 1);
        write(w_ + kOutBuff, get32(kOutBytes));
    }
};

// FUN_005c6300(&crc, &longueur, données, sortie, taille) : 1 et le flux dans `sortie`, ou 0 si le
// flux ne tient pas dans `taille` octets (FUN_005c5f80 écrit alors le bloc tel quel).
uint32_t compress_block(Cpu& cpu) {
    const uint32_t ret = cpu.reg(Cpu::Reg::ESP);  // adresse de retour
    const uint32_t work = ret - kWorkSize, param = work - kParamSize;
    // FUN_005cbcd0 (sondage de la pile) laisse son adresse de retour et ECX en haut du cadre ;
    // FUN_005c6300 range ensuite ESI et EDI sous les paramètres.
    uint8_t* m = cpu.host(0);
    auto put = [m](uint32_t address, uint32_t value) { std::memcpy(m + address, &value, 4); };
    put(ret - 4, kCompressBlock + 0x0A);
    put(ret - 8, cpu.reg(Cpu::Reg::ECX));
    put(param - 4, cpu.reg(Cpu::Reg::ESI));
    put(param - 8, cpu.reg(Cpu::Reg::EDI));
    std::memset(m + param, 0, kParamSize);
    const uint32_t size = cpu.arg(4);
    put(param + kInPtr, cpu.arg(2));
    put(param + kInLen, size);
    put(param + kOutPtr, cpu.arg(3));
    put(param + kOutCap, size);
    put(param + kMode, 1);
    put(param + kCrc, 0xFFFFFFFFu);
    Implode implode(cpu, work, param);
    if (implode.run(param, kImplodeType, kImplodeDictSize) != 0) return 0;
    uint32_t error, crc, total;
    std::memcpy(&error, m + param + kError, 4);
    if (error != 0) return 0;
    std::memcpy(&crc, m + param + kCrc, 4);
    std::memcpy(&total, m + param + kTotal, 4);
    put(cpu.arg(0), ~crc);
    put(cpu.arg(1), total);
    return 1;
}

uint64_t fingerprint(Cpu& cpu) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (const auto& range : kImplodeCode)
        for (uint32_t a = range[0]; a < range[1]; ++a) hash = (hash ^ *cpu.host(a)) * 0x100000001B3ull;
    return hash;
}

}  // namespace

int install_native_save(Process& p) {
    Cpu& cpu = p.cpu();
    if (fingerprint(cpu) != kImplodeCodeHash) return 0;
    cpu.replace_function(kCompressBlock, 0, compress_block);
    return 1;
}

}  // namespace zeus::win32
