// Capture d'un rendu SDL dans un fichier BMP, sans passer par la fenêtre.
#pragma once

#include <SDL.h>

#include <filesystem>
#include <functional>

namespace zeus::platform {

// Appelle `paint` avec une texture comme cible de rendu, puis enregistre le résultat.
// Lève std::runtime_error en cas d'échec.
void save_render_bmp(SDL_Renderer* renderer, const std::filesystem::path& out,
                     const std::function<void()>& paint);

}  // namespace zeus::platform
