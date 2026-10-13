// BINKW32 : lecture des vidéos Bink du jeu, décodées par ffmpeg.
//
// Le jeu (FUN_00418710) ouvre la vidéo, puis pour chaque image appelle BinkDoFrame,
// BinkNextFrame et BinkCopyToBuffer vers la surface primaire verrouillée, et patiente avec
// BinkWait en traitant ses messages. Il ne lit de la structure BINK que ses quatre premiers
// champs : largeur, hauteur, nombre d'images, numéro de l'image courante.
// La piste son est décodée d'un bloc en arrière-plan et jouée par le mixeur de l'hôte.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <thread>

#include "win32/process.h"

namespace zeus::win32 {
namespace {

using Clock = std::chrono::steady_clock;

// Décode toute la piste son du fichier en 16 bits stéréo.
void decode_audio(const std::string& path, Sound& out) {
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0) return;
    AVCodecContext* codec = nullptr;
    SwrContext* resampler = nullptr;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    const int stream = avformat_find_stream_info(format, nullptr) < 0
                           ? -1 : av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream >= 0) {
        const AVCodecParameters* parameters = format->streams[stream]->codecpar;
        codec = avcodec_alloc_context3(avcodec_find_decoder(parameters->codec_id));
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        if (codec && avcodec_parameters_to_context(codec, parameters) >= 0 &&
            avcodec_open2(codec, nullptr, nullptr) >= 0 &&
            swr_alloc_set_opts2(&resampler, &stereo, AV_SAMPLE_FMT_S16, codec->sample_rate, &codec->ch_layout,
                                codec->sample_fmt, codec->sample_rate, 0, nullptr) >= 0 &&
            swr_init(resampler) >= 0) {
            out.rate = codec->sample_rate;
            out.channels = 2;
            while (av_read_frame(format, packet) >= 0) {
                if (packet->stream_index == stream && avcodec_send_packet(codec, packet) >= 0) {
                    while (avcodec_receive_frame(codec, frame) >= 0) {
                        const size_t start = out.samples.size();
                        out.samples.resize(start + static_cast<size_t>(frame->nb_samples) * 2);
                        uint8_t* target = reinterpret_cast<uint8_t*>(&out.samples[start]);
                        const int done = swr_convert(resampler, &target, frame->nb_samples,
                                                     const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
                        out.samples.resize(start + static_cast<size_t>(std::max(done, 0)) * 2);
                    }
                }
                av_packet_unref(packet);
            }
        }
    }
    av_frame_free(&frame);
    av_packet_free(&packet);
    if (resampler) swr_free(&resampler);
    if (codec) avcodec_free_context(&codec);
    avformat_close_input(&format);
}

class Video {
public:
    explicit Video(const std::string& path) {
        if (avformat_open_input(&format_, path.c_str(), nullptr, nullptr) < 0) return;
        if (avformat_find_stream_info(format_, nullptr) < 0) return;
        stream_ = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (stream_ < 0) return;
        const AVStream* s = format_->streams[stream_];
        codec_ = avcodec_alloc_context3(avcodec_find_decoder(s->codecpar->codec_id));
        if (!codec_ || avcodec_parameters_to_context(codec_, s->codecpar) < 0 || avcodec_open2(codec_, nullptr, nullptr) < 0) return;
        width = codec_->width;
        height = codec_->height;
        const AVRational rate = s->avg_frame_rate.num ? s->avg_frame_rate : s->r_frame_rate;
        fps = rate.den ? static_cast<double>(rate.num) / rate.den : 15.0;
        // En-tête Bink : nombre d'images à l'octet 8 ; bits 28 à 31 des indicateurs vidéo
        // (octet 0x27) : 1 ou 2, hauteur doublée.
        std::ifstream in(path, std::ios::binary);
        uint8_t header[0x28] = {};
        in.read(reinterpret_cast<char*>(header), sizeof header);
        frames = header[8] | header[9] << 8 | header[10] << 16 | header[11] << 24;
        const int scaling = header[0x27] >> 4;
        line_repeat = scaling == 1 || scaling == 2 ? 2 : 1;
        scaler_ = sws_getContext(width, height, codec_->pix_fmt, width, height, AV_PIX_FMT_RGB555LE, SWS_POINT,
                                 nullptr, nullptr, nullptr);
        packet_ = av_packet_alloc();
        frame_ = av_frame_alloc();
        pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 2);
        ok = scaler_ && frames > 0 && width > 0 && height > 0;
    }
    ~Video() {
        av_frame_free(&frame_);
        av_packet_free(&packet_);
        if (scaler_) sws_freeContext(scaler_);
        if (codec_) avcodec_free_context(&codec_);
        if (format_) avformat_close_input(&format_);
    }
    Video(const Video&) = delete;
    Video& operator=(const Video&) = delete;

