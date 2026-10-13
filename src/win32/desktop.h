// Fenêtre, file de messages et écran du programme émulé, réalisés avec SDL.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace zeus::win32 {

// Message Windows en attente de lecture par le programme.
struct Message {
    uint32_t hwnd = 0, message = 0, wparam = 0, lparam = 0, time = 0;
    int32_t x = 0, y = 0;
};

class Desktop {
public:
    Desktop();
    ~Desktop();
    Desktop(const Desktop&) = delete;
    Desktop& operator=(const Desktop&) = delete;

    // Réglages de diagnostic, à fixer avant le lancement du programme.
    bool headless = false;        // fenêtre cachée
    std::string screenshot_path;  // enregistre la dernière image affichée
    int max_presents = 0;         // arrête le programme après ce nombre d'images (0 = jamais)
    double max_seconds = 0;       // arrête le programme après cette durée (0 = jamais)

    // Taille de l'écran annoncée au programme tant qu'il n'a pas choisi de mode d'affichage.
    static constexpr int kScreenWidth = 1024, kScreenHeight = 768;

    void create_window(const std::string& title, int width, int height);
    // Change le mode d'affichage : la fenêtre du programme, plein écran, en prend la taille.
    void set_mode(int width, int height);
    int width() const { return width_; }
    int height() const { return height_; }

    // Affiche une image 16 bits (5-5-5 ou 5-6-5) de `pitch` octets par ligne.
    void present(const uint8_t* pixels, int width, int height, int pitch, bool rgb565);
    int presents() const { return presents_; }
    void save_screenshot();

    // Convertit les événements SDL en messages Windows. Renvoie faux quand il faut s'arrêter.
    bool pump(uint32_t now);
    void wait(int milliseconds);
    std::deque<Message> queue;
    uint32_t hwnd = 0x00010010;  // la seule fenêtre du programme
    uint32_t wndproc = 0;
    int mouse_x = 0, mouse_y = 0;
    bool key_down(int virtual_key) const;
    bool should_stop() const { return stop_; }

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    bool texture_565_ = false;
    int width_ = kScreenWidth, height_ = kScreenHeight, texture_w_ = 0, texture_h_ = 0;
    int presents_ = 0;
    bool stop_ = false;
    uint32_t mouse_buttons_ = 0;
    std::vector<uint8_t> last_frame_;  // RGB24 de la dernière image, pour la capture
    std::vector<bool> keys_;
    uint64_t start_ticks_ = 0;
};

}  // namespace zeus::win32
