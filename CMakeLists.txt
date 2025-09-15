cmake_minimum_required(VERSION 4.00)
project(Test)
set(CMAKE_CXX_COMPILER "g++")

# СПринудительно установить стандарт c++ 20
# Исправление опечатки в STANDART -> STANDARD
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Включить предупреждения компилятора
add_compile_options(-Wall -Wextra -Wpedantic)
# Включить отладочную информацию
add_compile_options(-g)


# Добавить исходники
add_executable(Test main.cpp)

# Добавить директорию с заголовочными файлами
target_include_directories(Test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
