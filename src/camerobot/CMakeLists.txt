cmake_minimum_required(VERSION 3.8)
project(camerobot)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio)

add_executable(talker src/pi_camera_publisher.cpp src/frame_image_serialization.cpp)
ament_target_dependencies(talker rclcpp std_msgs)
target_link_libraries(talker ${OpenCV_LIBS})

add_executable(listener src/desktop_subscriber.cpp src/frame_image_serialization.cpp)
ament_target_dependencies(listener rclcpp std_msgs)
target_link_libraries(listener ${OpenCV_LIBS})

target_include_directories(talker PRIVATE include ${OpenCV_INCLUDE_DIRS})
target_include_directories(listener PRIVATE include ${OpenCV_INCLUDE_DIRS})

install(TARGETS talker listener DESTINATION lib/${PROJECT_NAME})
ament_package()
