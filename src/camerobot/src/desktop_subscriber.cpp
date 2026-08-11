#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <functional>
#include <memory>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "camerobot/frame_image_serialization.hpp"
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

using namespace std::chrono_literals;

namespace
{
constexpr const char *kDefaultRemoteHost = "localhost";
constexpr uint16_t kDefaultRemotePort = 8080;

std::string summarize_wire_message(const std::string &text)
{
  const size_t marker_pos = text.find("|frame=");
  if (marker_pos == std::string::npos) {
    return text + " [frame=none]";
  }

  const std::string prefix = text.substr(0, marker_pos);
  const size_t frame_chars = text.size() - (marker_pos + 7);
  return prefix + " [frame=attached chars=" + std::to_string(frame_chars) + "]";
}

void print_usage(const char *program_name)
{
  std::printf(
    "Usage: %s [--remote-ip <ip-or-host>] [--remote-port <port>]\n"
    "Defaults: --remote-ip %s --remote-port %u\n",
    program_name,
    kDefaultRemoteHost,
    static_cast<unsigned>(kDefaultRemotePort));
}

bool parse_remote_options(
  int argc,
  char *argv[],
  std::string &remote_host,
  uint16_t &remote_port,
  bool &show_help)
{
  remote_host = kDefaultRemoteHost;
  remote_port = kDefaultRemotePort;
  show_help = false;

  for (int i = 1; i < argc; ++i) {
    if ((std::strcmp(argv[i], "--remote-ip") == 0 || std::strcmp(argv[i], "--remote-host") == 0) && i + 1 < argc) {
      remote_host = argv[++i];
    } else if (std::strcmp(argv[i], "--remote-port") == 0 && i + 1 < argc) {
      const long parsed_port = std::strtol(argv[++i], nullptr, 10);
      if (parsed_port < 1 || parsed_port > 65535) {
        std::fprintf(stderr, "Invalid --remote-port value: %ld\n", parsed_port);
        return false;
      }
      remote_port = static_cast<uint16_t>(parsed_port);
    } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      show_help = true;
      return true;
    } else {
      std::fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[i]);
      return false;
    }
  }

  return true;
}
}  // namespace

class DesktopSubscriber : public rclcpp::Node
{
public:
  DesktopSubscriber(const std::string &remote_host, uint16_t remote_port)
  : Node("desktop_subscriber"), remote_host_(remote_host), remote_port_(remote_port), running_(true), socket_fd_(-1)
  {
    publisher_ = create_publisher<std_msgs::msg::String>("topic", 10);
    RCLCPP_INFO(
      get_logger(),
      "TCP bridge target configured: host=%s port=%u",
      remote_host_.c_str(),
      static_cast<unsigned>(remote_port_));
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
        RCLCPP_WARN(
          get_logger(),
          "TCP connect failed to %s:%u; retrying in 5s",
          remote_host_.c_str(),
          static_cast<unsigned>(remote_port_));
        sleep_retry();
        continue;
      }
      RCLCPP_INFO(
        get_logger(),
        "TCP bridge connected to %s:%u",
        remote_host_.c_str(),
        static_cast<unsigned>(remote_port_));
      read_loop();
      RCLCPP_WARN(get_logger(), "TCP bridge disconnected; reconnecting");
      close_socket();
      sleep_retry();
    }
  }

  bool connect_to_remote()
  {
    close_socket();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *result = nullptr;
    const std::string port_text = std::to_string(remote_port_);
    const int gai_status = getaddrinfo(remote_host_.c_str(), port_text.c_str(), &hints, &result);
    if (gai_status != 0) {
      RCLCPP_WARN(
        get_logger(),
        "Address resolution failed for %s:%s: %s",
        remote_host_.c_str(),
        port_text.c_str(),
        gai_strerror(gai_status));
      return false;
    }

    bool connected = false;
    for (addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
      const int candidate_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (candidate_fd < 0) {
        continue;
      }

      if (connect(candidate_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
        socket_fd_ = candidate_fd;
        connected = true;
        break;
      }

      close(candidate_fd);
    }

    freeaddrinfo(result);
    return connected;
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
    RCLCPP_INFO(get_logger(), "Received from TCP bridge: '%s'", summarize_wire_message(text).c_str());
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
  std::string remote_host_;
  uint16_t remote_port_;
  std::atomic<bool> running_;
  int socket_fd_;
  std::thread receiver_thread_;
};

int main(int argc, char *argv[])
{
  std::string remote_host;
  uint16_t remote_port = 0;
  bool show_help = false;
  if (!parse_remote_options(argc, argv, remote_host, remote_port, show_help)) {
    print_usage(argv[0]);
    return 1;
  }
  if (show_help) {
    print_usage(argv[0]);
    return 0;
  }

  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<DesktopSubscriber>(remote_host, remote_port));
  rclcpp::shutdown();
  return 0;
}
