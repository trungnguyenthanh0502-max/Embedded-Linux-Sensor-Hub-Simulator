set(CMAKE_AUTOMOC ON)
add_executable(dashboard main.cpp)
target_link_libraries(dashboard PRIVATE Qt6::Widgets Qt6::Charts Qt6::Network)
