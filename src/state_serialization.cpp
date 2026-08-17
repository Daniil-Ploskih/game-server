#include "state_serialization.h"
#include <fstream>
#include <filesystem>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

namespace state_serialization {

void SaveState(const std::filesystem::path& file_path, const model::Game& game) {
    serialization::GameStateRepr repr(game);
    
    std::filesystem::path temp_path = file_path.string() + ".tmp";
    std::ofstream ofs(temp_path);
    if (!ofs.is_open()) {
        throw std::runtime_error("Cannot open file for writing: " + temp_path.string());
    }
    
    boost::archive::text_oarchive oa(ofs);
    oa << repr;
    ofs.close();
    
    std::filesystem::rename(temp_path, file_path);
}

void LoadState(const std::filesystem::path& file_path, model::Game& game) {
    std::ifstream ifs(file_path);
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open file for reading: " + file_path.string());
    }
    
    boost::archive::text_iarchive ia(ifs);
    serialization::GameStateRepr repr;
    ia >> repr;
    
    repr.Restore(game);
}

} // namespace state_serialization