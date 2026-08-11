#include "camerobot/test_image_provider.hpp"
#include "camerobot/frame_image_serialization.hpp"
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <rclcpp/logging.hpp>

namespace camerobot {

std::unique_ptr<TestImageProvider> TestImageProvider::make_if_active(
  const std::string & test_image_path,
  bool test_mode,
  const rclcpp::Logger & logger)
{
  if (!test_image_path.empty()) {
    RCLCPP_INFO(logger, "Capture workflow selected: static test image (%s)", test_image_path.c_str());
    return std::unique_ptr<TestImageProvider>(new TestImageProvider(test_image_path));
  }

  if (test_mode) {
    const std::string share = ament_index_cpp::get_package_share_directory("camerobot");
    const std::string backyard = share + "/resources/test_backyard.jpg";
    const std::string invader  = share + "/resources/test_invader.jpg";
    RCLCPP_INFO(logger, "Capture workflow selected: test-mode (80%% backyard / 20%% invader)");
    return std::unique_ptr<TestImageProvider>(new TestImageProvider(backyard, invader));
  }

  return nullptr;
}

std::string TestImageProvider::next_frame(const rclcpp::Logger & logger)
{
  if (random_mode_) {
    const std::string & path = pick_invader_(rng_) ? invader_path_ : backyard_path_;
    return serialize_static_image(path, logger);
  }
  return serialize_static_image(fixed_path_, logger);
}

TestImageProvider::TestImageProvider(std::string fixed_path)
: fixed_path_(std::move(fixed_path)), random_mode_(false) {}

TestImageProvider::TestImageProvider(std::string backyard_path, std::string invader_path)
: backyard_path_(std::move(backyard_path)),
  invader_path_(std::move(invader_path)),
  random_mode_(true) {}

}  // namespace camerobot
