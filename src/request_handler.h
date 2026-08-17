#pragma once
#include "http_server.h"
#include "model.h"
#include "logger.h"
#include "extra_data.h"
#include "state_serialization.h"
#include <boost/json.hpp>
#include <string>
#include <string_view>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <boost/log/utility/manipulators/add_value.hpp>
#include "db/repository.h"

using boost::log::add_value;
using namespace std::literals;

namespace http_handler {
namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;
constexpr size_t AuthTokenLength = 32;

inline std::string_view GetPath(std::string_view target) {
    if (auto pos = target.find('?'); pos != std::string_view::npos) {
        return target.substr(0, pos);
    }
    return target;
}

using StringResponse = http::response<http::string_body>;
constexpr int64_t MillisecondsPerSecond = 1000;

namespace endpoints {
    constexpr std::string_view MapsList = "/api/v1/maps";
    constexpr std::string_view MapsListWithSlash = "/api/v1/maps/";
    constexpr std::string_view MapsPrefix = "/api/v1/maps/";
    
    constexpr std::string_view JoinGame = "/api/v1/game/join";
    constexpr std::string_view JoinGameWithSlash = "/api/v1/game/join/";
    
    constexpr std::string_view GetPlayers = "/api/v1/game/players";
    constexpr std::string_view GetPlayersWithSlash = "/api/v1/game/players/";
    
    constexpr std::string_view GameState = "/api/v1/game/state";
    constexpr std::string_view GameStateWithSlash = "/api/v1/game/state/";
    
    constexpr std::string_view PlayerAction = "/api/v1/game/player/action";
    constexpr std::string_view PlayerActionWithSlash = "/api/v1/game/player/action/";
    
    constexpr std::string_view Tick = "/api/v1/game/tick";
    constexpr std::string_view TickWithSlash = "/api/v1/game/tick/";

    constexpr std::string_view GameRecords = "/api/v1/game/records";
    constexpr std::string_view GameRecordsWithSlash = "/api/v1/game/records/";
}

bool IsSubPath(std::filesystem::path path, std::filesystem::path base);
std::string UrlDecode(std::string_view str);
std::string GetMimeType(const std::filesystem::path& path);
json::value GetContentTypeAsJson(const StringResponse& resp);
std::string DirectionToString(model::Direction dir);

class StaticFileHandler {
public:
    explicit StaticFileHandler(std::filesystem::path static_dir)
        : static_dir_{std::move(static_dir)} {}

    template <typename Body, typename Allocator, typename Send>
    void operator()(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        if (req.method() != http::verb::get && req.method() != http::verb::head) {
            SendMethodNotAllowed(req, "GET", std::move(send));
            return;
        }

        std::string decoded = UrlDecode(req.target());
        if (!decoded.empty() && decoded.front() == '/') {
            decoded.erase(0, 1);
        }
        if (decoded.empty() || decoded.back() == '/') {
            decoded += "index.html";
        }

        auto full = std::filesystem::weakly_canonical(static_dir_ / decoded);
        if (!IsSubPath(full, static_dir_)) {
            SendJsonError(http::status::bad_request, "badRequest", "Bad request", req, std::move(send));
            return;
        }

        std::error_code ec;
        if (!std::filesystem::is_regular_file(full, ec) || ec) {
            StringResponse res{http::status::not_found, req.version()};
            res.set(http::field::content_type, "text/plain");
            res.body() = "Not found"; 
            res.prepare_payload(); 
            send(std::move(res)); 
            return;
        }

        std::ifstream file(full, std::ios::binary | std::ios::ate);
        if (!file) {
            SendJsonError(http::status::internal_server_error, "internalError", "Failed to read file", req, std::move(send));
            return;
        }

        auto size = file.tellg();
        file.seekg(0);
        std::string content(size, '\0');
        file.read(content.data(), size);

        StringResponse res{http::status::ok, req.version()};
        res.set(http::field::content_type, GetMimeType(full));
        res.set(http::field::cache_control, "no-cache");
        res.body() = std::move(content);
        res.prepare_payload();
        send(std::move(res));
    }

private:
    template <typename Body, typename Allocator, typename Send>
    void SendJsonError(http::status status, std::string_view code, std::string_view message,
                      const http::request<Body, http::basic_fields<Allocator>>& req, Send&& send) {
        StringResponse res{status, req.version()};
        res.set(http::field::content_type, "application/json");
        json::object error_body{{"code", std::string(code)}, {"message", std::string(message)}};
        res.body() = boost::json::serialize(error_body);
        res.prepare_payload();
        send(std::move(res));
    }

