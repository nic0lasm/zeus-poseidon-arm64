// Visionneuse de sprites : affiche les images des fichiers .sg3 d'une installation du jeu.
//
//   zeus-sprites [--data DOSSIER] [--file NOM.sg3] [--first N] [--zoom N]
//                [--screenshot SORTIE.bmp] [--hash]
//
// --data        dossier d'installation du jeu (défaut : variable ZEUS_DATA_DIR)
// --screenshot  dessine une page hors écran, l'enregistre et quitte
// --hash        décode toutes les images du fichier et affiche une empreinte, sans fenêtre

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "formats/sg3.h"
#include "platform/capture.h"

namespace fs = std::filesystem;
using zeus::formats::Pixels;
using zeus::formats::SgFile;

namespace {

constexpr int kMargin = 6;

struct Options {
    fs::path data_dir;
    std::string file = "Zeus_General.sg3";
    size_t first = 201;
    int zoom = 1;
    fs::path screenshot;
    bool hash = false;
};

struct Sprite {
    size_t index;
    SDL_Texture* texture;
    SDL_Rect rect;
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Dossier DATA de l'installation et liste triée de ses fichiers .sg3.
std::vector<fs::path> find_sg_files(const fs::path& install) {
    fs::path data;
    for (const auto& entry : fs::directory_iterator(install))
        if (entry.is_directory() && lower(entry.path().filename().string()) == "data")
            data = entry.path();
    if (data.empty()) throw std::runtime_error("pas de dossier DATA dans " + install.string());
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(data))
        if (lower(entry.path().extension().string()) == ".sg3") files.push_back(entry.path());
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        return lower(a.filename().string()) < lower(b.filename().string());
    });
    if (files.empty()) throw std::runtime_error("aucun fichier .sg3 dans " + data.string());
    return files;
}

int run_hash(const fs::path& file) {
    SgFile sg(file);
    uLong crc = crc32(0L, Z_NULL, 0);
    size_t failed = 0;
    for (size_t i = 1; i <= sg.image_count(); ++i) {
        try {
            const Pixels p = sg.decode(i);
            const uint32_t w = static_cast<uint32_t>(p.width), h = static_cast<uint32_t>(p.height);
            const uint8_t dims[8] = {
                static_cast<uint8_t>(w), static_cast<uint8_t>(w >> 8), static_cast<uint8_t>(w >> 16),
                static_cast<uint8_t>(w >> 24), static_cast<uint8_t>(h), static_cast<uint8_t>(h >> 8),
                static_cast<uint8_t>(h >> 16), static_cast<uint8_t>(h >> 24)};
            crc = crc32(crc, dims, 8);
            if (!p.rgba.empty()) crc = crc32(crc, p.rgba.data(), static_cast<uInt>(p.rgba.size()));
        } catch (const std::exception&) {
            ++failed;
        }
    }
    std::printf("%s images=%zu echecs=%zu crc32=%08lx\n", file.filename().string().c_str(),
                sg.image_count(), failed, crc);
    return 0;
}

class Viewer {
public:
    Viewer(const Options& options, std::vector<fs::path> files)
        : files_(std::move(files)), zoom_(std::clamp(options.zoom, 1, 4)) {
        const Uint32 flags = options.screenshot.empty()
                                 ? static_cast<Uint32>(SDL_WINDOW_RESIZABLE)
                                 : static_cast<Uint32>(SDL_WINDOW_HIDDEN);
        window_ = SDL_CreateWindow("Zeus", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280,
                                   800, flags);
        if (!window_) throw std::runtime_error(SDL_GetError());
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer_) throw std::runtime_error(SDL_GetError());
        size_t wanted = 0;
        for (size_t i = 0; i < files_.size(); ++i)
            if (lower(files_[i].filename().string()) == lower(options.file)) wanted = i;
        open_file(wanted, options.first);
    }

    ~Viewer() {
        clear_page();
        if (renderer_) SDL_DestroyRenderer(renderer_);
        if (window_) SDL_DestroyWindow(window_);
    }

    void run() {
        bool running = true;
        while (running) {
            SDL_Event e;
            if (!SDL_WaitEvent(&e)) break;
            do {
                if (e.type == SDL_QUIT) running = false;
                if (e.type == SDL_KEYDOWN) running = on_key(e.key.keysym.sym) && running;
                if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    layout(first_);
                if (e.type == SDL_MOUSEMOTION) on_hover(e.motion.x, e.motion.y);
            } while (SDL_PollEvent(&e));
            draw();
        }
    }

    void save_screenshot(const fs::path& out) {
        zeus::platform::save_render_bmp(renderer_, out, [this] { paint(); });
        std::printf("capture : %s (%zu sprites, images %zu à %zu)\n", out.string().c_str(),
                    page_.size(), first_, next_ - 1);
    }

