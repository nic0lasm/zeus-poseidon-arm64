#include "win32/desktop.h"

#include "win32/text.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace zeus::win32 {
namespace {

// Messages Windows produits à partir des événements SDL.
constexpr uint32_t WM_CLOSE = 0x0010, WM_KEYDOWN = 0x0100, WM_KEYUP = 0x0101, WM_CHAR = 0x0102,
                   WM_MOUSEMOVE = 0x0200, WM_LBUTTONDOWN = 0x0201, WM_LBUTTONUP = 0x0202,
                   WM_RBUTTONDOWN = 0x0204, WM_RBUTTONUP = 0x0205, WM_MBUTTONDOWN = 0x0207,
                   WM_MBUTTONUP = 0x0208;
constexpr uint32_t MK_LBUTTON = 1, MK_RBUTTON = 2, MK_MBUTTON = 0x10;

// Code de touche virtuelle Windows pour une touche SDL, ou 0.
int virtual_key(SDL_Keycode key) {
    if (key >= SDLK_a && key <= SDLK_z) return 'A' + (key - SDLK_a);
    if (key >= SDLK_0 && key <= SDLK_9) return '0' + (key - SDLK_0);
    if (key >= SDLK_F1 && key <= SDLK_F12) return 0x70 + (key - SDLK_F1);
    switch (key) {
        case SDLK_LEFT: return 0x25;
        case SDLK_UP: return 0x26;
        case SDLK_RIGHT: return 0x27;
        case SDLK_DOWN: return 0x28;
        case SDLK_ESCAPE: return 0x1B;
        case SDLK_RETURN: case SDLK_KP_ENTER: return 0x0D;
        case SDLK_SPACE: return 0x20;
        case SDLK_BACKSPACE: return 0x08;
        case SDLK_TAB: return 0x09;
        case SDLK_LSHIFT: case SDLK_RSHIFT: return 0x10;
        case SDLK_LCTRL: case SDLK_RCTRL: return 0x11;
        case SDLK_LALT: case SDLK_RALT: return 0x12;
        case SDLK_PAUSE: return 0x13;
        case SDLK_PAGEUP: return 0x21;
        case SDLK_PAGEDOWN: return 0x22;
        case SDLK_END: return 0x23;
        case SDLK_HOME: return 0x24;
        case SDLK_INSERT: return 0x2D;
        case SDLK_DELETE: return 0x2E;
        case SDLK_KP_PLUS: return 0x6B;
        case SDLK_KP_MINUS: return 0x6D;
        case SDLK_MINUS: return 0xBD;
        case SDLK_EQUALS: return 0xBB;
        case SDLK_COMMA: return 0xBC;
        case SDLK_PERIOD: return 0xBE;
        case SDLK_LEFTBRACKET: return 0xDB;
        case SDLK_RIGHTBRACKET: return 0xDD;
        default: return 0;
    }
}

}  // namespace

Desktop::Desktop() : keys_(256, false) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    start_ticks_ = SDL_GetTicks64();
}

Desktop::~Desktop() {
    if (frame_log_) std::fclose(frame_log_);
    if (upscaled_) SDL_DestroyTexture(upscaled_);
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

double Desktop::screen_aspect() const {
    SDL_DisplayMode mode;
    if (SDL_GetDesktopDisplayMode(0, &mode) != 0 || mode.w <= 0 || mode.h <= 0) return 4.0 / 3.0;
    return static_cast<double>(mode.w) / mode.h;
}

void Desktop::create_window(const std::string& title, int width, int height) {
    if (window_) return;
    if (width > 0 && height > 0) {
        width_ = width;
        height_ = height;
    }
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
    if (headless) flags |= SDL_WINDOW_HIDDEN;
    else if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width_,
                               height_, flags);
    if (!window_) throw std::runtime_error(SDL_GetError());
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) throw std::runtime_error(SDL_GetError());
}

