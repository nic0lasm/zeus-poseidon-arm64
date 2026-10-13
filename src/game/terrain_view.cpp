#include "game/terrain_view.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace zeus::game {
namespace {

using Part = formats::SgFile::Part;

constexpr uint8_t kDrawTile = 0x40;
constexpr uint32_t kTerrainElevation = 0x200;
constexpr uint32_t kTerrainRedrawPending = 0x40000000;

// Images dont le socle n'est jamais dessiné : première image de 40 groupes de falaises de
// Zeus_Terrain (table 0x00692F38 de Zeus.exe 2.1, test 0x00476B90). Numéros dans la collection.
constexpr std::array<uint16_t, 40> kBackfacingCliffs = {
    1159, 1160, 1166, 1259, 1261, 1168, 1169, 1170, 1171, 1172, 1177, 1178, 1179, 1180,
    1186, 1271, 1273, 1189, 1191, 1195, 1199, 1275, 1281, 1194, 1198, 1202, 1243, 1249,
    1251, 1257, 1235, 1241, 1227, 1233, 1329, 1330, 1305, 1319, 1320, 1321};
constexpr uint32_t kTerrainCollection = 5;

uint32_t u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
           static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

struct Cell {
    bool in_map = false;
    uint32_t image = 0, terrain = 0;
    uint8_t edge = 0, bits = 0, elevation = 0;
    bool has_top_flag = false;    // octet 53 de l'enregistrement d'image
    bool has_top_pixels = false;  // l'image a réellement une partie haute à dessiner
    bool backfacing = false;
    int width = 0, height = 0;
};

}  // namespace

uint32_t convert_image_id(uint32_t image_id, uint32_t map_version) {
    if (map_version >= kFirstPoseidonMapVersion || image_id / kImagesPerCollection < 2)
        return image_id;
    return image_id + 2 * kImagesPerCollection;
}

