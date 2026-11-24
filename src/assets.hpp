// The ROM's data (everything but the translated code) as files in a data
// directory: extracted once from the dump the player gives, loaded into the
// ROM image (md::rom) at startup.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace assets {

// Writes the data of the dump at rom_path into dir (checks it is the right
// dump).
bool extract(const std::string& rom_path, const std::string& dir, std::string* err);
// Loads the files from dir into md::rom.
bool load(const std::string& dir, std::string* err);
// The unpacked data of the LCW packed data at ROM address src (the ROM image
// does not hold the packed bytes), or nullptr.
const std::vector<uint8_t>* unpacked(uint32_t src);

} // namespace assets
