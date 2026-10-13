#include "win32/desktop.h"

#include <SDL.h>

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
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

void Desktop::create_window(const std::string& title, int width, int height) {
    if (window_) return;
    if (width > 0 && height > 0) {
        width_ = width;
        height_ = height;
    }
    const Uint32 flags = headless ? static_cast<Uint32>(SDL_WINDOW_HIDDEN) : 0u;
    window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width_,
                               height_, flags);
    if (!window_) throw std::runtime_error(SDL_GetError());
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) throw std::runtime_error(SDL_GetError());
}

void Desktop::set_mode(int width, int height) {
    width_ = width;
    height_ = height;
    if (window_) {
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
        texture_w_ = width;
        texture_h_ = height;
        texture_565_ = rgb565;
    }
    SDL_UpdateTexture(texture_, nullptr, pixels, pitch);
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
    ++presents_;

    if (!screenshot_path.empty()) {
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

void Desktop::save_screenshot() {
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

bool Desktop::pump(uint32_t now) {
    if (max_seconds > 0 && (SDL_GetTicks64() - start_ticks_) / 1000.0 >= max_seconds) stop_ = true;
    auto post = [&](uint32_t message, uint32_t wparam, uint32_t lparam) {
        queue.push_back({hwnd, message, wparam, lparam, now, mouse_x, mouse_y});
    };
    auto mouse = [&](uint32_t message) {
        post(message, mouse_buttons_, static_cast<uint32_t>(mouse_y) << 16 | (static_cast<uint32_t>(mouse_x) & 0xFFFF));
    };
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                post(WM_CLOSE, 0, 0);
                break;
            case SDL_MOUSEMOTION:
                mouse_x = e.motion.x;
                mouse_y = e.motion.y;
                mouse(WM_MOUSEMOVE);
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                const bool down = e.type == SDL_MOUSEBUTTONDOWN;
                mouse_x = e.button.x;
                mouse_y = e.button.y;
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
                for (const char* c = e.text.text; *c; ++c)
                    if (static_cast<unsigned char>(*c) < 0x80) post(WM_CHAR, static_cast<uint8_t>(*c), 1);
                break;
            default:
                break;
        }
    }
    return !stop_;
}

}  // namespace zeus::win32