void Desktop::set_mode(int width, int height) {
    width_ = width;
    height_ = height;
    if (window_ && !fullscreen) {
        SDL_SetWindowSize(window_, width, height);
        SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
}

void Desktop::present(const uint8_t* pixels, int width, int height, int pitch, bool rgb565) {
    if (!window_) create_window("Zeus", width, height);
    if (!texture_ || texture_w_ != width || texture_h_ != height || texture_565_ != rgb565) {
        if (texture_) SDL_DestroyTexture(texture_);
        texture_ = SDL_CreateTexture(renderer_, rgb565 ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_RGB555,
                                     SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!texture_) throw std::runtime_error(SDL_GetError());
        SDL_SetTextureScaleMode(texture_, SDL_ScaleModeNearest);
        if (upscaled_) SDL_DestroyTexture(upscaled_);
        upscaled_ = nullptr;
        texture_w_ = width;
        texture_h_ = height;
        texture_565_ = rgb565;
    }
    if (!frame_log_path.empty()) {
        if (!frame_log_) frame_log_ = std::fopen(frame_log_path.c_str(), "w");
        uint32_t hash = 2166136261u;  // FNV-1a des pixels
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width * 2; ++x) hash = (hash ^ pixels[static_cast<size_t>(y) * pitch + x]) * 16777619u;
        if (frame_log_) std::fprintf(frame_log_, "%d %dx%d %08x\n", presents_, width, height, hash);
    }
    // Fenêtre cachée sans capture de son contenu : rien à dessiner.
    if (!headless || !window_shot_path.empty()) {
        SDL_UpdateTexture(texture_, nullptr, pixels, pitch);

        // L'image garde ses proportions et se centre dans la fenêtre. À un facteur entier, chaque
        // pixel du jeu devient un carré net. Sinon on agrandit d'abord au facteur entier supérieur,
        // sans lissage, puis on réduit avec lissage : les pixels restent nets et réguliers.
        int out_w = 0, out_h = 0, win_w = 0, win_h = 0;
        SDL_GetRendererOutputSize(renderer_, &out_w, &out_h);
        SDL_GetWindowSize(window_, &win_w, &win_h);
        const double scale = std::min(static_cast<double>(out_w) / width, static_cast<double>(out_h) / height);
        const SDL_Rect target = {static_cast<int>((out_w - width * scale) / 2), static_cast<int>((out_h - height * scale) / 2),
                                 static_cast<int>(width * scale), static_cast<int>(height * scale)};
        const double density = win_w > 0 ? static_cast<double>(out_w) / win_w : 1.0;
        view_scale_ = scale / density;
        view_x_ = target.x / density;
        view_y_ = target.y / density;
        const int factor = static_cast<int>(std::ceil(scale - 1e-6));
        SDL_RenderClear(renderer_);
        if (factor <= 1 || std::abs(scale - factor) < 1e-6) {
            SDL_RenderCopy(renderer_, texture_, nullptr, &target);
        } else {
            if (!upscaled_ || upscaled_factor_ != factor) {
                if (upscaled_) SDL_DestroyTexture(upscaled_);
                upscaled_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_TARGET, width * factor,
                                              height * factor);
                if (!upscaled_) throw std::runtime_error(SDL_GetError());
                SDL_SetTextureScaleMode(upscaled_, SDL_ScaleModeLinear);
                upscaled_factor_ = factor;
            }
            SDL_SetRenderTarget(renderer_, upscaled_);
            SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
            SDL_SetRenderTarget(renderer_, nullptr);
            SDL_RenderCopy(renderer_, upscaled_, nullptr, &target);
        }
        if (!window_shot_path.empty() && (presents_ % 30) == 0) {
            if (SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(0, out_w, out_h, 24, SDL_PIXELFORMAT_RGB24)) {
                if (SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_RGB24, shot->pixels, shot->pitch) == 0)
                    SDL_SaveBMP(shot, window_shot_path.c_str());
                SDL_FreeSurface(shot);
            }
        }
        SDL_RenderPresent(renderer_);
    }
    ++presents_;

    if (!screenshot_path.empty() || screenshot_wanted_) {
        // Conversion en RGB24 selon la routine du jeu (0x00418A00) : composante << 3.
        last_frame_.resize(static_cast<size_t>(width) * height * 3);
        for (int y = 0; y < height; ++y) {
            const uint8_t* row = pixels + static_cast<size_t>(y) * pitch;
            uint8_t* out = &last_frame_[static_cast<size_t>(y) * width * 3];
            for (int x = 0; x < width; ++x, out += 3) {
                const uint16_t c = static_cast<uint16_t>(row[2 * x] | row[2 * x + 1] << 8);
                if (rgb565) {
                    out[0] = static_cast<uint8_t>((c >> 11) << 3);
                    out[1] = static_cast<uint8_t>(((c >> 5) & 63) << 2);
                } else {
                    out[0] = static_cast<uint8_t>(((c >> 10) & 31) << 3);
                    out[1] = static_cast<uint8_t>(((c >> 5) & 31) << 3);
                }
                out[2] = static_cast<uint8_t>((c & 31) << 3);
            }
        }
    }
    if (max_presents && presents_ >= max_presents) stop_ = true;
}

// Position de la souris dans l'image du jeu, à partir de sa position dans la fenêtre.
void Desktop::to_game(int window_x, int window_y) {
    const int x = static_cast<int>((window_x - view_x_) / view_scale_), y = static_cast<int>((window_y - view_y_) / view_scale_);
    mouse_x = std::clamp(x, 0, std::max(texture_w_ - 1, 0));
    mouse_y = std::clamp(y, 0, std::max(texture_h_ - 1, 0));
}

