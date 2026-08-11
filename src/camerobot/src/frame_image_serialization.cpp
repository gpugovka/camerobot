#include "camerobot/frame_image_serialization.hpp"
#include "camerobot/base64.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <rclcpp/logger.hpp>
#include <rclcpp/logging.hpp>

std::string serialize_frame_to_string(cv::VideoCapture &camera, bool camera_ready, const rclcpp::Logger &logger)
{
  if (!camera_ready) {
    return "";
  }

  cv::Mat frame;
  constexpr int kMaxCaptureAttempts = 4;
  bool captured = false;
  for (int attempt = 1; attempt <= kMaxCaptureAttempts; ++attempt) {
    if (!camera.read(frame) || frame.empty()) {
      continue;
    }

    if (frame.channels() == 2) {
      cv::Mat bgr;
      cv::cvtColor(frame, bgr, cv::COLOR_YUV2BGR_YUY2);
      frame = bgr;
    } else if (frame.channels() == 1) {
      cv::Mat bgr;
      cv::cvtColor(frame, bgr, cv::COLOR_GRAY2BGR);
      frame = bgr;
    }

    const cv::Scalar mean_bgr = cv::mean(frame);
    const double mean_luma = (mean_bgr[0] + mean_bgr[1] + mean_bgr[2]) / 3.0;
    if (mean_luma < 5.0 && attempt < kMaxCaptureAttempts) {
      continue;
    }

    if (mean_luma < 5.0) {
      RCLCPP_WARN(logger, "Captured very dark frame (mean=%.2f)", mean_luma);
    }

    captured = true;
    break;
  }

  if (!captured || frame.empty()) {
    RCLCPP_WARN(logger, "Camera capture failed after retries");
    return "";
  }

  std::vector<uint8_t> encoded_jpeg;
  if (!cv::imencode(".jpg", frame, encoded_jpeg)) {
    RCLCPP_WARN(logger, "JPEG encoding failed for captured frame");
    return "";
  }
  return camerobot::base64_encode(encoded_jpeg);
}

std::string serialize_frame_to_string_using_rpicam_still(const rclcpp::Logger &logger)
{
#if defined(__linux__)
  const std::filesystem::path output_path = "/tmp/test_frame.jpg";
  std::error_code error;
  std::filesystem::remove(output_path, error);

  constexpr const char *kCaptureCommand =
    "rpicam-still -o /tmp/test_frame.jpg >/dev/null 2>&1";
  const int exit_code = std::system(kCaptureCommand);
  if (exit_code != 0) {
    RCLCPP_WARN(logger, "rpicam-still command failed with exit code %d", exit_code);
    return "";
  }

  if (!std::filesystem::exists(output_path)) {
    RCLCPP_WARN(logger, "rpicam-still did not create %s", output_path.c_str());
    return "";
  }

  const uintmax_t file_size = std::filesystem::file_size(output_path, error);
  if (error || file_size < 4) {
    RCLCPP_WARN(logger, "rpicam-still produced no usable JPEG file");
    return "";
  }

  std::ifstream input(output_path, std::ios::binary);
  if (!input.is_open()) {
    RCLCPP_WARN(logger, "Could not open %s", output_path.c_str());
    return "";
  }

  std::vector<uint8_t> jpeg_bytes(static_cast<size_t>(file_size));
  input.read(reinterpret_cast<char *>(jpeg_bytes.data()), static_cast<std::streamsize>(jpeg_bytes.size()));
  if (!input.good() && !input.eof()) {
    RCLCPP_WARN(logger, "Failed reading JPEG bytes from %s", output_path.c_str());
    return "";
  }

  if (!(jpeg_bytes[0] == 0xFF && jpeg_bytes[1] == 0xD8)) {
    RCLCPP_WARN(logger, "rpicam-still output did not start with a JPEG marker");
    return "";
  }

  const size_t n = jpeg_bytes.size();
  if (!(jpeg_bytes[n - 2] == 0xFF && jpeg_bytes[n - 1] == 0xD9)) {
    RCLCPP_WARN(logger, "rpicam-still output ended with an incomplete JPEG marker");
    return "";
  }

  return camerobot::base64_encode(jpeg_bytes);
#else
  (void)logger;
  return "";
#endif
}

std::vector<uint8_t> deserialize_frame_to_jpeg_bytes(const std::string &frame_serialized)
{
  return camerobot::base64_decode(frame_serialized);
}

bool save_jpeg_bytes_to_file(const std::vector<uint8_t> &jpeg_bytes, const std::string &filename)
{
  if (jpeg_bytes.empty() || filename.empty()) {
    return false;
  }

  const std::filesystem::path output_dir = std::filesystem::current_path() / "frames";
  std::error_code error;
  std::filesystem::create_directories(output_dir, error);
  if (error) {
    return false;
  }

  const std::filesystem::path output_path = output_dir / filename;
  std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    return false;
  }

  out.write(reinterpret_cast<const char *>(jpeg_bytes.data()), static_cast<std::streamsize>(jpeg_bytes.size()));
  return out.good();
}
