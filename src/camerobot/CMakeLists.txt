cmake_minimum_required(VERSION 3.8)
project(camerobot)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

add_executable(publisher src/pi_camera_publisher.cpp)
ament_target_dependencies(publisher rclcpp std_msgs)

add_executable(subscriber src/desktop_subscriber.cpp)
ament_target_dependencies(subscriber rclcpp std_msgs)

install(TARGETS publisher subscriber DESTINATION lib/${PROJECT_NAME})
ament_package()
