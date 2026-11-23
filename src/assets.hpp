// The ROM's data (everything but the translated code) as files in a data
// directory: extracted once from the dump the player gives, loaded into the
// ROM image (md::rom) at startup.
#pragma once

#include <string>

namespace assets {

// Writes the data ranges of the dump at rom_path into dir (checks it is the
// right dump).
bool extract(const std::string& rom_path, const std::string& dir, std::string* err);
// Loads the files from dir into md::rom.
bool load(const std::string& dir, std::string* err);

} // namespace assets