    // Décode l'image suivante dans `pixels` (5-5-5, de haut en bas).
    void decode_next() {
        while (av_read_frame(format_, packet_) >= 0) {
            const bool ours = packet_->stream_index == stream_;
            if (ours) avcodec_send_packet(codec_, packet_);
            av_packet_unref(packet_);
            if (ours && avcodec_receive_frame(codec_, frame_) >= 0) {
                uint8_t* planes[4] = {pixels.data(), nullptr, nullptr, nullptr};
                int pitches[4] = {width * 2, 0, 0, 0};
                sws_scale(scaler_, frame_->data, frame_->linesize, 0, height, planes, pitches);
                return;
            }
        }
    }

    bool ok = false;
    int width = 0, height = 0, frames = 0, line_repeat = 1;
    double fps = 15;
    std::vector<uint8_t> pixels;
    // Lecture en cours.
    int shown = 0;  // images déjà décodées
    Clock::time_point start{};
    bool started = false, paused = false;
    Clock::time_point paused_at{};
    int voice = 0;

private:
    AVFormatContext* format_ = nullptr;
    AVCodecContext* codec_ = nullptr;
    SwsContext* scaler_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    int stream_ = -1;
};

struct State {
    std::map<uint32_t, std::unique_ptr<Video>> videos;  // adresse émulée de la structure BINK
    uint32_t no_error = 0;                              // chaîne vide rendue par BinkGetError
    bool sound = false;                                 // le jeu a branché le son de la vidéo
};

}  // namespace

