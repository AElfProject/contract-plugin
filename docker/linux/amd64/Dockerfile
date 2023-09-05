FROM amd64/ubuntu
RUN apt-get update && \
    apt-get install -y protobuf-compiler cmake gcc g++
WORKDIR /home
ENV CXX /usr/bin/g++
ENV CC /usr/bin/gcc