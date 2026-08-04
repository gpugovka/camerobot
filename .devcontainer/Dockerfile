FROM ros:jazzy-ros-base

# Install common tools and build essentials
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    python3-pip \
    python3-colcon-common-extensions \
    && rm -rf /var/lib/apt/lists/*

# Create a workspace folder and set it as the working directory
WORKDIR /workspace

# Copy package source into container workspace
COPY . /workspace

# Default shell
SHELL ["/bin/bash", "-lc"]
