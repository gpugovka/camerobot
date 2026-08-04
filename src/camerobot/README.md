# camerobot ROS2 Lab Guide

This README documents the minimal working ROS2 workflow for the `camerobot` package.

It covers:
- Building and running the Mac listener in Docker
- Setting up a fresh Raspberry Pi to build and run the talker
- Incremental update, build, and run commands

## Mac listener setup

1. Install Docker Desktop for Mac.
2. Open Terminal.
3. Create the workspace:

```bash
mkdir -p ~/ros2_ws/src
cd ~/ros2_ws
```

4. Start the ROS2 Jazzy container:

```bash
docker run -it --rm \
  --name ros2-jazzy-mac \
  -v ~/ros2_ws:/workspace \
  -w /workspace \
  osrf/ros:jazzy-desktop
```

5. Confirm `camerobot` is under `src/`:

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

1. Flash Ubuntu 24.04 aarch64 to the Pi.
2. Boot it and connect with SSH.
3. Install ROS2 Jazzy and build tools:

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

4. Create the workspace:

```bash
mkdir -p ~/ros2_ws/src
cd ~/ros2_ws/src
```

5. Copy the package from Mac to Pi:

```bash
scp -r ~/ros2_ws/src/camerobot erinb@<pi-ip>:~/ros2_ws/src/
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
scp -r ~/ros2_ws/src/camerobot erinb@<pi-ip>:~/ros2_ws/src/
```

### Rebuild after changes

On Mac listener container:

```bash
cd /workspace
source /opt/ros/jazzy/setup.bash
colcon build --packages-select camerobot --symlink-install
source install/setup.bash
export ROS_DOMAIN_ID=0
ros2 run camerobot listener
```

On Raspberry Pi:

```bash
cd ~/ros2_ws
source /opt/ros/jazzy/setup.bash
colcon build --packages-select camerobot --symlink-install --parallel-workers 1 --cmake-args -DCMAKE_BUILD_PARALLEL_LEVEL=1
source install/setup.bash
export ROS_DOMAIN_ID=0
ros2 run camerobot talker
```

## Important notes

- Keep `ROS_DOMAIN_ID` the same on both machines.
- Always source `/opt/ros/jazzy/setup.bash` before building and running.
- Use `source install/setup.bash` after `colcon build`.
- If the Pi has low memory, enable swap or use `--parallel-workers 1`.

## What to avoid

- Do not try to build before ROS is installed on the Pi.
- Do not use `python3-rosdep2` if `python3-rosdep` is available.
- Do not pass `-j1` directly as a CMake argument.

## Quick commands

### Build listener on Mac

```bash
source /opt/ros/jazzy/setup.bash
cd /workspace
colcon build --packages-select camerobot --symlink-install
source install/setup.bash
export ROS_DOMAIN_ID=0
ros2 run camerobot listener
```

### Build talker on Pi

```bash
source /opt/ros/jazzy/setup.bash
cd ~/ros2_ws
colcon build --packages-select camerobot --symlink-install --parallel-workers 1 --cmake-args -DCMAKE_BUILD_PARALLEL_LEVEL=1
source install/setup.bash
export ROS_DOMAIN_ID=0
ros2 run camerobot talker
```
