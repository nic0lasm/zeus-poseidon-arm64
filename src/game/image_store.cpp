#include "game/image_store.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace zeus::game {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

ImageStore::ImageStore(std::filesystem::path data_dir) : data_dir_(std::move(data_dir)) {}

// Table construite par la fonction 0x0065DAFB. Les collections absentes de cette liste sont
// attribuées en cours de partie (dieux, monstres, sanctuaires...).
std::string ImageStore::collection_name(uint32_t collection) {
    static const std::map<uint32_t, std::string> names = {
        {0, "Zeus_Fonts"},       {1, "Zeus_Interface"},  {2, "Zeus_Unloaded"},
        {3, "Poseidon_Unloaded"}, {4, "Zeus_General"},    {5, "Zeus_Terrain"},
        {6, "Zeus_Sanctuary3"},  {7, "Zeus_Sanctuary1"}, {8, "Zeus_GOD01"},
        {9, "Zeus_GOD02"},       {10, "Zeus_GOD03"},     {11, "Zeus_GOD04"},
        {12, "Zeus_GOD05"},      {13, "Zeus_GOD06"},     {14, "Zeus_STATCON1"},
        {15, "Zeus_MapOfGreece01"}, {16, "SprAmbient"},  {17, "Destruction"},
        {51, "SprMain"},         {52, "Poseidon_Loaded"}, {53, "PoseidonImps"},
        {65, "Poseidon_GrandStatue1"}, {66, "PoseidonImps2"},
    };
    const auto found = names.find(collection);
    return found == names.end() ? std::string() : found->second;
}

formats::SgFile& ImageStore::file(uint32_t collection) {
    auto found = files_.find(collection);
    if (found != files_.end()) return *found->second;
    const std::string name = collection_name(collection);
    if (name.empty())
        throw std::runtime_error("collection d'images non attribuée : " + std::to_string(collection));
    const std::string wanted = lower(name + ".sg3");
    for (const auto& entry : std::filesystem::directory_iterator(data_dir_)) {
        if (lower(entry.path().filename().string()) == wanted)
            return *files_.emplace(collection, std::make_unique<formats::SgFile>(entry.path()))
                        .first->second;
    }
    throw std::runtime_error("fichier de collection introuvable : " + name + ".sg3");
}

const formats::SgImage& ImageStore::info(uint32_t image_id) {
    const uint32_t collection = image_id / kImagesPerCollection;
    const uint32_t number = image_id % kImagesPerCollection;
    formats::SgFile& sg = file(collection);
    const uint32_t index = number + (collection == 0 ? 0 : kSystemImages);
    if (number == 0 || index > sg.image_count())
        throw std::runtime_error("image hors collection : " + std::to_string(image_id));
    return sg.image(index);
}

formats::Pixels ImageStore::decode(uint32_t image_id, formats::SgFile::Part part) {
    info(image_id);  // valide l'identifiant
    const uint32_t collection = image_id / kImagesPerCollection;
    const uint32_t number = image_id % kImagesPerCollection;
    return file(collection).decode(number + (collection == 0 ? 0 : kSystemImages), part);
}

bool ImageStore::has_top(uint32_t image_id) {
    info(image_id);
    const uint32_t collection = image_id / kImagesPerCollection;
    const uint32_t number = image_id % kImagesPerCollection;
    return file(collection).has_top(number + (collection == 0 ? 0 : kSystemImages));
}

}  // namespace zeus::game
