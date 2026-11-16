# Build toolchain for mysmall-tv: PlatformIO Core only.
# The ESP32 platform, compilers and libraries are downloaded by PlatformIO on
# the first build into the cache directory mounted by build.sh.
FROM python:3.12

# 6.1.19 is the version platformio.ini's pioarduino pin (55.03.311) is tested with.
ARG PLATFORMIO_VERSION=6.1.19
RUN pip install --no-cache-dir "platformio==${PLATFORMIO_VERSION}"

ENV HOME=/cache \
    PLATFORMIO_CORE_DIR=/cache/.platformio \
    PLATFORMIO_SETTING_ENABLE_TELEMETRY=no

WORKDIR /src