TerrainView build_terrain_view(const formats::MapFile& map, ImageStore& images) {
    const uint32_t count = static_cast<uint32_t>(kGridSide) * kGridSide;
    const formats::MapChunk& image_grid = map.chunk(kImageGridChunk, 4, count);
    const formats::MapChunk& edge_grid = map.chunk(kEdgeGridChunk, 1, count);
    const formats::MapChunk& terrain_grid = map.chunk(kTerrainGridChunk, 4, count);
    const formats::MapChunk& bitfield_grid = map.chunk(kBitfieldGridChunk, 1, count);
    const formats::MapChunk& elevation_grid = map.chunk(kElevationGridChunk, 1, count);

    TerrainView view;
    std::vector<Cell> cells(count);
    for (uint32_t i = 0; i < count; ++i) {
        Cell& c = cells[i];
        c.image = convert_image_id(u32(&image_grid.data[i * 4]), map.version());
        if (c.image == 0) continue;  // hors de la zone jouable
        c.in_map = true;
        c.terrain = u32(&terrain_grid.data[i * 4]);
        c.edge = edge_grid.data[i];
        c.bits = bitfield_grid.data[i];
        c.elevation = elevation_grid.data[i];
        try {
            const formats::SgImage& info = images.info(c.image);
            const formats::SgImage& src =
                info.invert_offset
                    ? images.info(static_cast<uint32_t>(static_cast<int64_t>(c.image) + info.invert_offset))
                    : info;
            c.width = src.width;
            c.height = src.height;
            c.has_top_flag = src.has_compressed_part != 0;
            c.has_top_pixels = images.has_top(c.image);
        } catch (const std::exception&) {
            ++view.skipped;
            c.width = c.height = 0;
        }
        if (c.image / kImagesPerCollection == kTerrainCollection) {
            const uint32_t number = c.image % kImagesPerCollection;
            c.backfacing = std::find(kBackfacingCliffs.begin(), kBackfacingCliffs.end(), number) !=
                           kBackfacingCliffs.end();
        }
    }

    auto at = [&](int x, int y) -> const Cell* {
        if (x < 0 || y < 0 || x >= kGridSide || y >= kGridSide) return nullptr;
        const Cell& c = cells[static_cast<size_t>(y) * kGridSide + x];
        return c.in_map ? &c : nullptr;
    };

    // 0x0047A600 : compare l'altitude à celle de la case située juste derrière à l'écran.
    auto behind_compare = [&](const Cell& c, int x, int y) -> int {
        const Cell* behind = at(x - 1, y - 1);
        if (!behind) return 0;
        if (c.elevation < behind->elevation) return 1;
        if (behind->elevation < c.elevation) return -1;
        if ((c.terrain & kTerrainElevation) && (behind->terrain & 0x800200)) return 1;
        return 0;
    };
    // 0x0047A7C0 : terrain plat (arbre, pré...) sur une seule case.
    auto is_flat_terrain = [](const Cell& c) {
        const uint32_t flat = c.terrain & 0x881;
        return (c.bits & 0x0F) == 0 && flat != 0 && (c.terrain & 0x1000CA89) == flat;
    };
    // 0x0047A6C0 : le socle sera caché par une case plus haute située devant.
    auto hidden_by_front = [&](const Cell& c, int x, int y) {
        if ((c.bits & 0x0F) != 0) return false;
        const Cell* front = at(x + 1, y + 1);
        if (!front) return false;
        if (c.elevation < front->elevation) return true;
        const Cell* front2 = at(x + 2, y + 2);
        return front2 && c.elevation + 1 < front2->elevation;
    };

    bool first = true;
    auto emit = [&](const Cell& c, int x, int y, Part part) {
        if (!(c.edge & kDrawTile) || c.width <= 0 || c.height <= 0) return;
        if (part == Part::Top && !c.has_top_pixels) return;
        // L'image d'un élément de n cases de côté fait 60n - 2 pixels de large ; son socle
        // occupe les 30n lignes du bas et la case de dessin en est le coin gauche (0x00476240).
        const int size = (c.width + 2) / (kTileWidth + 2);
        DrawCommand d;
        d.image_id = c.image;
        d.part = part;
        d.tile_x = x;
        d.tile_y = y;
        d.width = c.width;
        d.height = c.height;
        d.x = tile_screen_x(x, y);
        d.y = tile_screen_y(x, y) + kTileStepY * (size + 1) - c.height - kElevationStep * c.elevation;
        view.commands.push_back(d);
        if (first) {
            view.min_x = d.x;
            view.min_y = d.y;
            view.max_x = d.x + d.width;
            view.max_y = d.y + d.height;
            first = false;
        } else {
            view.min_x = std::min(view.min_x, d.x);
            view.min_y = std::min(view.min_y, d.y);
            view.max_x = std::max(view.max_x, d.x + d.width);
            view.max_y = std::max(view.max_y, d.y + d.height);
        }
    };

    // Parcours par rangées d'écran (x + y constant), de gauche à droite, du fond vers l'avant.
    auto for_each_cell = [&](const auto& visit) {
        for (int sum = 0; sum <= 2 * (kGridSide - 1); ++sum)
            for (int x = std::max(0, sum - (kGridSide - 1)); x <= std::min(sum, kGridSide - 1); ++x)
                if (const Cell* c = at(x, sum - x)) visit(*c, x, sum - x);
    };

    // Première passe : socles des cases sans partie haute, des terrains plats et des cases
    // dominées par celle de derrière, sauf si la case de derrière est plus basse.
    for_each_cell([&](const Cell& c, int x, int y) {
        const int r = behind_compare(c, x, y);
        if ((!c.has_top_flag || is_flat_terrain(c) || r == 1) && r != -1 && !c.backfacing)
            emit(c, x, y, Part::Footprint);
    });

    // Seconde passe : socles différés, puis partie haute de chaque case.
    for_each_cell([&](const Cell& c, int x, int y) {
        const int r = behind_compare(c, x, y);
        if ((!c.has_top_flag || is_flat_terrain(c) || r == 1) && r != -1) {
            // Une case surélevée est redessinée si une case de derrière, assez basse, a pu
            // déborder sur elle avec sa partie haute.
            bool redraw = (c.terrain & kTerrainRedrawPending) != 0;
            if (!redraw && c.elevation != 0) {
                for (int k = 1; k <= c.elevation; ++k) {
                    const Cell* behind = at(x - k, y - k);
                    if (!behind) break;
                    if (behind->elevation <= c.elevation - k) {
                        redraw = true;
                        break;
                    }
                }
            }
            if (redraw) emit(c, x, y, Part::Footprint);
        } else if (!hidden_by_front(c, x, y) && !c.backfacing) {
            emit(c, x, y, Part::Footprint);
        }
        emit(c, x, y, Part::Top);
    });
    return view;
}

}  // namespace zeus::game
