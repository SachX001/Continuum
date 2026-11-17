cmake_minimum_required(VERSION 3.20)

project(continuum_uia_poc LANGUAGES CXX)

find_package(SQLite3 REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(continuum-uia-poc
    src/main.cpp
    src/claude_windows_finder.cpp
    src/uia_client.cpp
    src/ui_tree_dumper.cpp
    src/conversation_parser.cpp
    src/context_compiler.cpp
    src/evidence_repository.cpp
    src/context_proposal_engine.cpp
    src/ollama_client.cpp
)

target_link_libraries(continuum-uia-poc PRIVATE SQLite3::SQLite3)
target_link_libraries(continuum-uia-poc PRIVATE winhttp)

enable_testing()

add_executable(continuum-context-tests
    tests/context_compiler_tests.cpp
    src/conversation_parser.cpp
    src/context_compiler.cpp
    src/evidence_repository.cpp
    src/context_proposal_engine.cpp
)

target_link_libraries(continuum-context-tests PRIVATE SQLite3::SQLite3)

add_test(
    NAME context_compiler_regression
    COMMAND continuum-context-tests
)
