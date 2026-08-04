#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

class DesktopSubscriber : public rclcpp::Node
{
public:
  DesktopSubscriber()
  : Node("desktop_subscriber")
  {
    subscription_ = this->create_subscription<std_msgs::msg::String>(
      "topic",
      10,
      [this](std_msgs::msg::String::UniquePtr msg) {
        RCLCPP_INFO(this->get_logger(), "Received: '%s'", msg->data.c_str());
      });
  }

private:
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<DesktopSubscriber>());
  rclcpp::shutdown();
  return 0;
}