private:
    void open_file(size_t file_index, size_t first) {
        file_index_ = file_index;
        sg_ = std::make_unique<SgFile>(files_[file_index_]);
        history_.clear();
        layout(std::clamp<size_t>(first, 1, std::max<size_t>(sg_->image_count(), 1)));
    }

    void clear_page() {
        for (Sprite& s : page_) SDL_DestroyTexture(s.texture);
        page_.clear();
    }

    // Place les images à partir de `first`, ligne par ligne, jusqu'à remplir la fenêtre.
    void layout(size_t first) {
        clear_page();
        first_ = first;
        errors_ = 0;
        int win_w = 0, win_h = 0;
        SDL_GetRendererOutputSize(renderer_, &win_w, &win_h);
        int x = kMargin, y = kMargin, row_h = 0;
        size_t i = first;
        for (; i <= sg_->image_count(); ++i) {
            Pixels p;
            try {
                p = sg_->decode(i);
            } catch (const std::exception&) {
                ++errors_;
                continue;
            }
            if (p.width == 0 || p.height == 0) continue;
            const int w = p.width * zoom_, h = p.height * zoom_;
            if (x + w + kMargin > win_w && x > kMargin) {
                x = kMargin;
                y += row_h + kMargin;
                row_h = 0;
            }
            if (y + h + kMargin > win_h && !page_.empty()) break;
            SDL_Texture* t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                               SDL_TEXTUREACCESS_STATIC, p.width, p.height);
            if (!t) throw std::runtime_error(SDL_GetError());
            SDL_UpdateTexture(t, nullptr, p.rgba.data(), p.width * 4);
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            page_.push_back({i, t, {x, y, w, h}});
            x += w + kMargin;
            row_h = std::max(row_h, h);
        }
        next_ = i;
        hover_.clear();
        update_title();
    }

    bool on_key(SDL_Keycode key) {
        switch (key) {
            case SDLK_ESCAPE:
            case SDLK_q:
                return false;
            case SDLK_RIGHT:
            case SDLK_SPACE:
                if (next_ <= sg_->image_count()) {
                    history_.push_back(first_);
                    layout(next_);
                }
                break;
            case SDLK_LEFT:
                if (!history_.empty()) {
                    const size_t previous = history_.back();
                    history_.pop_back();
                    layout(previous);
                } else if (first_ > 1) {
                    layout(1);
                }
                break;
            case SDLK_DOWN:
                open_file((file_index_ + 1) % files_.size(), 1);
                break;
            case SDLK_UP:
                open_file((file_index_ + files_.size() - 1) % files_.size(), 1);
                break;
            case SDLK_EQUALS:
            case SDLK_PLUS:
            case SDLK_KP_PLUS:
                zoom_ = std::min(zoom_ + 1, 4);
                layout(first_);
                break;
            case SDLK_MINUS:
            case SDLK_KP_MINUS:
                zoom_ = std::max(zoom_ - 1, 1);
                layout(first_);
                break;
            case SDLK_HOME:
                history_.clear();
                layout(1);
                break;
            default:
                break;
        }
        return true;
    }

    void on_hover(int mx, int my) {
        std::string text;
        const SDL_Point point{mx, my};
        for (const Sprite& s : page_) {
            if (!SDL_PointInRect(&point, &s.rect)) continue;
            const auto& im = sg_->image(s.index);
            text = "  ·  image " + std::to_string(s.index) + " : " +
                   std::to_string(s.rect.w / zoom_) + "×" + std::to_string(s.rect.h / zoom_) +
                   ", type " + std::to_string(im.type) + (im.invert_offset ? ", miroir" : "");
            break;
        }
        if (text != hover_) {
            hover_ = text;
            update_title();
        }
    }

    void update_title() {
        std::string title = "Zeus — sprites  ·  " + files_[file_index_].filename().string() + " (" +
                            std::to_string(file_index_ + 1) + "/" + std::to_string(files_.size()) +
                            ")  ·  images " + std::to_string(first_) + " à " +
                            std::to_string(next_ - 1) + " sur " +
                            std::to_string(sg_->image_count()) + "  ·  zoom ×" +
                            std::to_string(zoom_);
        if (errors_) title += "  ·  " + std::to_string(errors_) + " illisibles";
        title += hover_.empty() ? "  ·  ←/→ page  ↑/↓ fichier  +/- zoom  Échap quitter" : hover_;
        SDL_SetWindowTitle(window_, title.c_str());
    }

    void paint() {
        SDL_SetRenderDrawColor(renderer_, 48, 52, 60, 255);
        SDL_RenderClear(renderer_);
        for (const Sprite& s : page_) SDL_RenderCopy(renderer_, s.texture, nullptr, &s.rect);
    }

    void draw() {
        paint();
        SDL_RenderPresent(renderer_);
    }

    std::vector<fs::path> files_;
    size_t file_index_ = 0;
    std::unique_ptr<SgFile> sg_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    std::vector<Sprite> page_;
    std::vector<size_t> history_;
    size_t first_ = 1, next_ = 1, errors_ = 0;
    int zoom_ = 1;
    std::string hover_;
};

Options parse(int argc, char** argv) {
    Options o;
    if (const char* env = std::getenv("ZEUS_DATA_DIR")) o.data_dir = env;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("valeur manquante après " + a);
            return argv[++i];
        };
        if (a == "--data") o.data_dir = value();
        else if (a == "--file") o.file = value();
        else if (a == "--first") o.first = std::stoul(value());
        else if (a == "--zoom") o.zoom = std::stoi(value());
        else if (a == "--screenshot") o.screenshot = value();
        else if (a == "--hash") o.hash = true;
        else throw std::runtime_error("option inconnue : " + a);
    }
    if (o.data_dir.empty())
        throw std::runtime_error("indiquer l'installation du jeu avec --data ou ZEUS_DATA_DIR");
    return o;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse(argc, argv);
        std::vector<fs::path> files = find_sg_files(options.data_dir);
        if (options.hash) {
            for (const fs::path& f : files)
                if (lower(f.filename().string()) == lower(options.file)) return run_hash(f);
            throw std::runtime_error("fichier introuvable : " + options.file);
        }
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // pixels nets en zoom
        {
            Viewer viewer(options, std::move(files));
            if (options.screenshot.empty()) viewer.run();
            else viewer.save_screenshot(options.screenshot);
        }
        SDL_Quit();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "zeus-sprites : %s\n", e.what());
        return 1;
    }
}
