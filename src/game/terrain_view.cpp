#include "game/terrain_view.h"

#include <algorithm>
#include <stdexcept>

namespace zeus::game {
namespace {

constexpr uint8_t kDrawTile = 0x40;

uint32_t u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
           static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

}  // namespace

uint32_t convert_image_id(uint32_t image_id, uint32_t map_version) {
    if (map_version >= kFirstPoseidonMapVersion || image_id / kImagesPerCollection < 2)
        return image_id;
    return image_id + 2 * kImagesPerCollection;
}

TerrainView build_terrain_view(const formats::MapFile& map, ImageStore& images) {
    const uint32_t cells = static_cast<uint32_t>(kGridSide) * kGridSide;
    const formats::MapChunk& image_grid = map.chunk(kImageGridChunk, 4, cells);
    const formats::MapChunk& edge_grid = map.chunk(kEdgeGridChunk, 1, cells);
    const formats::MapChunk& elevation_grid = map.chunk(kElevationGridChunk, 1, cells);

    TerrainView view;
    bool first = true;
    // Parcours par diagonales : les cases du fond (x + y petit) sont dessinées d'abord.
    for (int sum = 0; sum <= 2 * (kGridSide - 1); ++sum) {
        for (int x = std::max(0, sum - (kGridSide - 1)); x <= std::min(sum, kGridSide - 1); ++x) {
            const int y = sum - x;
            const size_t cell = static_cast<size_t>(y) * kGridSide + x;
            const uint32_t id = convert_image_id(u32(&image_grid.data[cell * 4]), map.version());
            if (id == 0 || !(edge_grid.data[cell] & kDrawTile)) continue;
            int width = 0, height = 0;
            try {
                const formats::SgImage& info = images.info(id);
                // Une image miroir prend les dimensions de l'image qu'elle retourne.
                const formats::SgImage& src =
                    info.invert_offset
                        ? images.info(static_cast<uint32_t>(static_cast<int64_t>(id) + info.invert_offset))
                        : info;
                width = src.width;
                height = src.height;
            } catch (const std::exception&) {
                ++view.skipped;
                continue;
            }
            if (width <= 0 || height <= 0) continue;
            // L'image d'un élément de n cases de côté fait 60n - 2 pixels de large ; son socle
            // occupe les 30n lignes du bas et la case de dessin en est le coin gauche.
            const int size = (width + 2) / (kTileWidth + 2);
            TerrainSprite s;
            s.image_id = id;
            s.tile_x = x;
            s.tile_y = y;
            s.width = width;
            s.has_top = images.has_top(id);
            s.height = height;
            s.x = tile_screen_x(x, y);
            s.y = tile_screen_y(x, y) + kTileStepY * (size + 1) - height -
                  kElevationStep * elevation_grid.data[cell];
            view.sprites.push_back(s);
            if (first) {
                view.min_x = s.x;
                view.min_y = s.y;
                view.max_x = s.x + width;
                view.max_y = s.y + height;
                first = false;
            } else {
                view.min_x = std::min(view.min_x, s.x);
                view.min_y = std::min(view.min_y, s.y);
                view.max_x = std::max(view.max_x, s.x + width);
                view.max_y = std::max(view.max_y, s.y + height);
            }
        }
    }
    return view;
}

}  // namespace zeus::game
