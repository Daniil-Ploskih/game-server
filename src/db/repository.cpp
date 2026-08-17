#include "repository.h"
#include <pqxx/pqxx>
#include <stdexcept>

namespace db {

GameRepository::GameRepository(const std::string& db_url, size_t pool_size)
    : pool_(pool_size, [db_url]() {
        auto conn = std::make_shared<pqxx::connection>(db_url);
        return conn;
    }) {
}

void GameRepository::Initialize() {
    auto conn = pool_.GetConnection();
    pqxx::work txn{*conn};
    
    txn.exec("CREATE TABLE IF NOT EXISTS retired_players ("
             "id SERIAL PRIMARY KEY, "
             "name TEXT NOT NULL, "
             "score BIGINT NOT NULL, "
             "play_time DOUBLE PRECISION NOT NULL"
             ")");
             
    txn.exec("CREATE INDEX IF NOT EXISTS idx_leaderboard ON retired_players (score DESC, play_time ASC, name ASC)");
    
    txn.commit();
}

void GameRepository::SaveRetirementRecord(const RetiredPlayer& record) {
    auto conn = pool_.GetConnection();
    pqxx::work txn{*conn};
    
    txn.exec_params(
        "INSERT INTO retired_players (name, score, play_time) VALUES ($1, $2, $3)",
        record.name, record.score, record.play_time
    );
    
    txn.commit();
}

std::vector<RetiredPlayer> GameRepository::GetLeaderboard(int start, int max_items) {
    auto conn = pool_.GetConnection();
    pqxx::work txn{*conn};
    
    long long limit = static_cast<long long>(max_items);
    long long offset = static_cast<long long>(start);

    auto result = txn.exec_params(
        "SELECT name, score, play_time FROM retired_players ORDER BY score DESC, play_time ASC, name ASC LIMIT $1 OFFSET $2",
        limit, 
        offset
    );
    
    std::vector<RetiredPlayer> records;
    records.reserve(result.size());
    for (const auto& row : result) {
        records.push_back({
            row[0].as<std::string>(),
            row[1].as<int64_t>(),
            row[2].as<double>()
        });
    }
    return records;
}

} // namespace db