void install_bink(Process& p) {
    auto st = std::make_shared<State>();
    Cpu& cpu = p.cpu();
    p.allow_silent_stubs("binkw32.dll");
    auto def = [&p](const char* name, uint32_t args, std::function<uint32_t()> fn) {
        p.def("binkw32.dll", name, args * 4, std::move(fn));
    };
    auto video = [st](uint32_t address) -> Video* {
        const auto found = st->videos.find(address);
        return found == st->videos.end() ? nullptr : found->second.get();
    };

    def("_BinkSetSoundSystem@8", 2, [st] {
        st->sound = true;
        return 1u;
    });
    // BinkOpen(nom de fichier, indicateurs) : structure BINK, ou 0.
    def("_BinkOpen@8", 2, [&p, &cpu, st] {
        const std::string name = cpu.read_string(p.arg(0));
        const auto path = p.vfs().find(name);
        if (p.trace) std::fprintf(stderr, "    vidéo : %s%s\n", name.c_str(), path ? "" : " (absente)");
        if (!path) return 0u;
        auto v = std::make_unique<Video>(path->string());
        if (!v->ok) {
            std::fprintf(stderr, "[hôte] vidéo illisible : %s\n", name.c_str());
            return 0u;
        }
        if (st->sound)
            if (Mixer* mixer = p.mixer()) {
                auto sound = std::make_shared<Sound>();
                std::thread([sound, file = path->string()] {
                    decode_audio(file, *sound);
                    sound->ready = true;
                }).detach();
                v->voice = mixer->add_voice();
                mixer->set_sound(v->voice, std::move(sound));
            }
        const uint32_t bink = p.alloc(256);
        cpu.write_u32(bink + 0, static_cast<uint32_t>(v->width));
        cpu.write_u32(bink + 4, static_cast<uint32_t>(v->height * v->line_repeat));
        cpu.write_u32(bink + 8, static_cast<uint32_t>(v->frames));
        cpu.write_u32(bink + 12, 1);  // les images se comptent à partir de 1
        st->videos[bink] = std::move(v);
        return bink;
    });
    def("_BinkClose@4", 1, [&p, st] {
        const auto found = st->videos.find(p.arg(0));
        if (found == st->videos.end()) return 0u;
        if (found->second->voice)
            if (Mixer* mixer = p.mixer()) mixer->remove_voice(found->second->voice);
        st->videos.erase(found);
        p.release(p.arg(0));
        return 0u;
    });
    def("_BinkBufferClose@4", 1, [] { return 0u; });
    // Résumé de lecture, que le jeu ne fait qu'écrire dans son journal : tout à zéro.
    def("_BinkGetSummary@8", 2, [&p, &cpu] {
        if (p.arg(1)) cpu.fill(p.arg(1), 0, 124);
        return 0u;
    });
    def("_BinkGetError@0", 0, [&p, st] {
        if (!st->no_error) st->no_error = p.alloc_string("");
        return st->no_error;
    });
    def("_BinkDDSurfaceType@4", 1, [] { return 9u; });  // BINKSURFACE555
    def("_BinkService@4", 1, [] { return 0u; });

    def("_BinkDoFrame@4", 1, [&p, video] {
        Video* v = video(p.arg(0));
        if (!v) return 0u;
        if (!v->started) {
            v->started = true;
            v->start = Clock::now();
            if (v->voice)
                if (Mixer* mixer = p.mixer()) mixer->start(v->voice);
        }
        v->decode_next();
        ++v->shown;
        return 0u;
    });
    def("_BinkNextFrame@4", 1, [&p, &cpu, video] {
        if (video(p.arg(0))) cpu.write_u32(p.arg(0) + 12, cpu.read_u32(p.arg(0) + 12) + 1);
        return 0u;
    });
    // BinkCopyToBuffer(bink, destination, octets par ligne, hauteur, x, y, type de surface).
    def("_BinkCopyToBuffer@28", 7, [&p, &cpu, video] {
        Video* v = video(p.arg(0));
        if (!v) return 0u;
        const uint32_t pitch = p.arg(2);
        const int lines = std::min(static_cast<int>(p.arg(3)), v->height * v->line_repeat);
        const uint32_t origin = p.arg(1) + p.arg(5) * pitch + p.arg(4) * 2;
        for (int y = 0; y < lines; ++y)
            cpu.write(origin + static_cast<uint32_t>(y) * pitch,
                      &v->pixels[static_cast<size_t>(y / v->line_repeat) * static_cast<size_t>(v->width) * 2],
                      static_cast<size_t>(v->width) * 2);
        return 0u;
    });
    // BinkWait : non nul tant qu'il est trop tôt pour l'image suivante.
    def("_BinkWait@4", 1, [&p, video] {
        Video* v = video(p.arg(0));
        if (!v || !v->started || v->paused) return 0u;
        const auto due = v->start + std::chrono::duration_cast<Clock::duration>(
                                        std::chrono::duration<double>(v->shown / v->fps));
        if (Clock::now() >= due) return 0u;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return 1u;
    });
    def("_BinkPause@8", 2, [&p, video] {
        Video* v = video(p.arg(0));
        if (!v) return 0u;
        const bool pause = p.arg(1) != 0;
        if (pause == v->paused) return 0u;
        Mixer* mixer = v->voice ? p.mixer() : nullptr;
        if (pause) {
            v->paused_at = Clock::now();
            if (mixer) mixer->stop(v->voice);
        } else {
            v->start += Clock::now() - v->paused_at;
            if (mixer) mixer->resume(v->voice);
        }
        v->paused = pause;
        return 0u;
    });
}

}  // namespace zeus::win32
