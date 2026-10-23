cmake_minimum_required(VERSION 3.16)
project(scms_host_tests C)
enable_testing()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(MODEL ../firmware/main)
add_executable(scms_tests test_scms.c
  ${MODEL}/access_control.c ${MODEL}/occupancy.c ${MODEL}/lighting.c
  ${MODEL}/scheduler.c ${MODEL}/logger.c ${MODEL}/serial_menu.c)
target_include_directories(scms_tests PRIVATE ${MODEL})
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(scms_tests PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
add_test(NAME scms_policy_parser_logger_energy COMMAND scms_tests)
