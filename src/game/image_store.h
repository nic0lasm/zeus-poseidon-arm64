// Accès aux images du jeu par identifiant global, comme le fait le moteur d'origine.
//
// Identifiant = collection * 0x4000 + numéro. La collection désigne un fichier .sg3 ; le numéro
// compte à partir de 1 en ignorant les 200 images « système » placées en tête de chaque fichier
// (preuves : fonctions 0x0040DF00, 0x00641DF4 et 0x0065DAFB de Zeus.exe 2.1).
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include "formats/sg3.h"

namespace zeus::game {

constexpr uint32_t kImagesPerCollection = 0x4000;
constexpr uint32_t kSystemImages = 200;

class ImageStore {
public:
    explicit ImageStore(std::filesystem::path data_dir);

    // Nom du fichier de la collection, ou chaîne vide si elle n'est pas attribuée de façon fixe.
    static std::string collection_name(uint32_t collection);

    // Image décodée ; lève std::runtime_error si l'identifiant ne désigne aucune image.
    formats::Pixels decode(uint32_t image_id,
                           formats::SgFile::Part part = formats::SgFile::Part::Whole);
    bool has_top(uint32_t image_id);
    const formats::SgImage& info(uint32_t image_id);

private:
    formats::SgFile& file(uint32_t collection);

    std::filesystem::path data_dir_;
    std::map<uint32_t, std::unique_ptr<formats::SgFile>> files_;
};

}  // namespace zeus::game
