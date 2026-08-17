#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <random>
#include <sstream>
#include <iomanip>
#include <optional>
#include <map>
#include <cmath>
#include <chrono> 

#include "boost/signals2.hpp"
#include "loot_generator.h"
#include "tagged.h"
#include "collision_detector.h"

namespace extra_data {
    class MapExtraData;
}

namespace model {

using Dimension = int;
using Coord = Dimension;
constexpr double max_road_deviation = 0.4;

struct Point {
    Coord x, y;
};

struct Point2d{
    double x, y;
};

struct Size {
    Dimension width, height;
};

struct Rectangle {
    Point position;
    Size size;
};

struct Offset {
    Dimension dx, dy;
};

enum class Direction{
    UP,
    DOWN,
    LEFT,
    RIGHT
};

class Road {
    struct HorizontalTag {
        explicit HorizontalTag() = default;
    };

    struct VerticalTag {
        explicit VerticalTag() = default;
    };

public:
    constexpr static HorizontalTag HORIZONTAL{};
    constexpr static VerticalTag VERTICAL{};

    Road(HorizontalTag, Point start, Coord end_x) noexcept
        : start_{start}
        , end_{end_x, start.y} {
    }

    Road(VerticalTag, Point start, Coord end_y) noexcept
        : start_{start}
        , end_{start.x, end_y} {
    }

    bool IsHorizontal() const noexcept {
        return start_.y == end_.y;
    }

    bool IsVertical() const noexcept {
        return start_.x == end_.x;
    }

    Point GetStart() const noexcept {
        return start_;
    }

    Point GetEnd() const noexcept {
        return end_;
    }

private:
    Point start_;
    Point end_;
};

class Building {
public:
    explicit Building(Rectangle bounds) noexcept
        : bounds_{bounds} {
    }

    const Rectangle& GetBounds() const noexcept {
        return bounds_;
    }

private:
    Rectangle bounds_;
};

class Office {
public:
    using Id = util::Tagged<std::string, Office>;

    Office(Id id, Point position, Offset offset) noexcept
        : id_{std::move(id)}
        , position_{position}
        , offset_{offset} {
    }

    const Id& GetId() const noexcept {
        return id_;
    }

    Point GetPosition() const noexcept {
        return position_;
    }

    Offset GetOffset() const noexcept {
        return offset_;
    }

private:
    Id id_;
    Point position_;
    Offset offset_;
};

class Map {
public:
    using Id = util::Tagged<std::string, Map>;
    using Roads = std::vector<Road>;
    using Buildings = std::vector<Building>;
    using Offices = std::vector<Office>;

    Map(Id id, std::string name, std::optional<double> dog_speed = std::nullopt) noexcept
        : id_(std::move(id))
        , name_(std::move(name))
        , dog_speed_(dog_speed) {
    }

    const Id& GetId() const noexcept {
        return id_;
    }

    const std::string& GetName() const noexcept {
        return name_;
    }

    const Buildings& GetBuildings() const noexcept {
        return buildings_;
    }

    const Roads& GetRoads() const noexcept {
        return roads_;
    }

    const Offices& GetOffices() const noexcept {
        return offices_;
    }

    void AddRoad(const Road& road) {
        roads_.emplace_back(road);
    }

    void AddBuilding(const Building& building) {
        buildings_.emplace_back(building);
    }

    void AddOffice(Office office);

    double GetDogSpeed(double default_speed)const noexcept{
        return dog_speed_.value_or(default_speed);
    }

    std::optional<double> GetDogSpeed()const noexcept{
        return dog_speed_;
    }

    void BuildSpatialIndex(){
        for (const auto& road : roads_){
            if(road.IsHorizontal()){
                horizontal_index_[road.GetStart().y].push_back(&road);
            }
            else{
                vertical_index_[road.GetStart().x].push_back(&road);
            }
        }
    }

    const Road* FindRoad(Point2d pos) const {
        for (const auto& [y_coord, roads_at_y] : horizontal_index_) {
            if (std::abs(pos.y - y_coord) <= max_road_deviation) {
                for (const Road* road : roads_at_y) {
                    Coord x_min = std::min(road->GetStart().x, road->GetEnd().x);
                    Coord x_max = std::max(road->GetStart().x, road->GetEnd().x);
                    if (pos.x >= x_min - max_road_deviation && pos.x <= x_max + max_road_deviation) {
                        return road;
                    }
                }
            }
        }
    
        for (const auto& [x_coord, roads_at_x] : vertical_index_) {
            if (std::abs(pos.x - x_coord) <= max_road_deviation) {
                for (const Road* road : roads_at_x) {
                    Coord y_min = std::min(road->GetStart().y, road->GetEnd().y);
                    Coord y_max = std::max(road->GetStart().y, road->GetEnd().y);
                    if (pos.y >= y_min - max_road_deviation && pos.y <= y_max + max_road_deviation) {
                        return road;
                    }
                }
            }
        }
        return nullptr;
    }

