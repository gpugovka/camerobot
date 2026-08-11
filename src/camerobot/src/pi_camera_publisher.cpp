#include <arpa/inet.h>
#include <chrono>
#include <condition_variable>
#include <cerrno>
#include <cstring>
#include <deque>
#include <functional>
#include <filesystem>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <cstdlib>
#include <string>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <thread>
#include <unistd.h>

#include "camerobot/frame_image_serialization.hpp"
#include "camerobot/test_image_provider.hpp"
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

static std::string summarize_wire_message(const std::string &text)
{
  const size_t marker_pos = text.find("|frame=");
  if (marker_pos == std::string::npos) {
    return text + " [frame=none]";
  }

  const std::string prefix = text.substr(0, marker_pos);
  const size_t frame_chars = text.size() - (marker_pos + 7);
  return prefix + " [frame=attached chars=" + std::to_string(frame_chars) + "]";
}

class PiCameraPublisher : public rclcpp::Node
{
public:
  PiCameraPublisher(uint16_t port, const std::string &test_image_path, bool test_mode)
  : Node("pi_camera_publisher"), count_(0), port_(port), running_(true), listen_fd_(-1), client_fd_(-1)
  {
    publisher_ = create_publisher<std_msgs::msg::String>("topic", 10);

    struct utsname sys_info;
    uname(&sys_info);
    machine_info_ = std::string(sys_info.sysname) + " (" + std::string(sys_info.machine) + ")";

    test_provider_ = camerobot::TestImageProvider::make_if_active(test_image_path, test_mode, get_logger());
    if (!test_provider_) {
      select_capture_workflow();
    }

    timer_ = create_wall_timer(500ms, std::bind(&PiCameraPublisher::timer_callback, this));
    start_tcp_server();
  }

  ~PiCameraPublisher() override
  {
    stop_tcp_server();
  }

private:
  enum class CaptureWorkflow {
    None,
    RpicamStill,
    OpenCvV4L2,
    OpenCvDefault,
  };

  bool detect_rpicam_still() const
  {
    const int result = std::system("command -v rpicam-still >/dev/null 2>&1");
    return result == 0;
  }

  bool has_video_device() const
  {
    return std::filesystem::exists("/dev/video0");
  }

  const char *capture_workflow_name() const
  {
    switch (capture_workflow_) {
      case CaptureWorkflow::RpicamStill:
        return "rpicam-still";
      case CaptureWorkflow::OpenCvV4L2:
        return "opencv-v4l2";
      case CaptureWorkflow::OpenCvDefault:
        return "opencv-default";
      case CaptureWorkflow::None:
      default:
        return "none";
    }
  }

  void log_camera_properties()
  {
    RCLCPP_INFO(
      get_logger(), "Capture workflow=%s width=%.0f height=%.0f fps=%.0f",
      capture_workflow_name(),
      camera_.get(cv::CAP_PROP_FRAME_WIDTH),
      camera_.get(cv::CAP_PROP_FRAME_HEIGHT),
      camera_.get(cv::CAP_PROP_FPS));
  }

  void select_capture_workflow()
  {
    if (detect_rpicam_still()) {
      capture_workflow_ = CaptureWorkflow::RpicamStill;
      RCLCPP_INFO(get_logger(), "Capture workflow selected: rpicam-still");
      return;
    }

    RCLCPP_WARN(get_logger(), "rpicam-still not found; checking OpenCV camera backends");

    if (!has_video_device()) {
      capture_workflow_ = CaptureWorkflow::None;
      RCLCPP_WARN(get_logger(), "No /dev/video0 device available; frame payloads will be skipped");
      return;
    }

    if (initialize_camera(cv::CAP_V4L2)) {
      capture_workflow_ = CaptureWorkflow::OpenCvV4L2;
      camera_ready_ = true;
      log_camera_properties();
      return;
    }

    RCLCPP_WARN(
      get_logger(),
      "OpenCV V4L2 backend could not open /dev/video0; trying default backend as last fallback");

    if (initialize_camera(cv::CAP_ANY)) {
      capture_workflow_ = CaptureWorkflow::OpenCvDefault;
      camera_ready_ = true;
      log_camera_properties();
      return;
    }

    capture_workflow_ = CaptureWorkflow::None;
    RCLCPP_WARN(get_logger(), "No camera workflow could be initialized; frame payloads will be skipped");
  }

