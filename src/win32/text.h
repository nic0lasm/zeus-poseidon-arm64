// Conversion entre le codage de texte du jeu (Windows-1252) et celui de l'hôte (UTF-8).
#pragma once

#include <string>

namespace zeus::win32 {

std::string to_utf8(const std::string& windows_text);
// Les caractères absents de Windows-1252 deviennent « ? ».
std::string from_utf8(const std::string& utf8_text);

}  // namespace zeus::win32
