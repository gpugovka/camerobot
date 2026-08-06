#include <arpa/inet.h>
#include <chrono>
#include <condition_variable>
#include <cerrno>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <thread>
#include <unistd.h>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

using namespace std::chrono_literals;

static bool send_all(int fd, const char *data, size_t size)
{
  while (size > 0) {
    ssize_t sent = send(fd, data, size, 0);
    if (sent <= 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    data += sent;
    size -= static_cast<size_t>(sent);
  }
  return true;
}

class PiCameraPublisher : public rclcpp::Node
{
public:
  PiCameraPublisher(uint16_t port)
  : Node("pi_camera_publisher"), count_(0), port_(port), running_(true), listen_fd_(-1), client_fd_(-1)
  {
    publisher_ = this->create_publisher<std_msgs::msg::String>("topic", 10);

    struct utsname sys_info;
    uname(&sys_info);
    machine_info_ = std::string(sys_info.sysname) + " (" + std::string(sys_info.machine) + ")";

    timer_ = this->create_wall_timer(500ms, std::bind(&PiCameraPublisher::timer_callback, this));
    start_tcp_server();
  }

  ~PiCameraPublisher() override
  {
    stop_tcp_server();
  }

private:
  void start_tcp_server()
  {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
      RCLCPP_WARN(this->get_logger(), "Could not create TCP socket: %s", std::strerror(errno));
      return;
    }

    int enable = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
      RCLCPP_WARN(this->get_logger(), "Could not bind TCP socket on port %u: %s", port_, std::strerror(errno));
      close(listen_fd_);
      listen_fd_ = -1;
      return;
    }

    if (listen(listen_fd_, 1) < 0) {
      RCLCPP_WARN(this->get_logger(), "Could not listen on TCP socket: %s", std::strerror(errno));
      close(listen_fd_);
      listen_fd_ = -1;
      return;
    }

    accept_thread_ = std::thread([this]() { accept_loop(); });
    RCLCPP_INFO(this->get_logger(), "TCP bridge listening on port %u", port_);
  }

  void stop_tcp_server()
  {
    running_.store(false);
    if (listen_fd_ >= 0) {
      close(listen_fd_);
      listen_fd_ = -1;
    }
    queue_cv_.notify_all();
    if (accept_thread_.joinable()) {
      accept_thread_.join();
    }
    close_client();
  }

  void accept_loop()
  {
    while (running_.load()) {
      sockaddr_in client_addr{};
      socklen_t client_len = sizeof(client_addr);
      int fd = accept(listen_fd_, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
      if (fd < 0) {
        if (running_.load()) {
          continue;
        }
        break;
      }

      {
        std::lock_guard<std::mutex> lock(client_mutex_);
        client_fd_ = fd;
      }
      client_connected_.store(true);
      RCLCPP_INFO(this->get_logger(), "TCP client connected from %s", inet_ntoa(client_addr.sin_addr));

      std::deque<std::string> outbound;
      while (running_.load()) {
        {
          std::unique_lock<std::mutex> lock(queue_mutex_);
          queue_cv_.wait(lock, [this] {
            return \!outgoing_queue_.empty() || \!running_.load();
          });
          if (\!running_.load()) {
            break;
          }
          outgoing_queue_.swap(outbound);
        }

        if (\!send_to_client(outbound)) {
          break;
        }
      }

      close_client();
    }
  }

  bool send_to_client(const std::deque<std::string> &messages)
  {
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_fd_ < 0) {
      return false;
    }

    for (const auto &message : messages) {
      if (\!send_all(client_fd_, message.c_str(), message.size())) {
        return false;
      }
    }
    return true;
  }

  void close_client()
  {
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_fd_ >= 0) {
      close(client_fd_);
      client_fd_ = -1;
    }
    client_connected_.store(false);
  }

  void queue_message(const std::string &text)
  {
    if (\!client_connected_.load()) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      outgoing_queue_.push_back(text);
    }
    queue_cv_.notify_one();
  }

  void timer_callback()
  {
    auto message = std_msgs::msg::String();
    message.data = "v4|Hello from " + machine_info_ + "\! Count: " + std::to_string(count_++);
    RCLCPP_INFO(this->get_logger(), "Publishing: '%s'", message.data.c_str());
    publisher_->publish(message);

    std::string wire_message = message.data + "\n";
    queue_message(wire_message);
  }

  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  size_t count_;
  std::string machine_info_;

  uint16_t port_;
  std::atomic<bool> running_;
  int listen_fd_;
  int client_fd_;
  std::atomic<bool> client_connected_ = false;
  std::mutex client_mutex_;
  std::thread accept_thread_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::deque<std::string> outgoing_queue_;
};

int main(int argc, char *argv[])
{
  uint16_t tcp_port = 8080;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--tcp-port") == 0 && i + 1 < argc) {
      tcp_port = static_cast<uint16_t>(std::stoi(argv[++i]));
    }
  }

  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<PiCameraPublisher>(tcp_port));
  rclcpp::shutdown();
  return 0;
}
