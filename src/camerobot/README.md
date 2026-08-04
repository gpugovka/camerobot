# camerobot ROS 2 Starter Guide

This README documents the minimal working ROS 2 workflow for the `camerobot` package.

It covers:
- Building and running the Mac listener in Docker
- Building and running the Raspberry Pi talker
- Incremental update workflow
- Current build commands, including the low-memory Pi build command

## Mac listener setup

1. Install Docker Desktop for Mac.
2. Open Terminal.
3. Create the workspace:

```bash
mkdir -p ~/ros2_ws/src
cd ~/ros2_ws
```

4. Start the ROS 2 Jazzy container:

```bash
docker run -it --rm \
  --name ros2-jazzy-mac \
  -v ~/ros2_ws:/workspace \
  -w /workspace \
  osrf/ros:jazzy-desktop
```

5. Confirm the package is present:

```bash
ls src/camerobot
```

6. Build inside the container:

```bash
source /opt/ros/jazzy/setup.bash
rosdep update
rosdep install --from-paths src --ignore-src -r -y --rosdistro jazzy
colcon build --packages-select camerobot --symlink-install
```

7. Run the listener:

```bash
source install/setup.bash
export ROS_DOMAIN_ID=0
ros2 run camerobot listener
```

## Raspberry Pi talker setup

1. Flash Ubuntu 24.04 aarch64 to the Raspberry Pi and boot it.
2. Connect to the Pi over SSH.
3. Install ROS 2 Jazzy and build tools:

```bash
sudo apt update
sudo apt install -y curl gnupg2 lsb-release ca-certificates
sudo mkdir -p /usr/share/keyrings
curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
  | sudo gpg --dearmour -o /usr/share/keyrings/ros-archive-keyring.gpg

echo "deb [arch=arm64 signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu noble main" \
  | sudo tee /etc/apt/sources.list.d/ros2.list

sudo apt update
sudo apt install -y ros-jazzy-desktop python3-rosdep python3-colcon-common-extensions build-essential cmake
```

4. Create the workspace on the Pi:

```bash
mkdir -p ~/ros2_ws/src
cd ~/ros2_ws/src
```

5. Copy the package from your Mac to the Pi:

```bash
scp -r ~/ros2_ws/src/camerobot <pi-user>@<pi-ip>:~/ros2_ws/src/
```

6. Build on the Pi:

```bash
source /opt/ros/jazzy/setup.bash
cd ~/ros2_ws
rosdep update
rosdep install --from-paths src --ignore-src -r -y --rosdistro jazzy
colcon build --packages-select camerobot --symlink-install --parallel-workers 1 --cmake-args -DCMAKE_BUILD_PARALLEL_LEVEL=1
```

7. Run the talker:

```bash
source install/setup.bash
export ROS_DOMAIN_ID=0
ros2 run camerobot talker
```

## Incremental update workflow

### Sync code changes to the Pi

```bash
scp -r ~/ros2_ws/src/camerobot <pi-user>@<pi-ip>:~/ros2_ws/src/
```

### Rebuild after changes

On the Mac listener container:

```bash
cd /workspace
source /opt/ros/jazzy/setup.bash
colcon build --packages-select camerobot --symlink-install
source install/setup.bash
export ROS_DOMAIN_ID=0
ros2 run camerobot listener
```

On the Raspberry Pi:

```bash
cd ~/ros2_ws
source /opt/ros/jazzy/setup.bash
colcon build --packages-select camerobot --symlink-install --parallel-workers 1 --cmake-args -DCMAKE_BUILD_PARALLEL_LEVEL=1
source install/setup.bash
export ROS_DOMAIN_ID=0
ros2 run camerobot talker
```

## Screenshots

The following screenshot examples show successful talker/listener communication between the Raspberry Pi publisher and the Mac listener.

![](../screenshots/talker-running.png)

![](../screenshots/listener-running.png)

## Notes

- Keep `ROS_DOMAIN_ID` the same on both machines.
- Always source `/opt/ros/jazzy/setup.bash` before building and running.
- Use `source install/setup.bash` after `colcon build`.
- Use `--parallel-workers 1 --cmake-args -DCMAKE_BUILD_PARALLEL_LEVEL=1` on the Pi for low-memory builds.

