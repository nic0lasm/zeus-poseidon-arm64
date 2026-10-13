#include "win32/audio.h"

#include <AudioToolbox/AudioToolbox.h>
#include <SDL.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace zeus::win32 {
namespace {

uint32_t u32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }
uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }

// Décode tout le fichier en 16 bits à sa fréquence et son nombre de canaux d'origine.
void decode_with_audio_toolbox(const std::filesystem::path& path, Sound& out) {
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(path.c_str()), static_cast<CFIndex>(path.native().size()), false);
    ExtAudioFileRef file = nullptr;
    if (url && ExtAudioFileOpenURL(url, &file) == noErr) {
        AudioStreamBasicDescription source{};
        UInt32 size = sizeof source;
        ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileDataFormat, &size, &source);
        const int channels = source.mChannelsPerFrame >= 2 ? 2 : 1;
        AudioStreamBasicDescription client{};
        client.mSampleRate = source.mSampleRate;
        client.mFormatID = kAudioFormatLinearPCM;
        client.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
        client.mBitsPerChannel = 16;
        client.mChannelsPerFrame = static_cast<UInt32>(channels);
        client.mBytesPerFrame = static_cast<UInt32>(2 * channels);
        client.mFramesPerPacket = 1;
        client.mBytesPerPacket = client.mBytesPerFrame;
        if (ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat, sizeof client, &client) == noErr) {
            out.rate = static_cast<int>(source.mSampleRate);
            out.channels = channels;
            std::vector<int16_t> chunk(static_cast<size_t>(16384) * static_cast<size_t>(channels));
            for (;;) {
                AudioBufferList list;
                list.mNumberBuffers = 1;
                list.mBuffers[0].mNumberChannels = static_cast<UInt32>(channels);
                list.mBuffers[0].mDataByteSize = static_cast<UInt32>(chunk.size() * 2);
                list.mBuffers[0].mData = chunk.data();
                UInt32 frames = 16384;
                if (ExtAudioFileRead(file, &frames, &list) != noErr || frames == 0) break;
                out.samples.insert(out.samples.end(), chunk.begin(),
                                   chunk.begin() + static_cast<std::ptrdiff_t>(frames) * channels);
            }
        }
        ExtAudioFileDispose(file);
    }
    if (url) CFRelease(url);
}

}  // namespace

size_t wav_size(const uint8_t* header, size_t available) {
    if (available < 12 || std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0) return 0;
    return static_cast<size_t>(u32(header + 4)) + 8;
}

std::shared_ptr<Sound> decode_wav(const uint8_t* data, size_t size) {
    if (!wav_size(data, size)) return nullptr;
    int format = 0, channels = 0, rate = 0, bits = 0;
    for (size_t at = 12; at + 8 <= size;) {
        const size_t length = u32(data + at + 4), body = at + 8;
        if (body + length > size) break;
        if (!std::memcmp(data + at, "fmt ", 4) && length >= 16) {
            format = u16(data + body);
            channels = u16(data + body + 2);
            rate = static_cast<int>(u32(data + body + 4));
            bits = u16(data + body + 14);
        } else if (!std::memcmp(data + at, "data", 4)) {
            if (format != 1 || (channels != 1 && channels != 2) || (bits != 8 && bits != 16) || rate <= 0) return nullptr;
            auto sound = std::make_shared<Sound>();
            sound->rate = rate;
            sound->channels = channels;
            if (bits == 16) {
                sound->samples.resize(length / 2);
                for (size_t i = 0; i < sound->samples.size(); ++i)
                    sound->samples[i] = static_cast<int16_t>(u16(data + body + 2 * i));
            } else {
                sound->samples.resize(length);
                for (size_t i = 0; i < length; ++i) sound->samples[i] = static_cast<int16_t>((data[body + i] - 128) << 8);
            }
            sound->samples.resize(sound->frames() * static_cast<size_t>(channels));
            sound->ready = true;
            return sound;
        }
        at = body + length + (length & 1);
    }
    return nullptr;
}

std::shared_ptr<Sound> decode_file_async(const std::filesystem::path& path) {
    auto sound = std::make_shared<Sound>();
    std::thread([sound, path] {
        decode_with_audio_toolbox(path, *sound);
        sound->ready = true;
    }).detach();
    return sound;
}

Mixer::Mixer() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) throw std::runtime_error(SDL_GetError());
    SDL_AudioSpec want{}, have{};
    want.freq = output_rate_;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = &Mixer::callback;
    want.userdata = this;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!device_) throw std::runtime_error(SDL_GetError());
    output_rate_ = have.freq;
    SDL_PauseAudioDevice(device_, 0);
}

