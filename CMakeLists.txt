cmake_minimum_required(VERSION 3.11)

project(game_server CXX)
set(CMAKE_CXX_STANDARD 20)

include(${CMAKE_BINARY_DIR}/conanbuildinfo_multi.cmake)
conan_basic_setup(TARGETS)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

get_target_property(BOOST_INCLUDE_DIRS CONAN_PKG::boost INTERFACE_INCLUDE_DIRECTORIES)

add_library(game_model STATIC
    src/model.h
    src/model.cpp
    src/loot_generator.h
    src/loot_generator.cpp
    src/tagged.h
    src/collision_detector.h
    src/collision_detector.cpp
    src/geom.h
    src/extra_data.h
    src/boost_json.cpp 
)

target_include_directories(game_model PUBLIC ${BOOST_INCLUDE_DIRS})
target_link_libraries(game_model PUBLIC Threads::Threads)

add_executable(game_server
    src/main.cpp
    src/http_server.cpp
    src/http_server.h
    src/sdk.h
    src/json_loader.h
    src/json_loader.cpp
    src/request_handler.cpp
    src/request_handler.h
    src/state_serialization.cpp
    src/db/connection_pool.h
    src/db/repository.h
    src/db/repository.cpp
)

target_link_libraries(game_server PRIVATE 
    game_model
    Threads::Threads
    CONAN_PKG::boost
    CONAN_PKG::libpqxx

)

add_executable(game_server_tests
    tests/loot_generator_tests.cpp
)

target_link_libraries(game_server_tests PRIVATE
    game_model
    Threads::Threads
    CONAN_PKG::catch2
)

enable_testing()
add_test(NAME GameServerTests COMMAND game_server_tests)