#include "camerobot/frame_image_serialization.hpp"
#include <filesystem>
#include <fstream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

std::string serialize_frame_to_string(cv::VideoCapture &camera, bool camera_ready, const rclcpp::Logger &logger)
{
  (void)logger;
  if (!camera_ready) {
    return "";
  }
  cv::Mat frame;
  if (!camera.read(frame) || frame.empty()) {
    return "";
  }
  std::vector<uint8_t> encoded_jpeg;
  if (!cv::imencode(".jpg", frame, encoded_jpeg)) {
    return "";
  }
  return std::string(encoded_jpeg.begin(), encoded_jpeg.end());
}

std::vector<uint8_t> deserialize_frame_to_jpeg_bytes(const std::string &frame_serialized)
{
  return std::vector<uint8_t>(frame_serialized.begin(), frame_serialized.end());
}

bool save_jpeg_bytes_to_file(const std::vector<uint8_t> &jpeg_bytes, const std::string &filename)
{
  if (jpeg_bytes.empty()) {
    return false;
  }
  std::filesystem::create_directories("frames");
  std::ofstream out("frames/" + filename, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    return false;
  }
  out.write(reinterpret_cast<const char *>(jpeg_bytes.data()), static_cast<std::streamsize>(jpeg_bytes.size()));
  return out.good();
}
