#include "model.h"

#include <algorithm>
#include <stdexcept>
#include "extra_data.h"

namespace {
constexpr int64_t MillisecondsPerSecond = 1000;
constexpr double BaseCollectRadius = 0.55;

class SimpleCollisionProvider : public collision_detector::ItemGathererProvider {
public:
    void SetData(std::vector<collision_detector::Item> items,
                 std::vector<collision_detector::Gatherer> gatherers) {
        items_ = std::move(items);
        gatherers_ = std::move(gatherers);
    }

    size_t ItemsCount() const override { return items_.size(); }
    collision_detector::Item GetItem(size_t idx) const override { 
        return items_.at(idx);
    }
    size_t GatherersCount() const override { 
        return gatherers_.size(); 
    }
    collision_detector::Gatherer GetGatherer(size_t idx) const override { 
        return gatherers_.at(idx); 
    }

private:
    std::vector<collision_detector::Item> items_;
    std::vector<collision_detector::Gatherer> gatherers_;
};

} // namespace

namespace model {
using namespace std::literals;

void Map::AddOffice(Office office) {
    if (warehouse_id_to_index_.contains(office.GetId())) {
        throw std::invalid_argument("Duplicate warehouse");
    }

    const size_t index = offices_.size();
    Office& o = offices_.emplace_back(std::move(office));
    try {
        warehouse_id_to_index_.emplace(o.GetId(), index);
    } 
    catch (...) {
        offices_.pop_back();
        throw;
    }
}

void Game::AddMap(Map map) {
    const size_t index = maps_.size();
    if (auto [it, inserted] = map_id_to_index_.emplace(map.GetId(), index); !inserted) {
        throw std::invalid_argument("Map with id "s + *map.GetId() + " already exists"s);
    } 
    else {
        try {
            maps_.emplace_back(std::move(map));
            maps_.back().BuildSpatialIndex();
        } 
        catch (...) {
            map_id_to_index_.erase(it);
            throw;
        }
    }
}

void PlayerTokens::RemovePlayer(Player::Id id) {
    for (auto it = tokens_to_players_.begin(); it != tokens_to_players_.end();) {
        if (it->second->GetId() == id) {
            tokens_to_players_.erase(it);
            return;
        }
        ++it;
    }
}

double Game::FindBoundaryX(Point2d pos, double speed_x, const Map& map) {
    for (const auto& [y_coord, roads_at_y] : map.GetHorizontalIndex()) {
        if (std::abs(pos.y - y_coord) <= max_road_deviation) {
            for (const Road* road : roads_at_y) {
                Coord x_min = std::min(road->GetStart().x, road->GetEnd().x);
                Coord x_max = std::max(road->GetStart().x, road->GetEnd().x);
                if (pos.x >= x_min - max_road_deviation && pos.x <= x_max + max_road_deviation) {
                    if (speed_x > 0) {
                        return static_cast<double>(x_max) + max_road_deviation;
                    }
                    return static_cast<double>(x_min) - max_road_deviation;
                }
            }
        }
    }
    
    for (const auto& [x_coord, roads_at_x] : map.GetVerticalIndex()) {
        if (std::abs(pos.x - x_coord) <= max_road_deviation) {
            for (const Road* road : roads_at_x) {
                Coord y_min = std::min(road->GetStart().y, road->GetEnd().y);
                Coord y_max = std::max(road->GetStart().y, road->GetEnd().y);
                if (pos.y >= y_min - max_road_deviation && pos.y <= y_max + max_road_deviation) {
                    if (speed_x > 0) {
                        return static_cast<double>(road->GetStart().x) + max_road_deviation;
                    }
                    return static_cast<double>(road->GetStart().x) - max_road_deviation;
                }
            }
        }
    }
    
    return pos.x;
}

double Game::FindBoundaryY(Point2d pos, double speed_y, const Map& map) {
    for (const auto& [x_coord, roads_at_x] : map.GetVerticalIndex()) {
        if (std::abs(pos.x - x_coord) <= max_road_deviation) {
            for (const Road* road : roads_at_x) {
                Coord y_min = std::min(road->GetStart().y, road->GetEnd().y);
                Coord y_max = std::max(road->GetStart().y, road->GetEnd().y);
                if (pos.y >= y_min - max_road_deviation && pos.y <= y_max + max_road_deviation) {
                    if (speed_y > 0) {
                        return static_cast<double>(y_max) + max_road_deviation;
                    }
                    return static_cast<double>(y_min) - max_road_deviation;
                }
            }
        }
    }

    for (const auto& [y_coord, roads_at_y] : map.GetHorizontalIndex()) {
        if (std::abs(pos.y - y_coord) <= max_road_deviation) {
            for (const Road* road : roads_at_y) {
                Coord x_min = std::min(road->GetStart().x, road->GetEnd().x);
                Coord x_max = std::max(road->GetStart().x, road->GetEnd().x);
                if (pos.x >= x_min - max_road_deviation && pos.x <= x_max + max_road_deviation) {
                    if (speed_y > 0) {
                        return static_cast<double>(road->GetStart().y) + max_road_deviation;
                    }
                    return static_cast<double>(road->GetStart().y) - max_road_deviation;
                }
            }
        }
    }
    
    return pos.y;
}

void Game::HandleCollision(Player& player, Point2d old_pos, Point2d new_pos, const Map& map) {
    Point2d speed = player.GetSpeed();
    
    if (speed.x != 0.0) {
        Point2d test_pos = {new_pos.x, old_pos.y};
        const Road* road = map.FindRoad(test_pos);

        if (road == nullptr) {
            double boundary_x = FindBoundaryX(old_pos, speed.x, map);
            player.SetPosition({boundary_x, old_pos.y});
            player.SetSpeed({0.0, 0.0});
            player.MarkAsStopped(current_game_time_);
            return; 
        }
    }

    if (speed.y != 0.0) {
        Point2d current_pos = player.GetPosition();
        Point2d test_pos = {current_pos.x, new_pos.y};
        const Road* road = map.FindRoad(test_pos);
        
        if (road == nullptr) {
            double boundary_y = FindBoundaryY(current_pos, speed.y, map);
            player.SetPosition({current_pos.x, boundary_y});
            player.SetSpeed({0.0, 0.0});
            player.MarkAsStopped(current_game_time_);
        }
    }
}

void Game::MovePlayer(Player& player, double time_delta_sec){
    Point2d speed = player.GetSpeed();
    if(speed.x == 0.0 && speed.y == 0.0){
        return;
    }
    const Map* map = FindMap(player.GetMapId());
    if(!map){
        return;
    }
    Point2d old_pos = player.GetPosition();
    Point2d new_pos;
    new_pos.x = old_pos.x + speed.x * time_delta_sec;
    new_pos.y = old_pos.y + speed.y * time_delta_sec;

    const Road* road = map->FindRoad(new_pos);
    if(road != nullptr) {
        player.SetPosition(new_pos);
    }
    else {
        HandleCollision(player, old_pos, new_pos, *map);
    }
}

void Game::InitLootGenerators() {
    for (const auto& map : maps_) {
        loot_generators_.emplace(
            map.GetId(),
            loot_gen::LootGenerator{
                loot_generator_config_.period,
                loot_generator_config_.probability
            }
        );
    }
}

std::vector<const LostObject*> Game::GetLostObjectsOnMap(const Map::Id& map_id) const {
    std::vector<const LostObject*> result;
    for (const auto& obj : lost_objects_) {
        if (obj->GetMapId() == map_id) {
            result.push_back(obj.get());
        }
    }
    return result;
}

void Game::GenerateLoot(double time_delta_sec) {
    auto time_delta_ms = std::chrono::milliseconds(static_cast<int64_t>(time_delta_sec * MillisecondsPerSecond));
    
    for (const auto& map : maps_) {
        auto it = loot_generators_.find(map.GetId());
        if (it == loot_generators_.end()) continue;
        
        unsigned looter_count = static_cast<unsigned>(GetPlayersOnMap(map.GetId()).size());
        unsigned loot_count = static_cast<unsigned>(GetLostObjectsOnMap(map.GetId()).size());
        
        unsigned new_loot_count = it->second.Generate(time_delta_ms, loot_count, looter_count);
        
        const auto& roads = map.GetRoads();
        if (roads.empty() || map.GetLootTypeCount() == 0) continue;
        
        for (unsigned i = 0; i < new_loot_count; ++i) {
            std::uniform_int_distribution<unsigned> type_dist(0, map.GetLootTypeCount() - 1);
            unsigned type = type_dist(generator_);
            
            std::uniform_int_distribution<size_t> road_dist(0, roads.size() - 1);
            const Road& road = roads[road_dist(generator_)];
            
            std::uniform_real_distribution<double> pos_dist(0.0, 1.0);
            double t = pos_dist(generator_);
            auto start = road.GetStart();
            auto end = road.GetEnd();
            Point2d position{
                start.x + t * (end.x - start.x),
                start.y + t * (end.y - start.y)
            };
            
            lost_objects_.push_back(std::make_unique<LostObject>(
                next_loot_id_++, type, position, map.GetId()
            ));
        }
    }
}

void Game::ProcessItemCollection(const Map& map, const std::unordered_map<Player::Id, Point2d>& old_positions) {
    const auto& map_id = map.GetId();
    
    std::vector<collision_detector::Item> cd_items;
    std::vector<LostObject*> item_ptrs;
    
    for (auto& obj : lost_objects_) {
        if (obj->GetMapId() != map_id) continue;
        
        collision_detector::Item item;
        item.position = {obj->GetPosition().x, obj->GetPosition().y};
        item.width = 0.0;
        cd_items.push_back(item);
        item_ptrs.push_back(obj.get());
    }
    
    if (cd_items.empty()) return;
    
    std::vector<collision_detector::Gatherer> cd_gatherers;
    std::vector<Player*> player_ptrs;
    
    for (auto& player : players_) {
        if (player->GetMapId() != map_id) continue;
        
        Point2d old_pos = old_positions.at(player->GetId());
        Point2d new_pos = player->GetPosition();
        
        if (old_pos.x == new_pos.x && old_pos.y == new_pos.y) continue;
        
        collision_detector::Gatherer gatherer;
        gatherer.start_pos = {old_pos.x, old_pos.y};
        gatherer.end_pos = {new_pos.x, new_pos.y};
        gatherer.width = 0.6;
        cd_gatherers.push_back(gatherer);
        player_ptrs.push_back(player.get());
    }
    
    if (cd_gatherers.empty()) {
        return;
    }

    SimpleCollisionProvider provider;
    provider.SetData(std::move(cd_items), std::move(cd_gatherers));
    
    auto events = collision_detector::FindGatherEvents(provider);
    
    std::vector<bool> item_collected(item_ptrs.size(), false);
    
    for (const auto& event : events) {
        Player* player = player_ptrs[event.gatherer_id];
        size_t item_idx = event.item_id;
        
        if (item_collected[item_idx]) {
            continue;
        }
        
        LostObject* item = item_ptrs[item_idx];
        
        if (!player->IsBagFull(map.GetBagCapacity())) {
            player->AddToBag(item->GetId(), item->GetType());
            item_collected[item_idx] = true;
        }
    }
    
    lost_objects_.erase(
        std::remove_if(lost_objects_.begin(), lost_objects_.end(),
            [&map_id, &item_collected, &item_ptrs](const std::unique_ptr<LostObject>& obj) {
                if (obj->GetMapId() != map_id) return false;
                
                for (size_t i = 0; i < item_ptrs.size(); ++i) {
                    if (item_ptrs[i] == obj.get() && item_collected[i]) {
                        return true;
                    }
                }
                return false;
            }),
        lost_objects_.end()
    );
    
    for (auto& player : players_) {
        if (player->GetMapId() != map_id) {
            continue;
        }
        if (player->GetBag().empty()) {
            continue;
        }
        
        Point2d pos = player->GetPosition();
        
        for (const auto& office : map.GetOffices()) {
            Point2d office_pos = {
                static_cast<double>(office.GetPosition().x),
                static_cast<double>(office.GetPosition().y)
            };
            
            double dx = pos.x - office_pos.x;
            double dy = pos.y - office_pos.y;
            double dist_sq = dx * dx + dy * dy;
            
            if (dist_sq <= BaseCollectRadius * BaseCollectRadius) {
                if(extra_data_ && extra_data_->HasMap(map_id)){
                    const auto& loot_types = extra_data_->GetLootTypes(map_id);
                    int64_t earned_points = 0;
                    for(const auto& bag_item : player->GetBag()){
                        if(bag_item.type < loot_types.size()){
                            const auto& loot_type_obj = loot_types.at(bag_item.type).as_object();
                            if(loot_type_obj.contains("value")){
                                earned_points += loot_type_obj.at("value").as_int64();
                            }
                        }
                    }
                    player->AddScore(earned_points);
                }
                player->ClearBag();
                break;
            }
        }
    }
}

}  // namespace model