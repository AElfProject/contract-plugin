#!/bin/bash
## Builder
ARG BUILDPLATFORM
FROM --platform=$BUILDPLATFORM luxel/alpine-cmake
RUN apk update && apk upgrade
RUN apk add git
WORKDIR /usr/app
COPY . /usr/app
RUN git submodule init
RUN git submodule update
RUN cmake .
# # RUN <install build dependecies/compiler>
# # COPY <source> .
ARG TARGETPLATFORM
RUN make
# RUN cd opt/bin && ls -al
## Runtime
# FROM alpine:latest
# # RUN <install runtime dependencies installed via emulation>
# COPY --from=build build/opt/bin /bin