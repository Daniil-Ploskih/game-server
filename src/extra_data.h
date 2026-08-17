#pragma once

#include <unordered_map>
#include <boost/json.hpp>
#include "model.h"

namespace extra_data {

class MapExtraData {
public:
    void SetLootTypes(const model::Map::Id& map_id, boost::json::array loot_types) {
        data_[map_id] = std::move(loot_types);
    }

    const boost::json::array& GetLootTypes(const model::Map::Id& map_id) const {
        auto it = data_.find(map_id);
        if (it == data_.end()) {
            throw std::out_of_range("Map not found in extra data");
        }
        return it->second;
    }

    bool HasMap(const model::Map::Id& map_id) const {
        return data_.contains(map_id);
    }

private:
    std::unordered_map<model::Map::Id, boost::json::array, util::TaggedHasher<model::Map::Id>> data_;
};

}  // namespace extra_data