FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

# Install required packages (base + Qt dependencies)
RUN apt update && apt install -y \
    git build-essential cmake curl unzip ninja-build \
    g++ wget pkg-config make zip tar python3 nano sudo \
    software-properties-common ca-certificates gnupg \
    lsb-release libfontconfig-dev libpulse-dev libpulse0 \
    libpipewire-0.3-0 libxcb-cursor0 libxcb-xinerama0 \
    libxkbcommon-x11-0 libx11-xcb1 libxcb1 libxcb-render0 \
    libxcb-shape0 libxcb-shm0 libopengl-dev libgl1-mesa-dev \
    libxcb-icccm4 libxcb-util1 libxcb-image0 libxcb-keysyms1 \
    libxcb-randr0 libxcb-render-util0 libxcb-xfixes0 \
    libxcb-sync1 libxcb-xkb1 \
    && apt clean

# Clone and bootstrap vcpkg
WORKDIR /opt
RUN git clone https://github.com/Microsoft/vcpkg.git
WORKDIR /opt/vcpkg
RUN ./bootstrap-vcpkg.sh -disableMetrics
ENV VCPKG_ROOT=/opt/vcpkg

# Clone PDF4QT
WORKDIR /root
RUN git clone https://github.com/JakubMelka/PDF4QT.git
ENV VCPKG_OVERLAY_PORTS=/root/PDF4QT/vcpkg/overlays

# Copy Qt installer (must be present next to Dockerfile)
COPY qt-online-installer-linux-x64-4.10.0.run /root/qt-installer.run

# Build arguments for Qt credentials
ARG QT_EMAIL
ARG QT_PASS

# Install Qt
RUN chmod +x /root/qt-installer.run && \
    /root/qt-installer.run \
    --platform minimal \
    --email $QT_EMAIL \
    --pw $QT_PASS \
    --root /opt/Qt \
    --accept-licenses \
    --default-answer \
    --confirm-command \
    install qt6.9.1-full-dev

# Set environment variables
ENV PATH=/opt/Qt/6.9.1/gcc_64/bin:$PATH
ENV CMAKE_PREFIX_PATH=/opt/Qt/6.9.1/gcc_64/lib/cmake
ENV VCPKG_ROOT=/opt/vcpkg
ENV VCPKG_OVERLAY_PORTS=/root/PDF4QT/vcpkg/overlays
ENV PDF4QT_QT_ROOT=/opt/Qt/6.9.1/gcc_64

# Persist env variables to root shell
RUN echo 'export PATH=/opt/Qt/6.9.1/gcc_64/bin:$PATH' >> /root/.bashrc && \
    echo 'export CMAKE_PREFIX_PATH=/opt/Qt/6.9.1/gcc_64/lib/cmake' >> /root/.bashrc && \
    echo 'export VCPKG_ROOT=/opt/vcpkg' >> /root/.bashrc && \
    echo 'export VCPKG_OVERLAY_PORTS=/root/PDF4QT/vcpkg/overlays' >> /root/.bashrc && \
    echo 'export PDF4QT_QT_ROOT=/opt/Qt/6.9.1/gcc_64' >> /root/.bashrc

# Reinstall essential tools (redundant but safe)
RUN apt update && apt install -y build-essential cmake g++ make ninja-build && apt clean

# Final working directory
WORKDIR /root

CMD ["bash"]