// Affiche le terrain d'une carte du jeu en vue isométrique.
//
//   zeus-map [--data DOSSIER] [--map NOM] [--zoom Z] [--size LxH] [--center X,Y]
//            [--screenshot SORTIE.bmp] [--hash]
//
// --data        dossier d'installation du jeu (défaut : variable ZEUS_DATA_DIR)
// --map         chemin d'un .map, ou fragment de son nom parmi ceux de l'installation
// --screenshot  dessine la vue hors écran, l'enregistre et quitte
// --hash        lit la carte et affiche l'empreinte de ses blocs, sans fenêtre

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "formats/mapfile.h"
#include "game/image_store.h"
#include "game/terrain_view.h"
#include "platform/capture.h"

namespace fs = std::filesystem;
using zeus::formats::MapFile;
using zeus::game::ImageStore;
using zeus::game::TerrainSprite;
using zeus::game::TerrainView;

namespace {

struct Options {
    fs::path data_dir;
    std::string map;
    double zoom = 1.0;
    int width = 1280, height = 800;
    int center_tile_x = -1, center_tile_y = -1;
    fs::path screenshot;
    bool hash = false;
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

fs::path find_subdir(const fs::path& parent, const std::string& name) {
    for (const auto& entry : fs::directory_iterator(parent))
        if (entry.is_directory() && lower(entry.path().filename().string()) == name)
            return entry.path();
    throw std::runtime_error("pas de dossier " + name + " dans " + parent.string());
}

std::vector<fs::path> find_maps(const fs::path& install) {
    std::vector<fs::path> maps;
    for (const auto& entry : fs::recursive_directory_iterator(find_subdir(install, "adventures")))
        if (entry.is_regular_file() && lower(entry.path().extension().string()) == ".map")
            maps.push_back(entry.path());
    std::sort(maps.begin(), maps.end());
    if (maps.empty()) throw std::runtime_error("aucune carte .map dans l'installation");
    return maps;
}

int run_hash(const fs::path& path) {
    const MapFile map(path);
    uLong crc = crc32(0L, Z_NULL, 0);
    size_t bytes = 0;
    for (const auto& c : map.chunks()) {
        if (!c.data.empty()) crc = crc32(crc, c.data.data(), static_cast<uInt>(c.data.size()));
        bytes += c.data.size();
    }
    std::printf("%s blocs=%zu octets=%zu crc32=%08lx\n", path.filename().string().c_str(),
                map.chunks().size(), bytes, crc);
    return 0;
}

class Viewer {
public:
    Viewer(const Options& options, std::vector<fs::path> maps)
        : maps_(std::move(maps)), images_(find_subdir(options.data_dir, "data")), zoom_(options.zoom) {
        const Uint32 flags = options.screenshot.empty() ? SDL_WINDOW_RESIZABLE
                                                        : static_cast<Uint32>(SDL_WINDOW_HIDDEN);
        window_ = SDL_CreateWindow("Zeus", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   options.width, options.height, flags);
        if (!window_) throw std::runtime_error(SDL_GetError());
        renderer_ =
            SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer_) throw std::runtime_error(SDL_GetError());
        size_t wanted = 0;
        if (!options.map.empty()) {
            bool found = false;
            for (size_t i = 0; i < maps_.size() && !found; ++i)
                if (lower(maps_[i].string()).find(lower(options.map)) != std::string::npos) {
                    wanted = i;
                    found = true;
                }
            if (!found) {
                if (!fs::exists(options.map)) throw std::runtime_error("carte introuvable : " + options.map);
                maps_.insert(maps_.begin(), options.map);
            }
        }
        open_map(wanted);
        if (options.center_tile_x >= 0) {
            center_x_ = zeus::game::tile_screen_x(options.center_tile_x, options.center_tile_y) + 29;
            center_y_ = zeus::game::tile_screen_y(options.center_tile_x, options.center_tile_y) + 15;
        }
    }

    ~Viewer() {
        clear_textures();
        if (renderer_) SDL_DestroyRenderer(renderer_);
        if (window_) SDL_DestroyWindow(window_);
    }

