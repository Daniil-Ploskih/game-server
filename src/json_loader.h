#pragma once

#include <filesystem>

#include "model.h"
#include "extra_data.h"

namespace json_loader {

model::Game LoadGame(const std::filesystem::path& json_path, extra_data::MapExtraData& extra_data);

}  // namespace json_loader
