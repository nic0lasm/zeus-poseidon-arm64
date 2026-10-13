// Mixeur audio de l'hôte : voix PCM mélangées vers la sortie son de SDL.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace zeus::win32 {

// Son décodé : échantillons 16 bits entrelacés.
struct Sound {
    int rate = 0, channels = 0;
    std::vector<int16_t> samples;
    std::atomic<bool> ready{false};  // faux tant qu'un décodage en arrière-plan n'est pas fini
    size_t frames() const { return channels ? samples.size() / static_cast<size_t>(channels) : 0; }
};

// Lit un fichier WAV PCM 8 ou 16 bits placé en mémoire. Renvoie nul si le format n'est pas pris en charge.
std::shared_ptr<Sound> decode_wav(const uint8_t* data, size_t size);
// Taille totale d'un fichier WAV d'après son en-tête RIFF, ou 0.
size_t wav_size(const uint8_t* header, size_t available);
// Décode un fichier audio (MP3, WAV…) en arrière-plan avec AudioToolbox.
std::shared_ptr<Sound> decode_file_async(const std::filesystem::path& path);

class Mixer {
public:
    enum class State { Stopped, Playing, Done };

    Mixer();
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    bool muted = false;  // mélange sans rien faire entendre (essais sans fenêtre)
    // Diagnostic : plus forte amplitude mélangée et nombre de lectures lancées depuis le début.
    std::atomic<int> peak{0}, starts{0};

    int add_voice();
    void remove_voice(int id);
    // Remet la voix dans son état initial : terminée, volume maximal, centrée, une seule lecture.
    void reset(int id);
    void set_sound(int id, std::shared_ptr<Sound> sound);
    void start(int id);   // depuis le début
    void stop(int id);
    void resume(int id);
    void end(int id);     // arrêt définitif : la voix est terminée
    void set_volume(int id, int volume);  // 0 à 127
    void set_pan(int id, int pan);        // 0 (gauche) à 127 (droite)
    void set_loops(int id, int loops);    // 0 : sans fin
    void set_rate(int id, int hertz);     // fréquence de lecture, 0 : celle du son
    int volume(int id);
    int rate(int id);
    State state(int id);
    // Position de lecture, en fraction de la durée du son (0 à 1).
    double position(int id);
    void set_position(int id, double fraction);

private:
    struct Voice {
        std::shared_ptr<Sound> sound;
        State state = State::Done;
        double cursor = 0;  // en images du son
        int volume = 127, pan = 64, loops = 1, loops_done = 0, rate = 0;
    };
    static void callback(void* user, uint8_t* stream, int length);
    void mix(int16_t* out, int frames);
    Voice* find(int id);

    std::mutex mutex_;
    std::map<int, Voice> voices_;
    int next_id_ = 1;
    uint32_t device_ = 0;
    int output_rate_ = 44100;
    std::vector<int32_t> accumulator_;
};

}  // namespace zeus::win32
