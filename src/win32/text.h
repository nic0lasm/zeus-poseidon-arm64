// Conversion entre le codage de texte du jeu (Windows-1252) et celui de l'hôte (UTF-8).
#pragma once

#include <string>

namespace zeus::win32 {

std::string to_utf8(const std::string& windows_text);
// Les caractères absents de Windows-1252 deviennent « ? ».
std::string from_utf8(const std::string& utf8_text);

// Code page of the game's bytes: 1252 (the default: the GOG edition and Western European
// languages) or 1251 (Cyrillic), chosen by a language pack (codage.txt). Returns false for an
// unknown code page, which leaves the current one in place. The comments above say Windows-1252:
// they hold for the default.
bool set_code_page(int code_page);
int code_page();

}  // namespace zeus::win32