void Desktop::toggle_fullscreen() {
    if (!window_ || headless) return;
    fullscreen = !fullscreen;
    SDL_SetWindowFullscreen(window_, fullscreen ? static_cast<Uint32>(SDL_WINDOW_FULLSCREEN_DESKTOP) : 0u);
    if (!fullscreen) {
        SDL_SetWindowSize(window_, width_, height_);
        SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
}

void Desktop::add_script(const std::string& text) {
    using Kind = ScriptEvent::Kind;
    std::stringstream all(text);
    std::string item;
    while (std::getline(all, item, ';')) {
        const size_t colon = item.find(':');
        if (colon == std::string::npos) {
            if (item.find_first_not_of(" \t") == std::string::npos) continue;
            throw std::runtime_error("action sans instant : " + item);
        }
        ScriptEvent e;
        e.at = std::stod(item.substr(0, colon));
        std::stringstream rest(item.substr(colon + 1));
        std::string verb;
        rest >> verb;
        auto point = [&] {
            char comma = 0;
            if (!(rest >> e.x >> comma >> e.y)) throw std::runtime_error("position attendue : " + item);
        };
        auto push = [&](Kind kind, double delay) {
            ScriptEvent copy = e;
            copy.kind = kind;
            copy.at += delay;
            script_.push_back(copy);
        };
        if (verb == "move") { point(); push(Kind::Move, 0); }
        else if (verb == "click") { point(); push(Kind::Move, 0); push(Kind::LeftDown, 0.15); push(Kind::LeftUp, 0.3); }
        else if (verb == "rclick") { point(); push(Kind::Move, 0); push(Kind::RightDown, 0.15); push(Kind::RightUp, 0.3); }
        // « down » et « up » séparés : un glissement s'écrit down, move…, up (tracé d'une route).
        // Le jeu lit la case survolée une fois par image : on lui laisse une demi-seconde entre
        // l'arrivée de la souris et le bouton, sinon il part de la case d'avant.
        else if (verb == "down") { point(); push(Kind::Move, 0); push(Kind::LeftDown, 0.5); }
        else if (verb == "up") { point(); push(Kind::Move, 0); push(Kind::LeftUp, 0.5); }
        else if (verb == "key") { rest >> e.key; push(Kind::KeyDown, 0); push(Kind::KeyUp, 0.15); }
        else if (verb == "char") { rest >> e.key; push(Kind::Char, 0); }
        else if (verb == "shot") { rest >> e.path; push(Kind::Shot, 0); }
        else throw std::runtime_error("action inconnue : " + item);
    }
    std::stable_sort(script_.begin(), script_.end(), [](const ScriptEvent& a, const ScriptEvent& b) { return a.at < b.at; });
    screenshot_wanted_ = !script_.empty();
}

void Desktop::alert(const std::string& title, const std::string& text) {
    if (headless) return;
    if (fullscreen) toggle_fullscreen();
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), text.c_str(), window_);
}

void Desktop::save_screenshot() { save_screenshot_to(screenshot_path); }

void Desktop::save_screenshot_to(const std::string& path) {
    const std::string& screenshot_path = path;
    if (screenshot_path.empty() || last_frame_.empty()) return;
    SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormatFrom(last_frame_.data(), texture_w_, texture_h_, 24,
                                                           texture_w_ * 3, SDL_PIXELFORMAT_RGB24);
    if (shot) {
        SDL_SaveBMP(shot, screenshot_path.c_str());
        SDL_FreeSurface(shot);
    }
}

bool Desktop::key_down(int virtual_key) const {
    if (virtual_key == 1) return mouse_buttons_ & MK_LBUTTON;
    if (virtual_key == 2) return mouse_buttons_ & MK_RBUTTON;
    return virtual_key >= 0 && virtual_key < 256 && keys_[static_cast<size_t>(virtual_key)];
}

void Desktop::wait(int milliseconds) { SDL_Delay(static_cast<Uint32>(milliseconds)); }

// Secondes écoulées depuis le lancement, sur l'horloge réelle ou sur celle du programme.
double Desktop::elapsed(uint32_t now) {
    if (!virtual_time) return (SDL_GetTicks64() - start_ticks_) / 1000.0;
    if (!has_first_now_) {
        first_now_ = now;
        has_first_now_ = true;
    }
    return (now - first_now_) / 1000.0;
}

