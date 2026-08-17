#pragma once

#include "connection_pool.h"
#include <string>
#include <vector>
#include <cstdint>

namespace db {

struct RetiredPlayer {
    std::string name;
    int64_t score;
    double play_time;
};

class GameRepository {
public:
    explicit GameRepository(const std::string& db_url, size_t pool_size = 4);

    void Initialize();

    void SaveRetirementRecord(const RetiredPlayer& record);

    std::vector<RetiredPlayer> GetLeaderboard(int start, int max_items);

private:
    ConnectionPool pool_;
};

} // namespace db