    template <typename Body, typename Allocator, typename Send>
    void SendMethodNotAllowed(const http::request<Body, http::basic_fields<Allocator>>& req,
                             std::string_view allowed_methods, Send&& send) {
        StringResponse res{http::status::method_not_allowed, req.version()};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::allow, std::string(allowed_methods));
        res.set(http::field::cache_control, "no-cache");
        json::object error_body{{"code", "invalidMethod"}, {"message", "Invalid method"}};
        res.body() = boost::json::serialize(error_body);
        res.prepare_payload();
        send(std::move(res));
    }

    std::filesystem::path static_dir_;
};

class ApiHandler {
public:
    explicit ApiHandler(model::Game& game, 
                        const extra_data::MapExtraData& extra_data, 
                        bool tick_mode_enabled,
                        db::GameRepository& db_repo,
                        std::filesystem::path state_file = {},
                        std::chrono::milliseconds save_period = std::chrono::milliseconds{0}) 
        : game_{game}
        , extra_data_{extra_data}
        , tick_mode_enabled_{tick_mode_enabled}
        , db_repo_{db_repo}
        , state_file_{std::move(state_file)}
        , save_period_{save_period}
    {}

    template <typename Body, typename Allocator, typename Send>
    void operator()(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        std::string_view path = GetPath(req.target());
        
         if (path == endpoints::MapsList || path == endpoints::MapsListWithSlash) {
        HandleMapsList(req, std::move(send));
        }
        else if (path.starts_with(endpoints::MapsPrefix)) {
            HandleMapById(req, std::move(send));
        }
        else if (path == endpoints::JoinGame || path == endpoints::JoinGameWithSlash) {
            HandleJoinGame(req, std::move(send));
        }
        else if (path == endpoints::GetPlayers || path == endpoints::GetPlayersWithSlash) {
            HandleGetPlayers(req, std::move(send));
        }
        else if (path == endpoints::GameState || path == endpoints::GameStateWithSlash) {
            HandleGameState(req, std::move(send));
        }
        else if (path == endpoints::PlayerAction || path == endpoints::PlayerActionWithSlash) {
            HandlePlayerAction(req, std::move(send));
        }
        else if (path == endpoints::Tick || path == endpoints::TickWithSlash) {
            HandleTick(req, std::move(send));
        }
        else if (path == endpoints::GameRecords || path == endpoints::GameRecordsWithSlash) {
            HandleGameRecords(req, std::move(send));
        }
        else {
            SendJsonError(http::status::bad_request, "badRequest", "Bad request", req, std::move(send));
        }
}

private:
    db::GameRepository& db_repo_; 

    template <typename Body, typename Allocator, typename Send>
    void SendJsonResponse(http::status status, const json::value& body, 
                         const http::request<Body, http::basic_fields<Allocator>>& req, Send&& send) {
        StringResponse res{status, req.version()};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        res.body() = boost::json::serialize(body);
        res.prepare_payload();
        send(std::move(res));
    }

    template <typename Body, typename Allocator, typename Send>
    void SendJsonError(http::status status, std::string_view code, std::string_view message,
                      const http::request<Body, http::basic_fields<Allocator>>& req, Send&& send) {
        json::object error_body{{"code", std::string(code)}, {"message", std::string(message)}};
        SendJsonResponse(status, error_body, req, std::move(send));
    }

