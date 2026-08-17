#include "http_server.h"
#include "json_loader.h"
#include "request_handler.h"
#include "logger.h"
#include "ticker.h"
#include "extra_data.h"
#include "state_serialization.h"

#include <boost/log/trivial.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>
using boost::log::add_value;

#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <thread>
#include <signal.h>
#include <filesystem>
#include <optional>

using namespace std::literals;

namespace net = boost::asio;
using tcp = net::ip::tcp;
constexpr int64_t MillisecondsPerSecond = 1000;


namespace {

struct Args {
    std::optional<int> tick_period;
    std::string config_file;
    std::string www_root;
    bool randomize_spawn_points = false;
    
    std::string state_file; 
    int save_state_period = 0; 
};

std::optional<Args> ParseCommandLine(int argc, const char* const argv[]) {
    namespace po = boost::program_options;
    
    Args args;
    po::options_description desc("Allowed options");
    
    desc.add_options()
        ("help,h", "produce help message")
        ("tick-period,t", 
         po::value<int>()->value_name("milliseconds"), 
         "set tick period")
        ("config-file,c", 
         po::value<std::string>(&args.config_file)->value_name("file")->required(),
         "set config file path")
        ("www-root,w", 
         po::value<std::string>(&args.www_root)->value_name("dir")->required(),
         "set static files root")
        ("randomize-spawn-points", 
         po::bool_switch(&args.randomize_spawn_points),
         "spawn dogs at random positions")
        ("state-file", po::value<std::string>(&args.state_file), "set state file path")
        ("save-state-period", po::value<int>(&args.save_state_period), "set save state period in ms");
    
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    
    if (vm.contains("help")) {
        std::cout << desc << std::endl;
        return std::nullopt;
    }
    
    try {
        po::notify(vm);
    } 
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cout << desc << std::endl;
        return std::nullopt;
    }
    
    if (vm.contains("tick-period")) {
        args.tick_period = vm["tick-period"].as<int>();
    }
    
    return args;
}

template <typename Fn>
void RunWorkers(unsigned n, const Fn& fn) {
    n = std::max(1u, n);
    std::vector<std::jthread> workers;
    workers.reserve(n - 1);
    while (--n) {
        workers.emplace_back(fn);
    }
    fn();
}

}  // namespace

int main(int argc, const char* argv[]) {
    try {
        auto args = ParseCommandLine(argc, argv);
        if (!args) {
            return EXIT_SUCCESS;
        }
        
        InitLogger();

        extra_data::MapExtraData extra_data;
        model::Game game = json_loader::LoadGame(args->config_file, extra_data);
        const char* db_url = std::getenv("GAME_DB_URL");
        if (!db_url) {
            throw std::runtime_error("GAME_DB_URL environment variable is not set");
        }

        db::GameRepository db_repo{db_url, std::thread::hardware_concurrency()};
        db_repo.Initialize();

        game.OnPlayerRetired().connect(
            [&db_repo](const model::RetiredPlayerRecord& record) {
                try {
                    db::RetiredPlayer db_record{
                        record.name,
                        record.score,
                        record.play_time
                    };
                    db_repo.SaveRetirementRecord(db_record);
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << "Failed to save retirement record: " << e.what();
                }
            }
        );
        
        game.SetExtraData(&extra_data);
        game.InitLootGenerators();
        game.SetRandomizeSpawnPoints(args->randomize_spawn_points);
        
        std::filesystem::path static_dir = args->www_root;

        const unsigned num_threads = std::thread::hardware_concurrency();
        net::io_context ioc(num_threads);
        
        auto api_strand = net::make_strand(ioc);

        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](auto, auto){
            ioc.stop();
        });

        {
            boost::json::value startup_data{{"port", 8080}, {"address", "0.0.0.0"}};
            BOOST_LOG_TRIVIAL(info)
                << add_value("AdditionalData", startup_data)
                << "server started"sv;
        }

        http_handler::RequestHandler base_handler{
            game, 
            extra_data,
            static_dir, 
            args->tick_period.has_value(),
            db_repo,
            args->state_file,
            std::chrono::milliseconds{args->save_state_period}
        };
        http_handler::LoggingRequestHandler handler{base_handler};

        const auto address = net::ip::make_address("0.0.0.0");
        constexpr net::ip::port_type port = 8080;

        http_server::ServeHttp(ioc, {address, port}, 
        [&handler](auto&& req, auto&& send) {
            handler(std::forward<decltype(req)>(req), std::forward<decltype(send)>(send));
        });
        
        if (!args->state_file.empty()) {
            std::filesystem::path state_path = args->state_file;
            if (std::filesystem::exists(state_path)) {
                try {
                    BOOST_LOG_TRIVIAL(info) << "Loading state from " << state_path;
                    state_serialization::LoadState(state_path, game);
                }
                catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << "Failed to load state: " << e.what();
                    return EXIT_FAILURE;
                }
            } 
            else {
                BOOST_LOG_TRIVIAL(info) << "State file not found, starting with clean state";
            }
        }        

        std::shared_ptr<Ticker> ticker;
        if (args->tick_period) {
            ticker = std::make_shared<Ticker>(
                api_strand,
                std::chrono::milliseconds(*args->tick_period),
                [&game, &base_handler](std::chrono::milliseconds delta) {
                    game.Tick(delta.count() / MillisecondsPerSecond);
                }
            );
            ticker->Start();
        }

        RunWorkers(std::max(1u, num_threads), [&ioc] {
            ioc.run();
        });

        if (!args->state_file.empty()) {
            try {
                BOOST_LOG_TRIVIAL(info) << "Saving state to " << args->state_file;
                state_serialization::SaveState(args->state_file, game);
            } 
            catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "Failed to save state: " << e.what();
            }
        }
        
        {
            boost::json::value exit_data{{"code", 0}};
            BOOST_LOG_TRIVIAL(info)
                << add_value("AdditionalData", exit_data)
                << "server exited"sv;
        }
        
    } 
    catch (const std::exception& ex) {
        {
            boost::json::value exit_data{
                {"code", EXIT_FAILURE},
                {"exception", ex.what()}
            };
            BOOST_LOG_TRIVIAL(error)
                << add_value("AdditionalData", exit_data)
                << "server exited"sv;
        }
        return EXIT_FAILURE;
    }
}