    const std::map<Coord, std::vector<const Road*>>& GetHorizontalIndex() const { 
        return horizontal_index_; 
    }
    const std::map<Coord, std::vector<const Road*>>& GetVerticalIndex() const { 
        return vertical_index_; 
    }
    
    void SetLootTypeCount(size_t count) noexcept { 
        loot_type_count_ = count; 
    }
    size_t GetLootTypeCount() const noexcept {
        return loot_type_count_; 
    }

    void SetBagCapacity(size_t capacity) noexcept{
        bag_capacity_ = capacity;
    }

    size_t GetBagCapacity() const noexcept{
        return bag_capacity_;
    }

private:
    using OfficeIdToIndex = std::unordered_map<Office::Id, size_t, util::TaggedHasher<Office::Id>>;

    Id id_;
    std::string name_;
    Roads roads_;
    Buildings buildings_;

    OfficeIdToIndex warehouse_id_to_index_;
    Offices offices_;
    std::optional<double> dog_speed_;

    std::map<Coord, std::vector<const Road*>> horizontal_index_;
    std::map<Coord, std::vector<const Road*>> vertical_index_;
    size_t loot_type_count_ = 0;
    size_t bag_capacity_ = 3;
};

class LostObject {
public:
    using Id = int64_t;
    
    LostObject(Id id, unsigned type, Point2d position, Map::Id map_id)
        : id_(id), type_(type), position_(position), map_id_(std::move(map_id)) {}
    
    Id GetId() const noexcept { 
        return id_; 
    }
    unsigned GetType() const noexcept { 
        return type_; 
    }
    Point2d GetPosition() const noexcept { 
        return position_; 
    }
    const Map::Id& GetMapId() const noexcept { 
        return map_id_; 
    }
    
private:
    Id id_;
    unsigned type_;
    Point2d position_;
    Map::Id map_id_;
};

class Player {
public:
    using Id = size_t;

    Player(Id id, std::string name, Map::Id map_id, std::string token)
        : id_(id)
        , name_(std::move(name))
        , map_id_(std::move(map_id))
        , token_(std::move(token))
        , join_time_{0.0}
    {}

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player(Player&&) = default;
    Player& operator=(Player&&) = default;

    Id GetId() const noexcept {
        return id_;
    }

    const std::string& GetName() const noexcept {
        return name_;
    }

    const Map::Id& GetMapId() const noexcept {
        return map_id_;
    }

    const std::string& GetToken() const noexcept {
        return token_;
    }

    Point2d GetPosition() const noexcept {
        return position_;
    }

    Point2d GetSpeed() const noexcept {
        return speed_;
    }

    Direction GetDirection() const noexcept{
        return direction_;
    }

    void SetPosition(Point2d pos) noexcept{
        position_ = pos;
    }

    void SetSpeed(Point2d speed) noexcept{
        speed_ = speed;
    }

    void SetDirection(Direction dir) noexcept{
        direction_ = dir;
    }

    struct BagItem{
        LostObject::Id id;
        unsigned type;
    };

    const std::vector<BagItem>& GetBag() const noexcept{
        return bag_;
    }

    void AddToBag(LostObject::Id id, unsigned type){
        bag_.push_back({id,type});
    }
    
    void ClearBag(){
        bag_.clear();
    }

    bool IsBagFull(size_t capacity) const{
        return bag_.size() >= capacity;
    }

    int64_t GetScore() const noexcept{
        return score_;
    }

    void AddScore(int64_t points) noexcept{
        score_ += points;
    }

    void ResetScore(){
        score_ = 0;
    }

    double GetJoinTime() const noexcept {
        return join_time_;
    }

    std::optional<double> GetLastStopTime() const noexcept {
        return last_stop_time_;
    }
    
    void MarkAsStopped(double game_time) noexcept {
        last_stop_time_ = game_time;
    }

    void MarkAsActive() noexcept {
        last_stop_time_.reset();
    }

    void SetJoinTime(double game_time) noexcept {
        join_time_ = game_time;
    }

private:
    Id id_;
    std::string name_;
    Map::Id map_id_;
    std::string token_;
    Point2d position_{0.0,0.0};
    Point2d speed_ {0.0,0.0};
    Direction direction_{Direction::UP};
    std::vector<BagItem> bag_;
    int64_t score_ = 0;
    
    double join_time_ = 0.0; 
    std::optional<double> last_stop_time_; 
};

struct JoinGameResult {
    enum class Status { Ok, MapNotFound, InvalidName };
    Status status;
    std::string error_message;
    Player::Id player_id = 0;
    std::string token;
};

class PlayerTokens {
public:
    PlayerTokens() : generator1_{GetRandomSeed()}, generator2_{GetRandomSeed()} {}

