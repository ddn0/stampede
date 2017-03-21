FROM ubuntu:16.10

RUN apt-get update && apt-get -qq -y install \
  cmake \
  curl \
  g++ \
  git \
  libboost-dev \
  parallel \
  python

RUN mkdir -p /build /src

WORKDIR /build
