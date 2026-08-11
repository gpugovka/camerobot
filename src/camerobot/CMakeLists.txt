cmake_minimum_required(VERSION 3.8)
project(camerobot)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio)

add_library(frame_image_serialization STATIC src/frame_image_serialization.cpp)
ament_target_dependencies(frame_image_serialization rclcpp)
target_link_libraries(frame_image_serialization ${OpenCV_LIBS})
target_include_directories(frame_image_serialization PUBLIC include ${OpenCV_INCLUDE_DIRS})

add_executable(talker src/pi_camera_publisher.cpp)
ament_target_dependencies(talker rclcpp std_msgs ament_index_cpp)
target_link_libraries(talker frame_image_serialization)

target_include_directories(talker PRIVATE include)

add_executable(listener src/desktop_subscriber.cpp)
ament_target_dependencies(listener rclcpp std_msgs)
target_link_libraries(listener frame_image_serialization)

target_include_directories(listener PRIVATE include)

install(TARGETS talker listener frame_image_serialization DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY resources DESTINATION share/${PROJECT_NAME})
ament_package()
