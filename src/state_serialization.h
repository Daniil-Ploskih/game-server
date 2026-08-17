#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/split_free.hpp>
#include "model.h"

namespace model {
template <typename Archive>
void serialize(Archive& ar, Point2d& p, [[maybe_unused]] const unsigned version) {
    ar & p.x;
    ar & p.y;
}

template <typename Archive>
void serialize(Archive& ar, Player::BagItem& item, [[maybe_unused]] const unsigned version) {
    ar & item.id;
    ar & item.type;
}

template <typename Archive>
void serialize(Archive& ar, Direction& d, [[maybe_unused]] const unsigned version) {
    if (Archive::is_saving::value) {
        int val = static_cast<int>(d);
        ar & val;
    } else {
        int val;
        ar & val;
        d = static_cast<Direction>(val);
    }
}
} // namespace model

namespace serialization {

struct PlayerRepr {
    model::Player::Id id_;
    std::string name_;
    model::Map::Id map_id_;
    std::string token_;
    model::Point2d position_;
    model::Point2d speed_;
    model::Direction direction_;
    int64_t score_;
    std::vector<model::Player::BagItem> bag_;

    PlayerRepr() 
        : id_(0)
        , map_id_(std::string{}) 
    {}

    explicit PlayerRepr(const model::Player& player)
        : id_(player.GetId())
        , name_(player.GetName())
        , map_id_(player.GetMapId())
        , token_(player.GetToken())
        , position_(player.GetPosition())
        , speed_(player.GetSpeed())
        , direction_(player.GetDirection())
        , score_(player.GetScore())
        , bag_(player.GetBag())
    {
    }

    [[nodiscard]] model::Player Restore() const {
        model::Player player{id_, name_, map_id_, token_};
        player.SetPosition(position_);
        player.SetSpeed(speed_);
        player.SetDirection(direction_);
        player.AddScore(score_);
        
        for (const auto& item : bag_) {
            player.AddToBag(item.id, item.type);
        }
        
        return player;
    }

    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar & id_;
        ar & name_;
        ar & map_id_;
        ar & token_;
        ar & position_;
        ar & speed_;
        ar & direction_;
        ar & score_;
        ar & bag_;
    }
};

struct LostObjectRepr {
    model::LostObject::Id id_;
    unsigned type_; 
    model::Point2d position_;
    model::Map::Id map_id_;

    LostObjectRepr() 
        : id_(0)
        , map_id_(std::string{})
    {}

    explicit LostObjectRepr(const model::LostObject& obj)
        : id_(obj.GetId())
        , type_(obj.GetType())
        , position_(obj.GetPosition())
        , map_id_(obj.GetMapId())
    {}

    [[nodiscard]] model::LostObject Restore() const {
        return model::LostObject{id_, type_, position_, map_id_};
    }

    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar & id_;
        ar & type_;
        ar & position_;
        ar & map_id_;
    }
};

struct GameStateRepr {
    std::vector<PlayerRepr> players;
    std::vector<LostObjectRepr> lost_objects;
    int64_t next_loot_id;
    double current_game_time;

    GameStateRepr() = default;

    explicit GameStateRepr(const model::Game& game) 
        : next_loot_id(game.GetNextLootId())
        , current_game_time(game.GetCurrentGameTime()) 
    {
        for (const auto& p_ptr : game.GetPlayers()) {
            players.emplace_back(*p_ptr);
        }
        
        for (const auto& lo_ptr : game.GetLostObjects()) {
            lost_objects.emplace_back(*lo_ptr);
        }
    }

    void Restore(model::Game& game) const {
        game.ClearState();
        
        for (const auto& player_repr : players) {
            game.AddRestoredPlayer(player_repr.Restore());
        }
        
        for (const auto& loot_repr : lost_objects) {
            game.AddRestoredLostObject(loot_repr.Restore());
        }
        
        game.SetNextLootId(next_loot_id);
    }

    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar & players;
        ar & lost_objects;
        ar & next_loot_id;
        ar & current_game_time;
    }
};

} // namespace serialization

namespace state_serialization {
    void SaveState(const std::filesystem::path& file_path, const model::Game& game);
    void LoadState(const std::filesystem::path& file_path, model::Game& game);
} // namespace state_serialization