Mixer::~Mixer() {
    if (device_) SDL_CloseAudioDevice(device_);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void Mixer::callback(void* user, uint8_t* stream, int length) {
    static_cast<Mixer*>(user)->mix(reinterpret_cast<int16_t*>(stream), length / 4);
}

void Mixer::mix(int16_t* out, int frames) {
    accumulator_.assign(static_cast<size_t>(frames) * 2, 0);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, v] : voices_) {
            if (v.state != State::Playing || !v.sound || !v.sound->ready) continue;
            const Sound& s = *v.sound;
            const size_t total = s.frames();
            if (!total) {
                v.state = State::Done;
                continue;
            }
            const double step = static_cast<double>(v.rate > 0 ? v.rate : s.rate) / output_rate_;
            // Répartition gauche/droite linéaire, pleine puissance au centre.
            const int left = v.volume * std::min(127, 2 * (127 - v.pan)), right = v.volume * std::min(127, 2 * v.pan);
            for (int i = 0; i < frames; ++i) {
                if (v.cursor >= static_cast<double>(total)) {
                    ++v.loops_done;
                    if (v.loops != 0 && v.loops_done >= v.loops) {
                        v.state = State::Done;
                        break;
                    }
                    v.cursor -= static_cast<double>(total);
                }
                const size_t index = static_cast<size_t>(v.cursor);
                const size_t next = index + 1 < total ? index + 1 : index;
                const double t = v.cursor - static_cast<double>(index);
                for (int c = 0; c < 2; ++c) {
                    const size_t channel = s.channels == 2 ? static_cast<size_t>(c) : 0;
                    const double a = s.samples[index * static_cast<size_t>(s.channels) + channel];
                    const double b = s.samples[next * static_cast<size_t>(s.channels) + channel];
                    accumulator_[static_cast<size_t>(i) * 2 + static_cast<size_t>(c)] +=
                        static_cast<int32_t>((a + (b - a) * t) * (c ? right : left) / (127 * 127));
                }
                v.cursor += step;
            }
        }
    }
    int loudest = peak;
    for (int32_t value : accumulator_) loudest = std::max(loudest, std::abs(value));
    peak = std::min(loudest, 32767);
    for (size_t i = 0; i < accumulator_.size(); ++i)
        out[i] = muted ? 0 : static_cast<int16_t>(std::clamp(accumulator_[i], -32768, 32767));
}

Mixer::Voice* Mixer::find(int id) {
    const auto found = voices_.find(id);
    return found == voices_.end() ? nullptr : &found->second;
}

int Mixer::add_voice() {
    std::lock_guard<std::mutex> lock(mutex_);
    voices_[next_id_];
    return next_id_++;
}

void Mixer::remove_voice(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.erase(id);
}

void Mixer::reset(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* v = find(id)) *v = Voice{};
}

void Mixer::set_sound(int id, std::shared_ptr<Sound> sound) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* v = find(id)) {
        v->sound = std::move(sound);
        v->state = State::Done;
        v->cursor = 0;
        v->loops_done = 0;
        v->rate = 0;
    }
}

void Mixer::start(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* v = find(id)) {
        v->cursor = 0;
        v->loops_done = 0;
        v->state = v->sound ? State::Playing : State::Done;
        ++starts;
    }
}

void Mixer::stop(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* v = find(id))
        if (v->state == State::Playing) v->state = State::Stopped;
}

void Mixer::end(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* v = find(id)) v->state = State::Done;
}

void Mixer::resume(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* v = find(id))
        if (v->state == State::Stopped && v->sound) v->state = State::Playing;
}

void Mixer::set_volume(int id, int volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* v = find(id)) v->volume = std::clamp(volume, 0, 127);
}

void Mixer::set_pan(int id, int pan) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* v = find(id)) v->pan = std::clamp(pan, 0, 127);
}

void Mixer::set_loops(int id, int loops) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* v = find(id)) v->loops = std::max(loops, 0);
}

void Mixer::set_rate(int id, int hertz) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* v = find(id)) v->rate = std::max(hertz, 0);
}

int Mixer::volume(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const Voice* v = find(id);
    return v ? v->volume : 0;
}

int Mixer::rate(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const Voice* v = find(id);
    if (!v) return 0;
    return v->rate > 0 ? v->rate : (v->sound && v->sound->ready ? v->sound->rate : 0);
}

Mixer::State Mixer::state(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const Voice* v = find(id);
    return v ? v->state : State::Done;
}

double Mixer::position(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const Voice* v = find(id);
    if (!v || !v->sound || !v->sound->ready || !v->sound->frames()) return 0;
    return std::min(1.0, v->cursor / static_cast<double>(v->sound->frames()));
}

void Mixer::set_position(int id, double fraction) {
    std::lock_guard<std::mutex> lock(mutex_);
    Voice* v = find(id);
    if (!v || !v->sound || !v->sound->ready) return;
    v->cursor = std::clamp(fraction, 0.0, 1.0) * static_cast<double>(v->sound->frames());
}

}  // namespace zeus::win32
