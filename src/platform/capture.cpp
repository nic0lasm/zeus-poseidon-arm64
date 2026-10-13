#include "platform/capture.h"

#include <stdexcept>
#include <string>

namespace zeus::platform {

void save_render_bmp(SDL_Renderer* renderer, const std::filesystem::path& out,
                     const std::function<void()>& paint) {
    // Le contenu de la fenêtre n'est plus lisible après présentation : on dessine dans une texture.
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    SDL_Texture* target =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(0, w, h, 24, SDL_PIXELFORMAT_RGB24);
    bool ok = target && shot && SDL_SetRenderTarget(renderer, target) == 0;
    if (ok) {
        paint();
        ok = SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGB24, shot->pixels,
                                  shot->pitch) == 0 &&
             SDL_SaveBMP(shot, out.string().c_str()) == 0;
    }
    const std::string error = ok ? "" : SDL_GetError();
    SDL_SetRenderTarget(renderer, nullptr);
    if (shot) SDL_FreeSurface(shot);
    if (target) SDL_DestroyTexture(target);
    if (!ok) throw std::runtime_error(error);
}

}  // namespace zeus::platform
