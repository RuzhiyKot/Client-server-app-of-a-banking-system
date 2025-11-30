cmake_minimum_required(VERSION 3.10)
project(MultiClientBankSys CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Флаги компиляции
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -g")

# Включить тестирование
include(CTest)

# Директории
set(SRCDIR ${CMAKE_SOURCE_DIR}/src)
set(BINDIR ${CMAKE_SOURCE_DIR}/bin)
set(TESTDIR ${CMAKE_SOURCE_DIR}/tests)

# Куда класть бинарники
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BINDIR})

# ----- Google Test -----
enable_testing()
find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

# ----- Источники -----

set(SERVER_SOURCES
    ${SRCDIR}/main_server.cpp
    ${SRCDIR}/server.cpp
    ${SRCDIR}/database.cpp
    ${SRCDIR}/account.cpp
    ${SRCDIR}/crypto.cpp
)

set(CLIENT_SOURCES
    ${SRCDIR}/main_client.cpp
    ${SRCDIR}/client.cpp
)

set(INIT_SOURCES
    ${SRCDIR}/init_database.cpp
    ${SRCDIR}/database.cpp
    ${SRCDIR}/account.cpp
    ${SRCDIR}/crypto.cpp
)

set(VIEW_SOURCES
    ${SRCDIR}/view_database.cpp
    ${SRCDIR}/database.cpp
    ${SRCDIR}/account.cpp
    ${SRCDIR}/crypto.cpp
)

# Тестовые файлы - в папке tests
set(TEST_SOURCES
    ${TESTDIR}/test_bank_system.cpp
    ${SRCDIR}/server.cpp
    ${SRCDIR}/database.cpp
    ${SRCDIR}/account.cpp
    ${SRCDIR}/crypto.cpp
    ${SRCDIR}/client.cpp
)

# Платформенные библиотеки
if(APPLE)
    set(PLATFORM_LIBS pthread)
elseif(UNIX)
    set(PLATFORM_LIBS pthread)
elseif(WIN32)
    set(PLATFORM_LIBS ws2_32)
endif()

# ----- Основные цели -----

add_executable(bank_server ${SERVER_SOURCES})
target_link_libraries(bank_server PRIVATE ${PLATFORM_LIBS})
target_include_directories(bank_server PRIVATE ${SRCDIR})

add_executable(bank_client ${CLIENT_SOURCES})
target_link_libraries(bank_client PRIVATE ${PLATFORM_LIBS})
target_include_directories(bank_client PRIVATE ${SRCDIR})

add_executable(init_db ${INIT_SOURCES})
target_link_libraries(init_db PRIVATE ${PLATFORM_LIBS})
target_include_directories(init_db PRIVATE ${SRCDIR})

add_executable(view_db ${VIEW_SOURCES})
target_link_libraries(view_db PRIVATE ${PLATFORM_LIBS})
target_include_directories(view_db PRIVATE ${SRCDIR})

# ----- Тестовая цель -----

add_executable(bank_tests ${TEST_SOURCES})
target_link_libraries(bank_tests PRIVATE 
    GTest::gtest 
    GTest::gtest_main 
    ${PLATFORM_LIBS} 
    Threads::Threads
)
target_include_directories(bank_tests PRIVATE ${SRCDIR})

# ----- CTest настройки -----

add_test(NAME BankSystemTests COMMAND bank_tests)
set_tests_properties(BankSystemTests PROPERTIES TIMEOUT 30)
set(CTEST_OUTPUT_ON_FAILURE OFF)

# Создаём data после сборки init_db
add_custom_command(
    TARGET init_db POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/data
)

# Дополнительные цели для удобства
add_custom_target(run_tests
    COMMAND ${BINDIR}/bank_tests
    DEPENDS bank_tests
    COMMENT "Running bank system unit tests"
)

add_custom_target(test_all
    COMMAND ctest --output-on-failure
    DEPENDS bank_tests
    COMMENT "Running all tests with CTest"
)

# Информация о целях
message(STATUS " ")
message(STATUS "Available targets:")
message(STATUS "  bank_server    - Main bank server")
message(STATUS "  bank_client    - Client application") 
message(STATUS "  init_db        - Initialize test database")
message(STATUS "  view_db        - View database contents")
message(STATUS "  bank_tests     - Build unit tests")
message(STATUS "  run_tests      - Run unit tests directly")
message(STATUS "  test_all       - Run tests with CTest")
message(STATUS " ")