    void run() {
        bool running = true;
        Uint32 last = SDL_GetTicks();
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) running = false;
                if (e.type == SDL_KEYDOWN) running = on_key(e.key.keysym.sym) && running;
                if (e.type == SDL_MOUSEWHEEL && e.wheel.y != 0) set_zoom(zoom_ * (e.wheel.y > 0 ? 1.25 : 0.8));
                if (e.type == SDL_MOUSEMOTION) {
                    if (e.motion.state & SDL_BUTTON_LMASK) {
                        center_x_ -= e.motion.xrel / zoom_;
                        center_y_ -= e.motion.yrel / zoom_;
                    }
                    mouse_x_ = e.motion.x;
                    mouse_y_ = e.motion.y;
                    update_title();
                }
            }
            const Uint32 now = SDL_GetTicks();
            const double step = 1.2 * (now - last) / zoom_;  // pixels de carte par milliseconde
            last = now;
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) center_x_ -= step;
            if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) center_x_ += step;
            if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) center_y_ -= step;
            if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) center_y_ += step;
            paint();
            SDL_RenderPresent(renderer_);
        }
    }

    void save_screenshot(const fs::path& out) {
        zeus::platform::save_render_bmp(renderer_, out, [this] { paint(); });
        std::printf("capture : %s (%s, zoom %.2f, %zu images dessinées, %zu cases ignorées)\n",
                    out.string().c_str(), maps_[map_index_].filename().string().c_str(), zoom_,
                    drawn_, view_.skipped);
    }

