// MSS32 : Miles Sound System, la bibliothèque de son du jeu, réalisée sur le mixeur de l'hôte.
//
// Le jeu joue des « échantillons » (fichiers WAV entiers placés en mémoire : bruitages, voix) et
// des « flux » (fichiers lus depuis le disque : musiques MP3). Les fournisseurs de son 3D ne sont
// pas proposés : l'énumération n'en renvoie aucun. Les signatures viennent de la documentation
// publique de Miles 6 ; les fonctions pas encore écrites ne font rien et renvoient 0.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>

#include "win32/audio.h"
#include "win32/process.h"

namespace zeus::win32 {
namespace {

// États d'un échantillon ou d'un flux.
constexpr uint32_t SMP_DONE = 2, SMP_PLAYING = 4, SMP_STOPPED = 8;

struct Handle {
    int voice = 0;
    bool stream = false;
    uint32_t file_size = 0;  // flux : taille du fichier, unité des positions
};

struct State {
    std::unique_ptr<Mixer> mixer;
    std::map<uint32_t, Handle> handles;  // adresse émulée de la poignée -> voix
    std::map<uint32_t, uint32_t> preferences;
    bool report = false;
    ~State() {
        if (report && mixer)
            std::fprintf(stderr, "[hôte] son : %d lectures lancées, amplitude maximale %d sur 32767\n",
                         mixer->starts.load(), mixer->peak.load());
    }
};

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

uint32_t status_of(Mixer::State s) {
    switch (s) {
        case Mixer::State::Playing: return SMP_PLAYING;
        case Mixer::State::Stopped: return SMP_STOPPED;
        default: return SMP_DONE;
    }
}

}  // namespace

void install_mss(Process& p) {
    auto st = std::make_shared<State>();
    Cpu& cpu = p.cpu();
    p.allow_silent_stubs("mss32.dll");
    auto def = [&p](const char* name, uint32_t args, std::function<uint32_t()> fn) {
        p.def("mss32.dll", name, args * 4, std::move(fn));
    };
    // Voix d'une poignée valide, ou nul.
    auto handle = [st](uint32_t address) -> Handle* {
        const auto found = st->handles.find(address);
        return found == st->handles.end() || !st->mixer ? nullptr : &found->second;
    };
    auto new_handle = [&p, st](bool stream) {
        const uint32_t address = p.alloc(16);
        Handle h;
        h.voice = st->mixer->add_voice();
        h.stream = stream;
        st->handles[address] = h;
        return address;
    };
    auto free_handle = [&p, st](uint32_t address) {
        const auto found = st->handles.find(address);
        if (found == st->handles.end()) return;
        if (st->mixer) st->mixer->remove_voice(found->second.voice);
        st->handles.erase(found);
        p.release(address);
    };
    // Copie depuis la mémoire émulée le fichier WAV qui commence à `address`.
    auto guest_wav = [&p, &cpu](uint32_t address) {
        std::vector<uint8_t> data;
        if (!address) return data;
        uint8_t header[12];
        cpu.read(address, header, sizeof header);
        size_t size = wav_size(header, sizeof header);
        if (const uint32_t block = p.alloc_size(address)) size = size ? std::min<size_t>(size, block) : 0;
        if (!size || size > (64u << 20)) return data;
        data.resize(size);
        cpu.read(address, data.data(), size);
        return data;
    };

    // --- système ---------------------------------------------------------------------
    def("_AIL_startup@0", 0, [] { return 1u; });
    def("_AIL_shutdown@0", 0, [st] {
        st->handles.clear();
        st->mixer.reset();
        return 0u;
    });
    def("_AIL_set_preference@8", 2, [&p, st] {
        const uint32_t previous = st->preferences[p.arg(0)];
        st->preferences[p.arg(0)] = p.arg(1);
        return previous;
    });
    def("_AIL_get_preference@4", 1, [&p, st] { return st->preferences[p.arg(0)]; });
    def("_AIL_serve@0", 0, [] { return 0u; });
    // AIL_waveOutOpen(pilote*, sortie*, périphérique, format) : 0 en cas de succès.
    def("_AIL_waveOutOpen@16", 4, [&p, &cpu, st] {
        try {
            if (!st->mixer) st->mixer = std::make_unique<Mixer>();
            st->mixer->muted = p.mute;
            st->report = p.trace;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[hôte] pas de sortie son : %s\n", e.what());
            return 1u;
        }
        cpu.write_u32(p.arg(0), 0x00D16000);  // poignée opaque du pilote
        return 0u;
    });

    // --- fichiers --------------------------------------------------------------------
    // AIL_file_read(nom, destination) : destination nulle = mémoire allouée par la bibliothèque.
    def("_AIL_file_read@8", 2, [&p, &cpu] {
        const auto path = p.vfs().find(cpu.read_string(p.arg(0)));
        if (p.trace) std::fprintf(stderr, "    son lu : %s%s\n", cpu.read_string(p.arg(0)).c_str(), path ? "" : " (absent)");
        if (!path) return 0u;
        const std::vector<uint8_t> data = read_file(*path);
        uint32_t destination = p.arg(1);
        if (destination == 0xFFFFFFFF) throw std::runtime_error("lecture avec taille en tête non fournie");
        if (!destination) destination = p.alloc(static_cast<uint32_t>(data.size()), false);
        if (destination && !data.empty()) cpu.write(destination, data.data(), data.size());
        return destination;
    });
    def("_AIL_mem_free_lock@4", 1, [&p] {
        p.release(p.arg(0));
        return 0u;
    });
    // AIL_WAV_info(données, AILSOUNDINFO*) : 1 si le fichier est compris.
    def("_AIL_WAV_info@8", 2, [&p, &cpu, guest_wav] {
        const std::vector<uint8_t> data = guest_wav(p.arg(0));
        const auto sound = decode_wav(data.data(), data.size());
        if (!sound) return 0u;
        size_t data_offset = 0;
        for (size_t at = 12; at + 8 <= data.size();) {
            uint32_t length;
            std::memcpy(&length, &data[at + 4], 4);
            if (!std::memcmp(&data[at], "data", 4)) {
                data_offset = at + 8;
                break;
            }
            at += 8 + length + (length & 1);
        }
        const uint32_t info = p.arg(1);
        const uint32_t bytes = static_cast<uint32_t>(sound->samples.size() * 2);
        cpu.write_u32(info + 0, 1);  // WAVE_FORMAT_PCM
        cpu.write_u32(info + 4, p.arg(0) + static_cast<uint32_t>(data_offset));
        cpu.write_u32(info + 8, bytes);
        cpu.write_u32(info + 12, static_cast<uint32_t>(sound->rate));
        cpu.write_u32(info + 16, 16);
        cpu.write_u32(info + 20, static_cast<uint32_t>(sound->channels));
        cpu.write_u32(info + 24, static_cast<uint32_t>(sound->frames()));
        cpu.write_u32(info + 28, static_cast<uint32_t>(2 * sound->channels));
        cpu.write_u32(info + 32, p.arg(0));
        return 1u;
    });

    // --- échantillons ----------------------------------------------------------------
    def("_AIL_allocate_sample_handle@4", 1, [st, new_handle] { return st->mixer ? new_handle(false) : 0u; });
    def("_AIL_release_sample_handle@4", 1, [&p, free_handle] {
        free_handle(p.arg(0));
        return 0u;
    });
    def("_AIL_init_sample@4", 1, [&p, st, handle] {
        if (Handle* h = handle(p.arg(0))) st->mixer->reset(h->voice);
        return 0u;
    });
    // AIL_set_sample_file(échantillon, image du fichier, bloc) : 1 si le fichier est compris.
    def("_AIL_set_sample_file@12", 3, [&p, st, handle, guest_wav] {
        Handle* h = handle(p.arg(0));
        if (!h) return 0u;
        const std::vector<uint8_t> data = guest_wav(p.arg(1));
        auto sound = decode_wav(data.data(), data.size());
        if (!sound) {
            std::fprintf(stderr, "[hôte] échantillon sonore de format inconnu en %#x\n", p.arg(1));
            return 0u;
        }
        if (p.trace)
            std::fprintf(stderr, "    échantillon %#x : %zu images à %d Hz, %d canal(aux)\n", p.arg(1), sound->frames(),
                         sound->rate, sound->channels);
        st->mixer->set_sound(h->voice, std::move(sound));
        return 1u;
    });
    def("_AIL_start_sample@4", 1, [&p, st, handle] {
        if (Handle* h = handle(p.arg(0))) st->mixer->start(h->voice);
        return 0u;
    });
    def("_AIL_end_sample@4", 1, [&p, st, handle] {
        if (Handle* h = handle(p.arg(0))) st->mixer->end(h->voice);
        return 0u;
    });
    def("_AIL_sample_status@4", 1, [&p, st, handle] {
        Handle* h = handle(p.arg(0));
        return h ? status_of(st->mixer->state(h->voice)) : SMP_DONE;
    });
    def("_AIL_set_sample_volume@8", 2, [&p, st, handle] {
        if (Handle* h = handle(p.arg(0))) st->mixer->set_volume(h->voice, static_cast<int32_t>(p.arg(1)));
        return 0u;
    });
    def("_AIL_set_sample_pan@8", 2, [&p, st, handle] {
        if (Handle* h = handle(p.arg(0))) st->mixer->set_pan(h->voice, static_cast<int32_t>(p.arg(1)));
        return 0u;
    });
    def("_AIL_set_sample_loop_count@8", 2, [&p, st, handle] {
        if (Handle* h = handle(p.arg(0))) st->mixer->set_loops(h->voice, static_cast<int32_t>(p.arg(1)));
        return 0u;
    });

    // --- flux ------------------------------------------------------------------------
    // AIL_open_stream(pilote, nom de fichier, mémoire) : poignée, ou 0 si le fichier manque.
    def("_AIL_open_stream@12", 3, [&p, &cpu, st, new_handle] {
        if (!st->mixer) return 0u;
        const std::string name = cpu.read_string(p.arg(1));
        const auto path = p.vfs().find(name);
        if (p.trace) std::fprintf(stderr, "    flux : %s%s\n", name.c_str(), path ? "" : " (absent)");
        if (!path) return 0u;
        std::shared_ptr<Sound> sound;
        std::error_code error;
        const auto size = std::filesystem::file_size(*path, error);
        const size_t dot = name.rfind('.');
        std::string extension = dot == std::string::npos ? "" : name.substr(dot);
        for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (extension == ".wav") {
            const std::vector<uint8_t> data = read_file(*path);
            sound = decode_wav(data.data(), data.size());
        }
        if (!sound) sound = decode_file_async(*path);
        const uint32_t address = new_handle(true);
        Handle& h = st->handles[address];
        h.file_size = error ? 0 : static_cast<uint32_t>(size);
        st->mixer->set_sound(h.voice, std::move(sound));
        return address;
    });
    def("_AIL_close_stream@4", 1, [&p, free_handle] {
        free_handle(p.arg(0));
        return 0u;
    });
    def("_AIL_start_stream@4", 1, [&p, st, handle] {
        if (Handle* h = handle(p.arg(0))) st->mixer->start(h->voice);
        return 0u;
    });
    def("_AIL_stream_status@4", 1, [&p, st, handle] {
        Handle* h = handle(p.arg(0));
        return h ? status_of(st->mixer->state(h->voice)) : SMP_DONE;
    });
    def("_AIL_set_stream_volume@8", 2, [&p, st, handle] {
        if (Handle* h = handle(p.arg(0))) st->mixer->set_volume(h->voice, static_cast<int32_t>(p.arg(1)));
        return 0u;
    });
    def("_AIL_stream_volume@4", 1, [&p, st, handle] {
        Handle* h = handle(p.arg(0));
        return h ? static_cast<uint32_t>(st->mixer->volume(h->voice)) : 0u;
    });
    def("_AIL_set_stream_loop_count@8", 2, [&p, st, handle] {
        if (Handle* h = handle(p.arg(0))) st->mixer->set_loops(h->voice, static_cast<int32_t>(p.arg(1)));
        return 0u;
    });
    // Les positions d'un flux se comptent en octets du fichier.
    def("_AIL_stream_position@4", 1, [&p, st, handle] {
        Handle* h = handle(p.arg(0));
        return h ? static_cast<uint32_t>(st->mixer->position(h->voice) * h->file_size) : 0u;
    });
    def("_AIL_set_stream_position@8", 2, [&p, st, handle] {
        Handle* h = handle(p.arg(0));
        if (h && h->file_size) st->mixer->set_position(h->voice, static_cast<double>(p.arg(1)) / h->file_size);
        return 0u;
    });

    // --- son 3D : aucun fournisseur proposé -------------------------------------------
    def("_AIL_enumerate_3D_providers@12", 3, [] { return 0u; });
    def("_AIL_open_3D_provider@4", 1, [] { return 1u; });  // échec
}

}  // namespace zeus::win32
