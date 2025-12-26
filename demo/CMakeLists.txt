cmake_minimum_required(VERSION 3.16)
project(drawstuff-modern-demo LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(demo_minimal demo_minimal.cpp)
add_executable(demo_100k_objects demo_100k_objects.cpp)

target_link_libraries(demo_minimal PRIVATE drawstuff-modern)
target_link_libraries(demo_100k_objects PRIVATE drawstuff-modern)

# 通常はこれで <drawstuff/drawstuff.h> が見える（drawstuff-modern が PUBLIC で include/ を公開しているため）
# もし見えない場合だけ、次の1行を有効化：
# target_include_directories(demo_minimal PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include)
