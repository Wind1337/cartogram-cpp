FROM ubuntu:latest

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    manpages-dev \
    software-properties-common \
    libcgal-dev \
    libomp-dev \
    libcairo2-dev \
    nlohmann-json3-dev \
    fftw3-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy the compiled binary
ARG CARTOGRAM_BIN
COPY ${CARTOGRAM_BIN} /opt/cartogram

# Set the binary as the entrypoint
ENTRYPOINT ["/opt/cartogram"]