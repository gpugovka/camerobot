#pragma once

#include <memory>
#include <random>
#include <string>
#include <rclcpp/logger.hpp>

namespace camerobot {

class TestImageProvider
{
public:
  // Returns nullptr when neither --test-image nor --test-mode is active.
  static std::unique_ptr<TestImageProvider> make_if_active(
    const std::string & test_image_path,
    bool test_mode,
    const rclcpp::Logger & logger);

  std::string next_frame(const rclcpp::Logger & logger);

private:
  explicit TestImageProvider(std::string fixed_path);
  TestImageProvider(std::string backyard_path, std::string invader_path);

  std::string fixed_path_;
  std::string backyard_path_;
  std::string invader_path_;
  bool random_mode_ = false;
  std::mt19937 rng_{std::random_device{}()};
  std::bernoulli_distribution pick_invader_{0.20};
};

}  // namespace camerobot