    template <typename Body, typename Allocator, typename Send>
    void SendMethodNotAllowed(const http::request<Body, http::basic_fields<Allocator>>& req,
                             std::string_view allowed_methods, Send&& send) {
        StringResponse res{http::status::method_not_allowed, req.version()};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::allow, std::string(allowed_methods));
        res.set(http::field::cache_control, "no-cache");
        json::object error_body{{"code", "invalidMethod"}, {"message", "Invalid method"}};
        res.body() = boost::json::serialize(error_body);
        res.prepare_payload();
        send(std::move(res));
    }

    template <typename Body, typename Allocator, typename Send>
    void HandleMapsList(const http::request<Body, http::basic_fields<Allocator>>& req, Send&& send) {
        if (req.method() != http::verb::get) {
            SendMethodNotAllowed(req, "GET", std::move(send));
            return;
        }
        json::array arr;
        for (const auto& m : game_.GetMaps()) {
            json::object o;
            o["id"] = *m.GetId();
            o["name"] = m.GetName();
            arr.emplace_back(std::move(o));
        }
        SendJsonResponse(http::status::ok, arr, req, std::move(send));
    }

    template <typename Body, typename Allocator, typename Send>
    void HandleMapById(const http::request<Body, http::basic_fields<Allocator>>& req, Send&& send) {
        if (req.method() != http::verb::get && req.method() != http::verb::head) {
            SendMethodNotAllowed(req, "GET, HEAD", std::move(send));
            return;
        }
        std::string_view target = req.target();
        constexpr std::string_view prefix = "/api/v1/maps/";
        std::string_view id = target.substr(prefix.length());
    
        if (!id.empty() && id.back() == '/'){
            id.remove_suffix(1);
        }
    
        const model::Map* map = game_.FindMap(model::Map::Id{std::string{id}});
        if (!map) {
            SendJsonError(http::status::not_found, "mapNotFound", "Map not found", req, std::move(send));
            return;
        }
    
        json::object o;
        o["id"] = *map->GetId();
        o["name"] = map->GetName();
    
        json::array roads;
        for (const auto& r : map->GetRoads()) {
            json::object ro; ro["x0"] = r.GetStart().x; ro["y0"] = r.GetStart().y;
            if (r.IsHorizontal()) ro["x1"] = r.GetEnd().x; else ro["y1"] = r.GetEnd().y;
            roads.emplace_back(std::move(ro));
        }
        o["roads"] = std::move(roads);
    
        json::array buildings;
        for (const auto& b : map->GetBuildings()) {
            json::object bo; const auto& rect = b.GetBounds();
            bo["x"] = rect.position.x; bo["y"] = rect.position.y; bo["w"] = rect.size.width; bo["h"] = rect.size.height;
            buildings.emplace_back(std::move(bo));
        }
        o["buildings"] = std::move(buildings);
    
        json::array offices;
        for (const auto& of : map->GetOffices()) {
            json::object oo; oo["id"] = *of.GetId(); oo["x"] = of.GetPosition().x; oo["y"] = of.GetPosition().y;
            oo["offsetX"] = of.GetOffset().dx; oo["offsetY"] = of.GetOffset().dy;
            offices.emplace_back(std::move(oo));
        }
        o["offices"] = std::move(offices);
    
        if (extra_data_.HasMap(map->GetId())) {
            o["lootTypes"] = extra_data_.GetLootTypes(map->GetId());
        }
        SendJsonResponse(http::status::ok, o, req, std::move(send));
    }

    template <typename Body, typename Allocator, typename Send>
    void HandleJoinGame(const http::request<Body, http::basic_fields<Allocator>>& req, Send&& send) {
        if (req.method() != http::verb::post) {
            SendMethodNotAllowed(req, "POST", std::move(send));
            return;
        }

        boost::json::error_code ec;
        boost::json::value req_body = boost::json::parse(req.body(), ec);
        if (ec || !req_body.is_object()) {
            SendJsonError(http::status::bad_request, "invalidArgument", "Join game request parse error", req, std::move(send));
            return;
        }

        const auto& obj = req_body.as_object();
        if (!obj.contains("userName") || !obj.contains("mapId")) {
            SendJsonError(http::status::bad_request, "invalidArgument", "Join game request parse error", req, std::move(send));
            return;
        }

        try {
            std::string user_name = std::string(obj.at("userName").as_string());
            std::string map_id_str = std::string(obj.at("mapId").as_string());

            model::JoinGameResult result = game_.JoinGame(user_name, model::Map::Id{map_id_str});

            if (result.status == model::JoinGameResult::Status::InvalidName) {
                SendJsonError(http::status::bad_request, "invalidArgument", "Invalid name", req, std::move(send));
            } 
            else if (result.status == model::JoinGameResult::Status::MapNotFound) {
                SendJsonError(http::status::not_found, "mapNotFound", "Map not found", req, std::move(send));
            } 
            else {
                json::object success_body;
                success_body["authToken"] = result.token;
                success_body["playerId"] = static_cast<int64_t>(result.player_id);
                SendJsonResponse(http::status::ok, success_body, req, std::move(send));
            }
        } 
        catch (...) {
            SendJsonError(http::status::bad_request, "invalidArgument", "Join game request parse error", req, std::move(send));
        }
    }

    template <typename Body, typename Allocator, typename Send>
    void HandleGetPlayers(const http::request<Body, http::basic_fields<Allocator>>& req, Send&& send) {
        if (req.method() != http::verb::get && req.method() != http::verb::head) {
            SendMethodNotAllowed(req, "GET, HEAD", std::move(send));
            return;
        }

        auto auth_it = req.find(http::field::authorization);
        if (auth_it == req.end()) {
            SendJsonError(http::status::unauthorized, "invalidToken", "Authorization header is missing", req, std::move(send));
            return;
        }

        std::string_view auth_val = auth_it->value();
        if (!auth_val.starts_with("Bearer ")) {
            SendJsonError(http::status::unauthorized, "invalidToken", "Authorization header is missing", req, std::move(send));
            return;
        }

        std::string token = std::string(auth_val.substr(7));
        const model::Player* player = game_.FindPlayerByToken(token);
        
        if (!player) {
            SendJsonError(http::status::unauthorized, "unknownToken", "Player token has not been found", req, std::move(send));
            return;
        }

        std::vector<const model::Player*> players_on_map = game_.GetPlayersOnMap(player->GetMapId());
        
        json::object result;
        for (const auto* p : players_on_map) {
            json::object player_info;
            player_info["name"] = p->GetName();
            result[std::to_string(p->GetId())] = std::move(player_info);
        }
        SendJsonResponse(http::status::ok, result, req, std::move(send));
    }

    template <typename Body, typename Allocator, typename Send>
    void HandleGameState(const http::request<Body, http::basic_fields<Allocator>>& req, Send&& send) {
        if (req.method() != http::verb::get && req.method() != http::verb::head) {
            SendMethodNotAllowed(req, "GET, HEAD", std::move(send));
            return;
        }

        auto auth_it = req.find(http::field::authorization);
        if (auth_it == req.end()) {
            SendJsonError(http::status::unauthorized, "invalidToken", "Authorization header is missing", req, std::move(send));
            return;
        }

        std::string_view auth_val = auth_it->value();
        if (!auth_val.starts_with("Bearer ")) {
            SendJsonError(http::status::unauthorized, "invalidToken", "Authorization header is missing", req, std::move(send));
            return;
        }

        std::string token = std::string(auth_val.substr(7));

        if(token.length() != AuthTokenLength) {
            SendJsonError(http::status::unauthorized, "invalidToken", "Authorization header is required", req, std::move(send));
            return;
        }
        const model::Player* player = game_.FindPlayerByToken(token);
    
        if (!player) {
            SendJsonError(http::status::unauthorized, "unknownToken", "Player token has not been found", req, std::move(send));
            return;
        }

        std::vector<const model::Player*> players_on_map = game_.GetPlayersOnMap(player->GetMapId());
    
        json::object players_obj;
        for (const auto* p : players_on_map) {
            json::object player_info;
        
            json::array pos;
            pos.emplace_back(p->GetPosition().x);
            pos.emplace_back(p->GetPosition().y);
            player_info["pos"] = std::move(pos);
        
            json::array speed;
            speed.emplace_back(p->GetSpeed().x);
            speed.emplace_back(p->GetSpeed().y);
            player_info["speed"] = std::move(speed);
        
            player_info["dir"] = DirectionToString(p->GetDirection());
            json::array bag_array;
            for (const auto& item : p->GetBag()) {
                json::object bag_item;
                bag_item["id"] = static_cast<int64_t>(item.id);
                bag_item["type"] = static_cast<int64_t>(item.type);
                bag_array.emplace_back(std::move(bag_item));
            }

            player_info["score"] = p->GetScore();
            player_info["bag"] = std::move(bag_array);
            players_obj[std::to_string(p->GetId())] = std::move(player_info);
        }
    
        json::object result;
        result["players"] = std::move(players_obj);

        json::object lost_objects_obj;
        auto lost_objects = game_.GetLostObjectsOnMap(player->GetMapId());
        for (const auto* obj : lost_objects) {
            json::object obj_info;
            obj_info["type"] = static_cast<int64_t>(obj->GetType());
            json::array pos;
            pos.emplace_back(obj->GetPosition().x);
            pos.emplace_back(obj->GetPosition().y);
            obj_info["pos"] = std::move(pos);
            lost_objects_obj[std::to_string(obj->GetId())] = std::move(obj_info);
        }
        result["lostObjects"] = std::move(lost_objects_obj);
        SendJsonResponse(http::status::ok, result, req, std::move(send));
    }
    
    template <typename Body, typename Allocator, typename Send>
    void HandlePlayerAction(const http::request<Body, http::basic_fields<Allocator>>& req, Send&& send) {
        if (req.method() != http::verb::post) {
            SendMethodNotAllowed(req, "POST", std::move(send));
            return;
        }
    
        auto ct_it = req.find(http::field::content_type);
        if (ct_it == req.end() || ct_it->value() != "application/json") {
            SendJsonError(http::status::bad_request, "invalidArgument", "Invalid content type", req, std::move(send));
            return;
        }
    
        auto auth_it = req.find(http::field::authorization);
        if (auth_it == req.end()) {
            SendJsonError(http::status::unauthorized, "invalidToken", "Authorization header is required", req, std::move(send));
            return;
        }
    
        std::string_view auth_val = auth_it->value();
        if (!auth_val.starts_with("Bearer ")) {
            SendJsonError(http::status::unauthorized, "invalidToken", "Authorization header is required", req, std::move(send));
            return;
        }
    
        std::string token = std::string(auth_val.substr(7));
        if (token.length() != AuthTokenLength) {
            SendJsonError(http::status::unauthorized, "invalidToken", "Authorization header is required", req, std::move(send));
            return;
        }
    
        model::Player* player = game_.FindPlayerByToken(token);
        if (!player) {
            SendJsonError(http::status::unauthorized, "unknownToken", "Player token has not been found", req, std::move(send));
            return;
        }
    
        boost::json::error_code ec;
        boost::json::value req_body = boost::json::parse(req.body(), ec);
        if (ec || !req_body.is_object()) {
            SendJsonError(http::status::bad_request, "invalidArgument", "Failed to parse action", req, std::move(send));
            return;
        }
    
        const auto& obj = req_body.as_object();
        if (!obj.contains("move")) {
            SendJsonError(http::status::bad_request, "invalidArgument", "Failed to parse action", req, std::move(send));
            return;
        }
    
        std::string move_str = std::string(obj.at("move").as_string());
    
        const model::Map* map = game_.FindMap(player->GetMapId());
        double speed = game_.GetDogSpeed(*map);
    
        model::Point2d new_speed{0.0, 0.0};
        model::Direction new_direction = player->GetDirection();

        if(move_str == "U"){
            new_speed = {0.0, -speed};
            new_direction = model::Direction::UP;
            player->MarkAsActive();
        }
        else if(move_str == "D"){
            new_speed = {0.0, speed};
            new_direction = model::Direction::DOWN;
            player->MarkAsActive();
        }
        else if(move_str == "L"){
            new_speed = {-speed,0.0};
            new_direction = model::Direction::LEFT;
            player->MarkAsActive();
        }
        else if(move_str == "R"){
            new_speed = {speed, 0.0};
            new_direction = model::Direction::RIGHT;
            player->MarkAsActive();
        }
        else if(move_str == ""){
            new_speed = {0.0,0.0};
            player->MarkAsStopped(game_.GetCurrentGameTime());
        }
        else{
            SendJsonError(http::status::bad_request, "invalidArgument", "Failed to parse action", req, std::move(send));
            return;
        }

        player->SetSpeed(new_speed);
        if(move_str != ""){
            player->SetDirection(new_direction);
        }
        SendJsonResponse(http::status::ok, json::object{}, req, std::move(send));
    }

    template <typename Body, typename Allocator, typename Send>
    void HandleTick(const http::request<Body, http::basic_fields<Allocator>>& req, Send&& send) {
        if (tick_mode_enabled_) {
            SendJsonError(http::status::bad_request, "badRequest", "Invalid endpoint", req, std::move(send));
            return;
        }
        if (req.method() != http::verb::post) {
            SendMethodNotAllowed(req, "POST", std::move(send));
            return;
        }
    
        auto ct_it = req.find(http::field::content_type);
        if (ct_it == req.end() || ct_it->value() != "application/json") {
            SendJsonError(http::status::bad_request, "invalidArgument", "Invalid content type", req, std::move(send));
            return;
        }
    
        boost::json::error_code ec;
        boost::json::value req_body = boost::json::parse(req.body(), ec);
        if (ec || !req_body.is_object()) {
            SendJsonError(http::status::bad_request, "invalidArgument", "Failed to parse tick request JSON", req, std::move(send));
            return;
        }
    
        const auto& obj = req_body.as_object();
        if (!obj.contains("timeDelta")) {
            SendJsonError(http::status::bad_request, "invalidArgument", "Failed to parse tick request JSON", req, std::move(send));
            return;
        }
    
        int64_t time_delta_ms;
        try {
            time_delta_ms = obj.at("timeDelta").as_int64();
        }
        catch (...) {
            SendJsonError(http::status::bad_request, "invalidArgument", "Failed to parse tick request JSON", req, std::move(send));
            return;
        }
    
        if (time_delta_ms <= 0) {
            SendJsonError(http::status::bad_request, "invalidArgument", "Time delta must be positive", req, std::move(send));
            return;
        }
    
        double time_delta_sec = static_cast<double>(time_delta_ms) / MillisecondsPerSecond;
    
        game_.Tick(time_delta_sec);

        if (!state_file_.empty() && save_period_.count() > 0) {
            accumulated_game_time_ += std::chrono::milliseconds{time_delta_ms};
            
            if (accumulated_game_time_ >= save_period_) {
                try {
                    state_serialization::SaveState(state_file_, game_);
                    accumulated_game_time_ -= save_period_;
                } 
                catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << "Failed to auto-save state: " << e.what();
                }
            }
        }
        SendJsonResponse(http::status::ok, json::object{}, req, std::move(send));
    }
    
    model::Game& game_;
    const extra_data::MapExtraData& extra_data_; 
    bool tick_mode_enabled_;
    
    std::filesystem::path state_file_;
    std::chrono::milliseconds save_period_;
    std::chrono::milliseconds accumulated_game_time_{0};

    template <typename Body, typename Allocator, typename Send>
    void HandleGameRecords(const http::request<Body, http::basic_fields<Allocator>>& req, Send&& send) {
        if (req.method() != http::verb::get && req.method() != http::verb::head) {
            return SendMethodNotAllowed(req, "GET, HEAD", std::move(send));
        }

        int start = 0;
        int max_items = 100;
    
        std::string_view target = req.target();

        if (auto pos = target.find('?'); pos != std::string_view::npos) {
            std::string_view query = target.substr(pos + 1);
            size_t start_pos = 0;
            while (start_pos < query.size()) {
                auto amp_pos = query.find('&', start_pos);
                std::string_view param = (amp_pos == std::string_view::npos) 
                    ? query.substr(start_pos) 
                    : query.substr(start_pos, amp_pos - start_pos);
            
                if (auto eq_pos = param.find('='); eq_pos != std::string_view::npos) {
                    std::string_view key = param.substr(0, eq_pos);
                    std::string_view value = param.substr(eq_pos + 1);
                
                    try {
                        if (key == "start") {
                            start = std::stoi(std::string(value));
                        }
                        else if (key == "maxItems") {
                            max_items = std::stoi(std::string(value));
                        }
                }
                catch (const std::exception&) {
                    return SendJsonError(http::status::bad_request, "invalidArgument",
                                        "Invalid query parameters", req, std::move(send));
                }
            }
                start_pos = (amp_pos == std::string_view::npos) ? query.size() : amp_pos + 1;
            }
        }

        constexpr int MaxRecordsPerRequest = 100;
        if (start < 0 || max_items < 0 || max_items > MaxRecordsPerRequest) {
            return SendJsonError(http::status::bad_request, "invalidArgument", 
                           "Invalid parameters", req, std::move(send));
        }

        try {
            auto records = db_repo_.GetLeaderboard(start, max_items);
            json::array result;
            for (const auto& r : records) {
                json::object obj;
                obj["name"] = r.name;
                obj["score"] = r.score;
                obj["playTime"] = r.play_time;
                result.emplace_back(std::move(obj));
            }
            SendJsonResponse(http::status::ok, result, req, std::move(send));
        }
        catch (const std::exception& e) {
            SendJsonError(http::status::internal_server_error, "dbError", e.what(), req, std::move(send));
        }
    }
}; 

