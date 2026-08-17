#include <boost/json.hpp>

#include "json_loader.h"
#include <fstream>
#include <string>
#include <boost/log/trivial.hpp>

namespace {

constexpr int64_t MillisecondsPerSecond = 1000;

// Загрузка дорог
void LoadRoads(const boost::json::array& roads_array, model::Map& map) {
    for (const auto& road_item : roads_array) {
        const auto& road_obj = road_item.as_object();
        
        model::Coord x0 = static_cast<model::Coord>(road_obj.at("x0").as_int64());
        model::Coord y0 = static_cast<model::Coord>(road_obj.at("y0").as_int64());
        
        if (const auto* x1_ptr = road_obj.if_contains("x1")) {
            model::Coord x1 = static_cast<model::Coord>(x1_ptr->as_int64());
            model::Road road{model::Road::HORIZONTAL, model::Point{x0, y0}, x1};
            map.AddRoad(road);
        } else {
            model::Coord y1 = static_cast<model::Coord>(road_obj.at("y1").as_int64());
            model::Road road{model::Road::VERTICAL, model::Point{x0, y0}, y1};
            map.AddRoad(road);
        }
    }
}

// Загрузка зданий
void LoadBuildings(const boost::json::array& buildings_array, model::Map& map) {
    for (const auto& b_item : buildings_array) {
        const auto& b_obj = b_item.as_object();
        
        model::Coord x = static_cast<model::Coord>(b_obj.at("x").as_int64());
        model::Coord y = static_cast<model::Coord>(b_obj.at("y").as_int64());
        model::Dimension w = static_cast<model::Dimension>(b_obj.at("w").as_int64());
        model::Dimension h = static_cast<model::Dimension>(b_obj.at("h").as_int64());
        
        model::Rectangle rect{model::Point{x, y}, model::Size{w, h}};
        model::Building building{rect};
        map.AddBuilding(building);
    }
}

// Загрузка офисов
void LoadOffices(const boost::json::array& offices_array, model::Map& map) {
    for (const auto& o_item : offices_array) {
        const auto& o_obj = o_item.as_object();
        
        std::string office_id_str = std::string(o_obj.at("id").as_string());
        model::Office::Id office_id{office_id_str};
        
        model::Coord x = static_cast<model::Coord>(o_obj.at("x").as_int64());
        model::Coord y = static_cast<model::Coord>(o_obj.at("y").as_int64());
        model::Dimension offsetX = static_cast<model::Dimension>(o_obj.at("offsetX").as_int64());
        model::Dimension offsetY = static_cast<model::Dimension>(o_obj.at("offsetY").as_int64());
        
        model::Office office{office_id, model::Point{x, y}, model::Offset{offsetX, offsetY}};
        map.AddOffice(office); 
    }
}

}  // namespace

namespace json_loader {

model::Game LoadGame(const std::filesystem::path& json_path, extra_data::MapExtraData& extra_data) {
    std::ifstream file(json_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + json_path.string());
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    boost::json::value root;
    try {
        root = boost::json::parse(content);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse JSON file '" + 
                                 json_path.string() + "': " + e.what());
    }
    
    const boost::json::array& maps_array = root.as_object().at("maps").as_array();
    
    model::Game game;
    
    const auto& root_obj = root.as_object();

    size_t default_bag_capacity = 3;
    if (const auto* value = root_obj.if_contains("defaultBagCapacity")) {
        default_bag_capacity = static_cast<size_t>(value->as_int64());
    }

    if (const auto* config_value = root_obj.if_contains("lootGeneratorConfig")) {
        const auto& config = config_value->as_object();
        double period_sec = config.at("period").as_double();
        double probability = config.at("probability").as_double();
        auto period = std::chrono::milliseconds(static_cast<int64_t>(period_sec * MillisecondsPerSecond));
        game.SetLootGeneratorConfig(period, probability);
    }

    if (const auto* value = root_obj.if_contains("dogRetirementTime")) {
        game.SetDogRetirementTime(value->as_double());
    }
    
    for (const auto& item : maps_array) {
        const auto& map_obj = item.as_object();
        
        std::string id_str = std::string(map_obj.at("id").as_string());
        std::string name_str = std::string(map_obj.at("name").as_string());
        size_t bag_capacity = default_bag_capacity;
        if (const auto* value = map_obj.if_contains("bagCapacity")) {
            bag_capacity = static_cast<size_t>(value->as_int64());
        }     
        model::Map::Id map_id{id_str};
        
        std::optional<double> map_dog_speed;
        if (const auto* value = map_obj.if_contains("dogSpeed")) {
            map_dog_speed = value->as_double();
        }
        
        model::Map map{map_id, name_str, map_dog_speed};
        map.SetBagCapacity(bag_capacity);
        
        LoadRoads(map_obj.at("roads").as_array(), map);
        LoadBuildings(map_obj.at("buildings").as_array(), map);
        LoadOffices(map_obj.at("offices").as_array(), map);

        if (const auto* loot_types_value = map_obj.if_contains("lootTypes")) {
            const auto& loot_types_array = loot_types_value->as_array();
            map.SetLootTypeCount(loot_types_array.size());
            extra_data.SetLootTypes(map_id, loot_types_array);
        }
        
        game.AddMap(std::move(map));
    }
    return game;
}

}  // namespace json_loader