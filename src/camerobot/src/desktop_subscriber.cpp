#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <functional>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "camerobot/frame_image_serialization.hpp"
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

using namespace std::chrono_literals;

class DesktopSubscriber : public rclcpp::Node
{
public:
  DesktopSubscriber(const std::string &remote_ip, uint16_t remote_port)
  : Node("desktop_subscriber"), remote_ip_(remote_ip), remote_port_(remote_port), running_(true), socket_fd_(-1)
  {
    publisher_ = this->create_publisher<std_msgs::msg::String>("topic", 10);
    receiver_thread_ = std::thread([this]() { receiver_loop(); });
  }

  ~DesktopSubscriber() override
  {
    running_.store(false);
    if (receiver_thread_.joinable()) {
      receiver_thread_.join();
    }
    close_socket();
  }

private:
  void receiver_loop()
  {
    while (running_.load()) {
      if (!connect_to_remote()) {
        sleep_retry();
        continue;
      }
      read_loop();
      close_socket();
      sleep_retry();
    }
  }

  bool connect_to_remote()
  {
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
      return false;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(remote_port_);
    if (inet_pton(AF_INET, remote_ip_.c_str(), &server_addr.sin_addr) != 1) {
      close_socket();
      return false;
    }

    if (connect(socket_fd_, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) < 0) {
      close_socket();
      return false;
    }
    return true;
  }

  void read_loop()
  {
    std::string buffer;
    buffer.reserve(4096);
    char temp[4096];
    while (running_.load()) {
      ssize_t count = recv(socket_fd_, temp, sizeof(temp), 0);
      if (count <= 0) {
        break;
      }

      buffer.append(temp, static_cast<size_t>(count));
      size_t pos;
      while ((pos = buffer.find('\n')) != std::string::npos) {
        std::string line = buffer.substr(0, pos);
        buffer.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        if (!line.empty()) {
          publish_remote_message(line);
        }
      }
    }
  }

  void publish_remote_message(const std::string &text)
  {
    auto message = std_msgs::msg::String();
    message.data = text;
    publisher_->publish(message);

    const size_t marker_pos = text.find("|frame=");
    if (marker_pos != std::string::npos) {
      const std::string frame_serialized = text.substr(marker_pos + 7);
      const std::vector<uint8_t> jpeg_bytes = deserialize_frame_to_jpeg_bytes(frame_serialized);
      if (!jpeg_bytes.empty()) {
        save_jpeg_bytes_to_file(jpeg_bytes, "last_frame.jpg");
      }
    }
  }

  void sleep_retry()
  {
    for (int i = 0; i < 5 && running_.load(); ++i) {
      std::this_thread::sleep_for(1s);
    }
  }

  void close_socket()
  {
    if (socket_fd_ >= 0) {
      close(socket_fd_);
      socket_fd_ = -1;
    }
  }

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  std::string remote_ip_;
  uint16_t remote_port_;
  std::atomic<bool> running_;
  int socket_fd_;
  std::thread receiver_thread_;
};

int main(int argc, char *argv[])
{
  std::string remote_ip = "127.0.0.1";
  uint16_t remote_port = 8080;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--remote-ip") == 0 && i + 1 < argc) {
      remote_ip = argv[++i];
    } else if (std::strcmp(argv[i], "--remote-port") == 0 && i + 1 < argc) {
      remote_port = static_cast<uint16_t>(std::stoi(argv[++i]));
    }
  }

  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<DesktopSubscriber>(remote_ip, remote_port));
  rclcpp::shutdown();
  return 0;
}