    std::string GenerateToken() {
        std::uniform_int_distribution<std::mt19937_64::result_type> dist;
        std::stringstream ss;
        ss << std::hex << std::setfill('0') 
           << std::setw(16) << dist(generator1_)
           << std::setw(16) << dist(generator2_);
        return ss.str(); 
    }

    void AddPlayer(Player* player) {
        tokens_to_players_.emplace(player->GetToken(), player);
    }

    Player* FindByToken(const std::string& token) const {
        auto it = tokens_to_players_.find(token);
        return it != tokens_to_players_.end() ? it->second : nullptr;
    }

    void RemovePlayer(Player::Id id);

private:
    static std::mt19937_64::result_type GetRandomSeed() {
        std::random_device rd;
        return rd();
    }

    std::mt19937_64 generator1_;
    std::mt19937_64 generator2_;
    std::unordered_map<std::string, Player*> tokens_to_players_;
};

struct RetiredPlayerRecord {
    std::string name;
    int64_t score;
    double play_time;
};

class Game {
public:

    struct LootGeneratorConfig {
        std::chrono::milliseconds period{5000};
        double probability{0.5};
    };
    
    void SetLootGeneratorConfig(std::chrono::milliseconds period, double probability) {
        loot_generator_config_ = {period, probability};
    }
    
    void InitLootGenerators();
    
    std::vector<const LostObject*> GetLostObjectsOnMap(const Map::Id& map_id) const;
    using Maps = std::vector<Map>;
    Game() : generator_{GetRandomSeed()}, default_dog_speed_{1.0} {}

    void SetDefaultDogSpeed(double speed)noexcept{
        default_dog_speed_ = speed;
    }

    double GetDefaultDogSpeed()const noexcept{
        return default_dog_speed_;
    }

    double GetDogSpeed(const Map& map)const noexcept{
        return map.GetDogSpeed(default_dog_speed_);
    }

    void AddMap(Map map);

    const Maps& GetMaps() const noexcept {
        return maps_;
    }

    const Map* FindMap(const Map::Id& id) const noexcept {
        auto it = map_id_to_index_.find(id);
        return it != map_id_to_index_.end() ? &maps_.at(it->second) : nullptr;
    }

    JoinGameResult JoinGame(const std::string& player_name, const Map::Id& map_id) {
        if (player_name.empty()) {
            return {
                JoinGameResult::Status::InvalidName, "Invalid name"
            };
        }
        if (!FindMap(map_id)) {
            return {
                JoinGameResult::Status::MapNotFound, "Map not found"
            };
        }

        std::string token = player_tokens_.GenerateToken();
        Player::Id new_id = players_.size();
        auto player = std::make_unique<Player>(new_id, player_name, map_id, token);
        
        player->SetJoinTime(current_game_time_); 
        
        Player* ptr = player.get();

        const Map* map = FindMap(map_id);
        const auto& roads = map->GetRoads();
    
        Point2d spawn_pos;
        if (randomize_spawn_points_) {
            std::uniform_int_distribution<size_t> road_dist(0, roads.size() - 1);
            const Road& road = roads[road_dist(generator_)];
            std::uniform_real_distribution<double> position_dist(0.0, 1.0);
            double t = position_dist(generator_);
            auto start = road.GetStart();
            auto end = road.GetEnd();
            spawn_pos = {
                start.x + t * (end.x - start.x),
                start.y + t * (end.y - start.y)
            };
        } 
        else {
            auto start = roads.at(0).GetStart();
            spawn_pos = {static_cast<double>(start.x), static_cast<double>(start.y)};
        }
        
        ptr->SetPosition(spawn_pos);
        players_.push_back(std::move(player));
        player_tokens_.AddPlayer(ptr);

        return {JoinGameResult::Status::Ok, "", ptr->GetId(), ptr->GetToken()};
    }

    std::vector<const Player*> GetPlayersOnMap(const Map::Id& map_id) const {
        std::vector<const Player*> result;
        for (const auto& player : players_) {
            if (player->GetMapId() == map_id) {
                result.push_back(player.get());
            }
        }
        return result;
    }

    const Player* FindPlayer(Player::Id id) const {
        return id < players_.size() ? players_[id].get() : nullptr;
    }
    
    const Player* FindPlayerByToken(const std::string& token) const {
        return player_tokens_.FindByToken(token);
    }

    Player* FindPlayerByToken(const std::string& token) {
        return player_tokens_.FindByToken(token);
    }

