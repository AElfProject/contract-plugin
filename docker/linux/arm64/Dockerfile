FROM arm64v8/ubuntu
RUN apt-get update && \
    apt-get install -y protobuf-compiler cmake gcc-aarch64-linux-gnu g++-aarch64-linux-gnu zip
WORKDIR /home
ENV CXX /usr/bin/aarch64-linux-gnu-g++
ENV CC /usr/bin/aarch64-linux-gnu-gcc

