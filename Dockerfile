#!/bin/bash
ARG BUILDPLATFORM
FROM --platform=$BUILDPLATFORM luxel/alpine-cmake
RUN apk update && apk upgrade
RUN apk add git
WORKDIR /usr/app
COPY . /usr/app
RUN git submodule init
RUN git submodule update
RUN cmake .
## Final C++ compilation stage
ARG TARGETPLATFORM
RUN make