  bool initialize_camera(int api_preference)
  {
    if (camera_.open(0, api_preference)) {
      camera_.set(cv::CAP_PROP_FRAME_WIDTH, 640);
      camera_.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
      camera_.set(cv::CAP_PROP_FPS, 15);
      camera_.set(cv::CAP_PROP_BUFFERSIZE, 1);
      return true;
    }

    return false;
  }

  void start_tcp_server()
  {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
      RCLCPP_WARN(get_logger(), "Could not create TCP socket: %s", std::strerror(errno));
      return;
    }

    int enable = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
      RCLCPP_WARN(get_logger(), "Could not bind TCP socket on port %u: %s", port_, std::strerror(errno));
      close(listen_fd_);
      listen_fd_ = -1;
      return;
    }

    if (listen(listen_fd_, 1) < 0) {
      RCLCPP_WARN(get_logger(), "Could not listen on TCP socket: %s", std::strerror(errno));
      close(listen_fd_);
      listen_fd_ = -1;
      return;
    }

    accept_thread_ = std::thread([this]() { accept_loop(); });
    RCLCPP_INFO(get_logger(), "TCP bridge listening on port %u", port_);
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
      RCLCPP_INFO(get_logger(), "TCP client connected from %s", inet_ntoa(client_addr.sin_addr));

      std::deque<std::string> outbound;
      while (running_.load()) {
        {
          std::unique_lock<std::mutex> lock(queue_mutex_);
          queue_cv_.wait(lock, [this] {
            return !outgoing_queue_.empty() || !running_.load();
          });
          if (!running_.load()) {
            break;
          }
          outgoing_queue_.swap(outbound);
        }

        if (!send_to_client(outbound)) {
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
      if (!send_all(client_fd_, message.c_str(), message.size())) {
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
    if (!client_connected_.load()) {
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
    message.data = "v13|Hello from " + machine_info_ + "! Count: " + std::to_string(count_++);
    publisher_->publish(message);

    std::string wire_message = message.data;
    std::string frame_serialized;
    if (test_provider_) {
      frame_serialized = test_provider_->next_frame(get_logger());
    } else if (capture_workflow_ == CaptureWorkflow::RpicamStill) {
      frame_serialized = serialize_frame_to_string_using_rpicam_still(get_logger());
    } else if (camera_ready_) {
      frame_serialized = serialize_frame_to_string(camera_, camera_ready_, get_logger());
    }
    if (!frame_serialized.empty()) {
      const std::vector<uint8_t> jpeg_bytes = deserialize_frame_to_jpeg_bytes(frame_serialized);
      if (!jpeg_bytes.empty()) {
        save_jpeg_bytes_to_file(jpeg_bytes, "last_frame_sent.jpg");
      }
      wire_message += "|frame=" + frame_serialized;
    }
    wire_message += "\n";
    const std::string log_message = wire_message.substr(0, wire_message.size() - 1);
    RCLCPP_INFO(get_logger(), "Publishing payload: '%s'", summarize_wire_message(log_message).c_str());
    queue_message(wire_message);
  }

  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  size_t count_;
  std::string machine_info_;
  cv::VideoCapture camera_;
  bool camera_ready_ = false;
  CaptureWorkflow capture_workflow_ = CaptureWorkflow::None;
  std::unique_ptr<camerobot::TestImageProvider> test_provider_;

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
  std::string test_image_path;
  bool test_mode = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--tcp-port") == 0 && i + 1 < argc) {
      tcp_port = static_cast<uint16_t>(std::stoi(argv[++i]));
    } else if (std::strcmp(argv[i], "--test-image") == 0 && i + 1 < argc) {
      test_image_path = argv[++i];
    } else if (std::strcmp(argv[i], "--test-mode") == 0) {
      test_mode = true;
    }
  }

  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<PiCameraPublisher>(tcp_port, test_image_path, test_mode));
  rclcpp::shutdown();
  return 0;
}
