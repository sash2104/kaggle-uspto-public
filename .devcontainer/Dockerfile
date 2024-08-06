FROM mcr.microsoft.com/devcontainers/base:ubuntu-22.04

RUN apt update && \
    apt install -y python3-pip && \
    apt clean && \
    rm -rf /var/lib/apt/lists/*

RUN ln -s /usr/bin/python3 /usr/bin/python

COPY requirements.txt /tmp
RUN python -m pip install -r /tmp/requirements.txt