# camerobot ROS 2 Guide

This package builds two executables:
- `talker` (`src/pi_camera_publisher.cpp`): publishes ROS text messages and serves TCP payloads.
- `listener` (`src/desktop_subscriber.cpp`): receives TCP payloads and republishes to ROS.

Current payload format is versioned text (`v13|...`) and may include a frame payload field (`|frame=`) when a camera is available.

## Mac (listener in Docker)

Use these commands inside your ROS 2 Jazzy container:

```bash
cd /workspace
source /opt/ros/jazzy/setup.bash
colcon build --packages-select camerobot --symlink-install
source install/setup.bash

# Local default mode (connects to localhost:8080)
ros2 run camerobot listener

# Remote mode (Pi talker on your LAN)
ros2 run camerobot listener --remote-ip <pi-ip> --remote-port 8080
```

`listener` options:
- `--remote-ip <ip-or-host>`: remote talker host (`--remote-host` alias is also supported).
- `--remote-port <port>`: remote talker TCP port.
- If options are omitted, defaults are `localhost:8080`.

## Raspberry Pi (talker)

Build and run on Pi:

```bash
cd ~/ros2_ws
source /opt/ros/jazzy/setup.bash
colcon build --packages-select camerobot --symlink-install --parallel-workers 1 --cmake-args -DCMAKE_BUILD_PARALLEL_LEVEL=1
source install/setup.bash
ros2 run camerobot talker --tcp-port 8080
```

If `/dev/video0` is available, talker embeds frame payloads in TCP messages and saves `frames/last_frame_sent.jpg` locally.
If no camera is available, talker still runs and sends versioned text payloads.

## Sync Mac -> Pi

When code changes locally, copy package to Pi before rebuilding:

```bash
scp -r ~/ros2_ws/src/camerobot <pi-user>@<pi-ip>:~/ros2_ws/src/
```

If you are running from the ROS Docker container used in this repo, use the container path:

```bash
scp -r /workspace/src/camerobot <pi-user>@<pi-ip>:~/ros2_ws/src/
```

Then rebuild on Pi using the build command above.

## Swampy Pi Build (swap + clean + rebuild)

Use this when Pi builds are slow or memory-constrained.

Mac/container sync:

```bash
cd /workspace
scp -r /workspace/src/camerobot <pi-user>@<pi-ip>:/home/<pi-user>/ros2_ws/src/
```

Pi swap, clean, build, run:

```bash
cd /home/<pi-user>/ros2_ws
source /opt/ros/jazzy/setup.bash

sudo swapoff -a 2>/dev/null || true
sudo rm -f /swapfile
sudo fallocate -l 4G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
grep -q '^/swapfile ' /etc/fstab || echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab
echo 'vm.swappiness=60' | sudo tee /etc/sysctl.d/99-swappiness.conf
sudo sysctl -w vm.swappiness=60

pkill -f "ros2 run camerobot talker" || true
pkill -f "ros2 run camerobot listener" || true
rm -rf /home/<pi-user>/ros2_ws/build /home/<pi-user>/ros2_ws/install /home/<pi-user>/ros2_ws/log /home/<pi-user>/ros2_ws/frames

colcon build --packages-select camerobot --symlink-install --parallel-workers 1 --cmake-args -DCMAKE_BUILD_PARALLEL_LEVEL=1
source /home/<pi-user>/ros2_ws/install/setup.bash
ros2 run camerobot talker --tcp-port 8080
```

Mac/container listener + pull latest Pi frame:

```bash
cd /workspace
source /opt/ros/jazzy/setup.bash
source /workspace/install/setup.bash
ros2 run camerobot listener --remote-ip <pi-ip> --remote-port 8080
mkdir -p /workspace/frames
scp <pi-user>@<pi-ip>:/home/<pi-user>/ros2_ws/frames/last_frame_sent.jpg /workspace/frames/last_frame_from_pi.jpg
```

## Quick Local Smoke Test (single machine)

```bash
cd /workspace
source /opt/ros/jazzy/setup.bash
source install/setup.bash

# Terminal 1
ros2 run camerobot talker --tcp-port 8080

# Terminal 2
ros2 run camerobot listener
```

## Notes

- Build requires OpenCV (`core`, `imgproc`, `imgcodecs`, `videoio`).
- Keep `ROS_DOMAIN_ID` aligned if you use ROS networking between hosts.
- For low-memory Pi builds, keep `--parallel-workers 1 --cmake-args -DCMAKE_BUILD_PARALLEL_LEVEL=1`.