bool Desktop::pump(uint32_t now) {
    if (max_seconds > 0 && elapsed(now) >= max_seconds) stop_ = true;
    auto post = [&](uint32_t message, uint32_t wparam, uint32_t lparam) {
        queue.push_back({hwnd, message, wparam, lparam, now, mouse_x, mouse_y});
    };
    auto mouse = [&](uint32_t message) {
        // Comme Windows, on ne garde en file que le dernier déplacement de souris : sinon les
        // déplacements s'accumulent plus vite que le jeu ne les lit et retardent les clics.
        if (message == WM_MOUSEMOVE && !queue.empty() && queue.back().message == WM_MOUSEMOVE) queue.pop_back();
        post(message, mouse_buttons_, static_cast<uint32_t>(mouse_y) << 16 | (static_cast<uint32_t>(mouse_x) & 0xFFFF));
    };
    // Scénario de diagnostic : mêmes messages que ceux d'un joueur.
    const double since_start = elapsed(now);
    for (; script_next_ < script_.size() && script_[script_next_].at <= since_start; ++script_next_) {
        using Kind = ScriptEvent::Kind;
        const ScriptEvent& s = script_[script_next_];
        const uint32_t vk = static_cast<uint32_t>(s.key);
        switch (s.kind) {
            case Kind::Move: mouse_x = s.x; mouse_y = s.y; mouse(WM_MOUSEMOVE); break;
            case Kind::LeftDown: mouse_buttons_ |= MK_LBUTTON; mouse(WM_LBUTTONDOWN); break;
            case Kind::LeftUp: mouse_buttons_ &= ~MK_LBUTTON; mouse(WM_LBUTTONUP); break;
            case Kind::RightDown: mouse_buttons_ |= MK_RBUTTON; mouse(WM_RBUTTONDOWN); break;
            case Kind::RightUp: mouse_buttons_ &= ~MK_RBUTTON; mouse(WM_RBUTTONUP); break;
            case Kind::KeyDown: if (vk < 256) keys_[vk] = true; post(WM_KEYDOWN, vk, 1); break;
            case Kind::KeyUp: if (vk < 256) keys_[vk] = false; post(WM_KEYUP, vk, 0xC0000001u); break;
            case Kind::Char: post(WM_CHAR, vk, 1); break;
            case Kind::Shot: save_screenshot_to(s.path); break;
        }
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                post(WM_CLOSE, 0, 0);
                break;
            case SDL_MOUSEMOTION:
                to_game(e.motion.x, e.motion.y);
                mouse(WM_MOUSEMOVE);
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                const bool down = e.type == SDL_MOUSEBUTTONDOWN;
                to_game(e.button.x, e.button.y);
                uint32_t mask = 0, message = 0;
                if (e.button.button == SDL_BUTTON_LEFT) {
                    mask = MK_LBUTTON;
                    message = down ? WM_LBUTTONDOWN : WM_LBUTTONUP;
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    mask = MK_RBUTTON;
                    message = down ? WM_RBUTTONDOWN : WM_RBUTTONUP;
                } else if (e.button.button == SDL_BUTTON_MIDDLE) {
                    mask = MK_MBUTTON;
                    message = down ? WM_MBUTTONDOWN : WM_MBUTTONUP;
                }
                if (!message) break;
                if (down) mouse_buttons_ |= mask;
                else mouse_buttons_ &= ~mask;
                mouse(message);
                break;
            }
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                // Les raccourcis ⌘ sont ceux de l'hôte : le jeu ne les voit pas.
                if (e.key.keysym.mod & KMOD_GUI) {
                    const SDL_Keycode key = e.key.keysym.sym;
                    if (e.type == SDL_KEYDOWN && !e.key.repeat && (key == SDLK_f || key == SDLK_RETURN)) toggle_fullscreen();
                    break;
                }
                const int vk = virtual_key(e.key.keysym.sym);
                if (!vk) break;
                const bool down = e.type == SDL_KEYDOWN;
                keys_[static_cast<size_t>(vk)] = down;
                post(down ? WM_KEYDOWN : WM_KEYUP, static_cast<uint32_t>(vk), down ? 1u : 0xC0000001u);
                // Touches de contrôle : Windows envoie aussi un WM_CHAR.
                if (down && (vk == 0x0D || vk == 0x08 || vk == 0x1B || vk == 0x09)) post(WM_CHAR, static_cast<uint32_t>(vk), 1);
                break;
            }
            case SDL_TEXTINPUT:
                if (SDL_GetModState() & KMOD_GUI) break;
                // Le jeu attend du Windows-1252 : « é » arrive comme un seul octet. Un caractère
                // qui n'y existe pas est ignoré.
                for (const char* c = e.text.text; *c;) {
                    const unsigned char lead = static_cast<unsigned char>(*c);
                    const size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
                    const std::string one = from_utf8(std::string(c, strnlen(c, length)));
                    if (!one.empty() && (one[0] != '?' || lead == '?')) post(WM_CHAR, static_cast<uint8_t>(one[0]), 1);
                    c += strnlen(c, length);
                }
                break;
            default:
                break;
        }
    }
    return !stop_;
}

}  // namespace zeus::win32