private:
    void clear_textures() {
        for (auto& [id, texture] : textures_)
            if (texture) SDL_DestroyTexture(texture);
        textures_.clear();
    }

    void open_map(size_t index) {
        map_index_ = index;
        const MapFile map(maps_[index]);
        view_ = zeus::game::build_terrain_view(map, images_);
        center_x_ = (view_.min_x + view_.max_x) / 2.0;
        center_y_ = (view_.min_y + view_.max_y) / 2.0;
        update_title();
    }

    // Texture d'une partie d'image (socle ou partie haute), créée à la première utilisation.
    SDL_Texture* texture(uint32_t id, zeus::formats::SgFile::Part part) {
        const uint64_t key = static_cast<uint64_t>(id) << 2 | static_cast<uint64_t>(part);
        auto found = textures_.find(key);
        if (found != textures_.end()) return found->second;
        SDL_Texture* t = nullptr;
        try {
            const zeus::formats::Pixels p = images_.decode(id, part);
            if (p.width > 0 && p.height > 0) {
                t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                      p.width, p.height);
                if (!t) throw std::runtime_error(SDL_GetError());
                SDL_UpdateTexture(t, nullptr, p.rgba.data(), p.width * 4);
                SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            }
        } catch (const std::exception&) {
            t = nullptr;  // image illisible : la case reste vide
        }
        textures_.emplace(key, t);
        return t;
    }

    void paint() {
        int out_w = 0, out_h = 0;
        SDL_GetRendererOutputSize(renderer_, &out_w, &out_h);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        const double left = center_x_ - out_w / (2 * zoom_), top = center_y_ - out_h / (2 * zoom_);
        const double right = left + out_w / zoom_, bottom = top + out_h / zoom_;
        const SDL_ScaleMode mode = zoom_ < 1.0 ? SDL_ScaleModeLinear : SDL_ScaleModeNearest;
        drawn_ = 0;
        // Une seule passe, du fond vers l'avant, image entière. Le jeu sépare socles et parties
        // hautes et diffère certains dessins selon l'altitude des cases voisines (0x0047499F) ;
        // cette logique n'est pas encore portée. Dessiner tous les socles puis toutes les
        // parties hautes sans elle donne un résultat pire : les falaises du fond passent devant
        // les plateaux.
        using Part = zeus::formats::SgFile::Part;
        for (const TerrainSprite& s : view_.sprites) {
            if (s.x + s.width < left || s.x > right || s.y + s.height < top || s.y > bottom) continue;
            SDL_Texture* t = texture(s.image_id, Part::Whole);
            if (!t) continue;
            SDL_SetTextureScaleMode(t, mode);
            // Bords arrondis de façon cohérente pour que les cases voisines restent jointives.
            const int x0 = static_cast<int>(std::floor((s.x - left) * zoom_));
            const int y0 = static_cast<int>(std::floor((s.y - top) * zoom_));
            const int x1 = static_cast<int>(std::floor((s.x + s.width - left) * zoom_));
            const int y1 = static_cast<int>(std::floor((s.y + s.height - top) * zoom_));
            const SDL_Rect dst{x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
            SDL_RenderCopy(renderer_, t, nullptr, &dst);
            ++drawn_;
        }
    }

    void set_zoom(double zoom) {
        zoom_ = std::clamp(zoom, 0.05, 4.0);
        update_title();
    }

    bool on_key(SDL_Keycode key) {
        switch (key) {
            case SDLK_ESCAPE:
            case SDLK_q:
                return false;
            case SDLK_EQUALS:
            case SDLK_PLUS:
            case SDLK_KP_PLUS:
                set_zoom(zoom_ >= 1 ? zoom_ + 1 : zoom_ * 2);
                break;
            case SDLK_MINUS:
            case SDLK_KP_MINUS:
                set_zoom(zoom_ > 1 ? zoom_ - 1 : zoom_ / 2);
                break;
            case SDLK_1:
                set_zoom(1.0);
                break;
            case SDLK_PAGEDOWN:
            case SDLK_n:
                open_map((map_index_ + 1) % maps_.size());
                break;
            case SDLK_PAGEUP:
            case SDLK_p:
                open_map((map_index_ + maps_.size() - 1) % maps_.size());
                break;
            case SDLK_HOME:
                open_map(map_index_);
                break;
            default:
                break;
        }
        return true;
    }

    void update_title() {
        int out_w = 0, out_h = 0;
        SDL_GetRendererOutputSize(renderer_, &out_w, &out_h);
        // Case sous le curseur : inverse de la projection, centre du losange en (+29, +15).
        const double wx = center_x_ + (mouse_x_ - out_w / 2.0) / zoom_;
        const double wy = center_y_ + (mouse_y_ - out_h / 2.0) / zoom_;
        const double u = (wx - 29) / zeus::game::kTileStepX, v = (wy - 15) / zeus::game::kTileStepY;
        const int tx = static_cast<int>(std::lround((v + u) / 2)), ty = static_cast<int>(std::lround((v - u) / 2));
        char zoom[16];
        std::snprintf(zoom, sizeof zoom, "%.2f", zoom_);
        std::string title = "Zeus — carte  ·  " + maps_[map_index_].stem().string() + " (" +
                            std::to_string(map_index_ + 1) + "/" + std::to_string(maps_.size()) +
                            ")  ·  zoom ×" + zoom + "  ·  case " + std::to_string(tx) + "," +
                            std::to_string(ty) +
                            "  ·  flèches ou glisser : défiler  +/- : zoom  N/P : carte  Échap";
        SDL_SetWindowTitle(window_, title.c_str());
    }

    std::vector<fs::path> maps_;
    size_t map_index_ = 0;
    ImageStore images_;
    TerrainView view_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    std::map<uint64_t, SDL_Texture*> textures_;
    double center_x_ = 0, center_y_ = 0, zoom_ = 1.0;
    int mouse_x_ = 0, mouse_y_ = 0;
    size_t drawn_ = 0;
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
        else if (a == "--map") o.map = value();
        else if (a == "--zoom") o.zoom = std::stod(value());
        else if (a == "--size") {
            const std::string v = value();
            const size_t x = v.find('x');
            if (x == std::string::npos) throw std::runtime_error("--size attend LARGEURxHAUTEUR");
            o.width = std::stoi(v.substr(0, x));
            o.height = std::stoi(v.substr(x + 1));
        }
        else if (a == "--center") {
            const std::string v = value();
            const size_t comma = v.find(',');
            if (comma == std::string::npos) throw std::runtime_error("--center attend X,Y (case)");
            o.center_tile_x = std::stoi(v.substr(0, comma));
            o.center_tile_y = std::stoi(v.substr(comma + 1));
        }
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
        std::vector<fs::path> maps = find_maps(options.data_dir);
        if (options.hash) {
            for (const fs::path& m : maps)
                if (options.map.empty() || lower(m.string()).find(lower(options.map)) != std::string::npos)
                    run_hash(m);
            return 0;
        }
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
        {
            Viewer viewer(options, std::move(maps));
            if (options.screenshot.empty()) viewer.run();
            else viewer.save_screenshot(options.screenshot);
        }
        SDL_Quit();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "zeus-map : %s\n", e.what());
        return 1;
    }
}