class RequestHandler {
public:
    RequestHandler(model::Game& game, 
                   const extra_data::MapExtraData& extra_data, 
                   std::filesystem::path static_dir, 
                   bool tick_mode_enabled,
                   db::GameRepository& db_repo,
                   std::filesystem::path state_file = {},
                   std::chrono::milliseconds save_period = std::chrono::milliseconds{0})
        : api_handler_{game, extra_data, tick_mode_enabled, db_repo, std::move(state_file), save_period}, 
          static_handler_{std::move(static_dir)} {}

    RequestHandler(const RequestHandler&) = delete;
    RequestHandler& operator=(const RequestHandler&) = delete;

    template <typename Body, typename Allocator, typename Send>
    void operator()(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        std::string_view target = req.target();
        
        if (target.starts_with("/api")) {
            api_handler_(std::move(req), std::move(send));
        } 
        else {
            static_handler_(std::move(req), std::move(send));
        }
    }

private:
    ApiHandler api_handler_;
    StaticFileHandler static_handler_;
};

template <typename BaseHandler>
class LoggingRequestHandler {
public:
    explicit LoggingRequestHandler(BaseHandler& handler) : decorated_(handler) {}

    template <typename Body, typename Allocator, typename Send>
    void operator()(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        std::string client_ip = "127.0.0.1";
        json::value request_data = json::object{
            {"ip", client_ip},
            {"URI", std::string(req.target())},
            {"method", std::string(req.method_string())}
        };

        BOOST_LOG_TRIVIAL(info)
            << add_value("AdditionalData", request_data)
            << "request received"sv;

        auto start = std::chrono::steady_clock::now();
        auto wrapped_send = [start, client_ip, send = std::forward<Send>(send)]
            (StringResponse&& resp) mutable {
            auto end = std::chrono::steady_clock::now();
            auto response_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                end - start).count();
            json::value response_data = json::object{
                {"ip", client_ip},
                {"response_time", response_time},
                {"code", static_cast<int>(resp.result_int())},
                {"content_type", GetContentTypeAsJson(resp)}
            };

            BOOST_LOG_TRIVIAL(info)
                << add_value("AdditionalData", response_data)
                << "response sent"sv;
            send(std::move(resp));
        };

        decorated_(std::move(req), std::move(wrapped_send));
    }

private:
    BaseHandler& decorated_;
};

}  // namespace http_handler