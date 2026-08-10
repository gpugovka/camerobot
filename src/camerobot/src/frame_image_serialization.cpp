#include "camerobot/frame_image_serialization.hpp"
#include "camerobot/base64.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <rclcpp/rclcpp.hpp>

namespace
{
bool capture_with_rpicam(std::vector<uint8_t> &jpeg_bytes)
{
  const std::string output_path = "/tmp/camerobot_frame.jpg";
  const std::string command =
    "rpicam-still -n -t 1 --immediate -o " + output_path + " >/dev/null 2>&1";

  if (std::system(command.c_str()) != 0) {
    return false;
  }

  std::ifstream in(output_path, std::ios::binary);
  if (!in.is_open()) {
    return false;
  }

  jpeg_bytes.assign(
    std::istreambuf_iterator<char>(in),
    std::istreambuf_iterator<char>());

  std::error_code ec;
  std::filesystem::remove(output_path, ec);
  return !jpeg_bytes.empty();
}
}  // namespace

std::string serialize_frame_to_string(cv::VideoCapture &camera, bool camera_ready, const rclcpp::Logger &logger)
{
  std::vector<uint8_t> encoded_jpeg;
  if (capture_with_rpicam(encoded_jpeg)) {
    return camerobot::base64_encode(encoded_jpeg);
  }

  static bool warned_rpicam_capture = false;
  if (!warned_rpicam_capture) {
    RCLCPP_WARN(logger, "rpicam-still capture failed; falling back to OpenCV VideoCapture");
    warned_rpicam_capture = true;
  }

  if (!camera_ready) {
    return "";
  }

  cv::Mat frame;
  if (!camera.read(frame) || frame.empty()) {
    return "";
  }
  encoded_jpeg.clear();
  if (!cv::imencode(".jpg", frame, encoded_jpeg)) {
    return "";
  }
  return camerobot::base64_encode(encoded_jpeg);
}

std::vector<uint8_t> deserialize_frame_to_jpeg_bytes(const std::string &frame_serialized)
{
  return camerobot::base64_decode(frame_serialized);
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
