#include <cmath>
#include <catch2/catch_test_macros.hpp>

#include "../src/model.h"
#include "../src/loot_generator.h"

using namespace std::literals;

model::Map CreateTestMap(const std::string& id, size_t loot_type_count) {
    model::Map::Id map_id{id};
    model::Map map{map_id, "Test Map", std::nullopt};
    map.SetLootTypeCount(loot_type_count);
    
    model::Road road{model::Road::HORIZONTAL, model::Point{0, 0}, 100};
    map.AddRoad(road);
    
    model::Office office{
        model::Office::Id{"o0"},
        model::Point{100, 0},
        model::Offset{0, 0}
    };
    map.AddOffice(office);
    
    return map;
}

SCENARIO("Loot generation") {
    using loot_gen::LootGenerator;
    using TimeInterval = LootGenerator::TimeInterval;

    GIVEN("a loot generator") {
        LootGenerator gen{1s, 1.0};

        constexpr TimeInterval TIME_INTERVAL = 1s;

        WHEN("loot count is enough for every looter") {
            THEN("no loot is generated") {
                for (unsigned looters = 0; looters < 10; ++looters) {
                    for (unsigned loot = looters; loot < looters + 10; ++loot) {
                        INFO("loot count: " << loot << ", looters: " << looters);
                        REQUIRE(gen.Generate(TIME_INTERVAL, loot, looters) == 0);
                    }
                }
            }
        }

        WHEN("number of looters exceeds loot count") {
            THEN("number of loot is proportional to loot difference") {
                for (unsigned loot = 0; loot < 10; ++loot) {
                    for (unsigned looters = loot; looters < loot + 10; ++looters) {
                        INFO("loot count: " << loot << ", looters: " << looters);
                        REQUIRE(gen.Generate(TIME_INTERVAL, loot, looters) == looters - loot);
                    }
                }
            }
        }
    }

    GIVEN("a loot generator with some probability") {
        constexpr TimeInterval BASE_INTERVAL = 1s;
        LootGenerator gen{BASE_INTERVAL, 0.5};

        WHEN("time is greater than base interval") {
            THEN("number of generated loot is increased") {
                CHECK(gen.Generate(BASE_INTERVAL * 2, 0, 4) == 3);
            }
        }

        WHEN("time is less than base interval") {
            THEN("number of generated loot is decreased") {
                const auto time_interval
                    = std::chrono::duration_cast<TimeInterval>(std::chrono::duration<double>{
                        1.0 / (std::log(1 - 0.5) / std::log(1.0 - 0.25))});
                CHECK(gen.Generate(time_interval, 0, 4) == 1);
            }
        }
    }

    GIVEN("a loot generator with custom random generator") {
        LootGenerator gen{1s, 0.5, [] {
                              return 0.5;
                          }};
        WHEN("loot is generated") {
            THEN("number of loot is proportional to random generated values") {
                const auto time_interval
                    = std::chrono::duration_cast<TimeInterval>(std::chrono::duration<double>{
                        1.0 / (std::log(1 - 0.5) / std::log(1.0 - 0.25))});
                CHECK(gen.Generate(time_interval, 0, 4) == 0);
                CHECK(gen.Generate(time_interval, 0, 4) == 1);
            }
        }
    }
}

SCENARIO("Game loot generation integration") {
    
    GIVEN("a game with one map and configured generator") {
        model::Game game;
        game.SetLootGeneratorConfig(std::chrono::milliseconds(1000), 1.0);
        
        auto map = CreateTestMap("map1", 2); 
        game.AddMap(std::move(map));
        
        game.InitLootGenerators();
        
        WHEN("a player joins and tick happens") {
            auto result = game.JoinGame("Player1", model::Map::Id{"map1"});
            REQUIRE(result.status == model::JoinGameResult::Status::Ok);
            
            game.Tick(1.0); 
            
            THEN("at least one loot object appears on the map") {
                auto lost_objects = game.GetLostObjectsOnMap(model::Map::Id{"map1"});
                CHECK(lost_objects.size() >= 1);
            }
            
            THEN("loot coordinates are strictly on the road") {
                auto lost_objects = game.GetLostObjectsOnMap(model::Map::Id{"map1"});
                if (!lost_objects.empty()) {
                    const auto* obj = lost_objects[0];
                    CHECK(obj->GetPosition().y == 0.0);
                    CHECK(obj->GetPosition().x >= 0.0);
                    CHECK(obj->GetPosition().x <= 100.0);
                }
            }
            
            THEN("loot type is within valid range") {
                auto lost_objects = game.GetLostObjectsOnMap(model::Map::Id{"map1"});
                for (const auto* obj : lost_objects) {
                    CHECK(obj->GetType() < 2);
                }
            }
        }
    }
    
    GIVEN("a game with no players") {
        model::Game game;
        game.SetLootGeneratorConfig(std::chrono::milliseconds(1000), 1.0);
        
        auto map = CreateTestMap("map1", 1);
        game.AddMap(std::move(map));
        game.InitLootGenerators();
        
        WHEN("multiple ticks happen") {
            game.Tick(1.0);
            game.Tick(1.0);
            game.Tick(1.0);
            
            THEN("no loot is generated because there are no looters") {
                auto lost_objects = game.GetLostObjectsOnMap(model::Map::Id{"map1"});
                CHECK(lost_objects.empty());
            }
        }
    }
    
    GIVEN("a game with limited capacity") {
        model::Game game;
        game.SetLootGeneratorConfig(std::chrono::milliseconds(1000), 1.0);
        
        auto map = CreateTestMap("map1", 1);
        game.AddMap(std::move(map));
        game.InitLootGenerators();
        
        game.JoinGame("Player1", model::Map::Id{"map1"});
        game.JoinGame("Player2", model::Map::Id{"map1"});
        
        WHEN("many ticks pass") {
            for (int i = 0; i < 10; ++i) {
                game.Tick(1.0);
            }
            
            THEN("loot count never exceeds player count") {
                auto lost_objects = game.GetLostObjectsOnMap(model::Map::Id{"map1"});
                CHECK(lost_objects.size() <= 2);
            }
        }
    }
    
    GIVEN("a game with multiple maps") {
        model::Game game;
        game.SetLootGeneratorConfig(std::chrono::milliseconds(1000), 1.0);
        
        auto map1 = CreateTestMap("map1", 1);
        auto map2 = CreateTestMap("map2", 1);
        game.AddMap(std::move(map1));
        game.AddMap(std::move(map2));
        game.InitLootGenerators();
        
        game.JoinGame("Player1", model::Map::Id{"map1"});
        
        WHEN("tick happens") {
            game.Tick(1.0);
            
            THEN("loot appears only on map with players") {
                auto loot1 = game.GetLostObjectsOnMap(model::Map::Id{"map1"});
                auto loot2 = game.GetLostObjectsOnMap(model::Map::Id{"map2"});
                
                CHECK(loot1.size() >= 1);
                CHECK(loot2.empty());
            }
        }
    }
}