    void Tick(double time_delta_sec) {
        current_game_time_ += time_delta_sec;

        GenerateLoot(time_delta_sec);
        std::unordered_map<Player::Id, Point2d> old_position;
        for(const auto& player : players_){
            old_position[player->GetId()] = player->GetPosition();
        }
        for(auto& player : players_){
            MovePlayer(*player, time_delta_sec);
        }
        for(const auto& map : maps_){
            ProcessItemCollection(map, old_position);
        }

        double now = current_game_time_; 
        for (auto it = players_.begin(); it != players_.end(); ) {
            auto& player = **it;
            bool is_stopped = (player.GetSpeed().x == 0.0 && player.GetSpeed().y == 0.0);
        
            if (is_stopped) {
                if (!player.GetLastStopTime().has_value()) {
                    double stop_time = now - time_delta_sec;
                    player.MarkAsStopped(std::max(stop_time, player.GetJoinTime()));
                }
                double idle_seconds = now - player.GetLastStopTime().value();
                constexpr double kIdleTimePrecision = 1e6;  // микросекундная точность
                double rounded_idle = std::round(idle_seconds * kIdleTimePrecision) / kIdleTimePrecision;
                if (rounded_idle >= dog_retirement_time_) {
                    model::RetiredPlayerRecord record{player.GetName(), player.GetScore(), now - player.GetJoinTime()};
                    on_player_retired_(record);
                    player_tokens_.RemovePlayer(player.GetId());
                    it = players_.erase(it);
                    continue;
                }
            } 
            else {
                player.MarkAsActive();
            }
            ++it;
        }          
    }

    void SetRandomizeSpawnPoints(bool value) noexcept {
        randomize_spawn_points_ = value;
    }

    void SetExtraData(const extra_data::MapExtraData* extra_data){
        extra_data_ = extra_data;
    }

    const std::vector<std::unique_ptr<Player>>& GetPlayers() const noexcept {
        return players_;
    }
    
    LostObject::Id GetNextLootId() const noexcept {
        return next_loot_id_;
    }
    
    const std::vector<std::unique_ptr<LostObject>>& GetLostObjects() const noexcept {
        return lost_objects_;
    }

    void ClearState() {
        players_.clear();
        lost_objects_.clear();
        player_tokens_ = PlayerTokens();
        next_loot_id_ = 0;
    }

    void AddRestoredPlayer(Player&& player) {
        Player* ptr = &player;
        players_.push_back(std::make_unique<Player>(std::move(player)));
        player_tokens_.AddPlayer(players_.back().get());
    }

    void AddRestoredLostObject(LostObject&& obj) {
        lost_objects_.push_back(std::make_unique<LostObject>(std::move(obj)));
    }
    
    void SetNextLootId(LostObject::Id id) {
        next_loot_id_ = id;
    }

    double GetDogRetirementTime() const noexcept { 
        return dog_retirement_time_; 
    }

    void SetDogRetirementTime(double seconds) noexcept {
        dog_retirement_time_ = seconds;
    }

    using PlayerRetiredSignal = boost::signals2::signal<void(const RetiredPlayerRecord&)>;

    PlayerRetiredSignal& OnPlayerRetired() { 
        return on_player_retired_; 
    }

    double GetCurrentGameTime() const noexcept {
    return current_game_time_;
    }
    
private:
    using MapIdHasher = util::TaggedHasher<Map::Id>;
    using MapIdToIndex = std::unordered_map<Map::Id, size_t, MapIdHasher>;

    std::vector<Map> maps_;
    MapIdToIndex map_id_to_index_;
    std::vector<std::unique_ptr<Player>> players_;
    PlayerTokens player_tokens_;
    static std::mt19937_64::result_type GetRandomSeed(){
        std::random_device rd;
        return rd();
    }
    std::mt19937_64 generator_;
    double default_dog_speed_;
    void MovePlayer(Player& player, double time_delta_sec);
    void HandleCollision(Player& player, Point2d old_pos, Point2d new_pos, const Map& map);
    double FindBoundaryX(Point2d pos, double speed_x, const Map& map);
    double FindBoundaryY(Point2d pos, double speed_y, const Map& map);
    bool randomize_spawn_points_ = false;
    void GenerateLoot(double time_delta_sec);
    double dog_retirement_time_{60.0};

    LootGeneratorConfig loot_generator_config_;
    std::unordered_map<Map::Id, loot_gen::LootGenerator, util::TaggedHasher<Map::Id>> loot_generators_;
    std::vector<std::unique_ptr<LostObject>> lost_objects_;
    LostObject::Id next_loot_id_ = 0;
    void ProcessItemCollection(const Map& map, const std::unordered_map<Player::Id, Point2d>& old_position);
    const extra_data::MapExtraData* extra_data_ = nullptr;
    PlayerRetiredSignal on_player_retired_;
    double current_game_time_ = 0.0;
};

}  // namespace model