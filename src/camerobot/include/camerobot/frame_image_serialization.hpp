#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/core/mat.hpp>
#include <opencv2/videoio.hpp>
#include <rclcpp/logger.hpp>

std::string serialize_frame_to_string(cv::VideoCapture &camera, bool camera_ready, const rclcpp::Logger &logger);
std::string serialize_frame_to_string_using_rpicam_still(const rclcpp::Logger &logger);
std::vector<uint8_t> deserialize_frame_to_jpeg_bytes(const std::string &frame_serialized);
bool save_jpeg_bytes_to_file(const std::vector<uint8_t> &jpeg_bytes, const std::string &filename);