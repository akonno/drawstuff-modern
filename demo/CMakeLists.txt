cmake_minimum_required(VERSION 3.16)
project(drawstuff-modern-demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(demo_minimal demo_minimal.cpp)
add_executable(demo_spheres_100k demo_spheres_100k.cpp)

target_link_libraries(demo_minimal PRIVATE drawstuff-modern)
target_link_libraries(demo_spheres_100k PRIVATE drawstuff-modern)

# 通常はこれで <drawstuff/drawstuff.h> が見える（drawstuff-modern が PUBLIC で include/ を公開しているため）
# もし見えない場合だけ、次の1行を有効化：
# target_include_directories(demo_minimal PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include)
