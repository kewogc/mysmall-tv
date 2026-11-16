#!/bin/sh
# Native tests for the build screen: its state machine, the Drone replies it
# reads and the PNG decoder for the avatars. Needs a C++ compiler, python3 and
# zlib; ArduinoJson comes from the firmware build's libdeps, as for the other
# tests.
set -eu
cd "$(dirname "$0")/../.."
JSON_INCLUDE=${THEME_JSON_INCLUDE:-.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src}
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/ci tests/ci/test_tracker.cpp -o /tmp/smalltv-ci-tracker-tests
/tmp/smalltv-ci-tracker-tests
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/ci -I"$JSON_INCLUDE" tests/ci/test_parse.cpp -o /tmp/smalltv-ci-parse-tests
/tmp/smalltv-ci-parse-tests
python3 tests/ci/make_png.py /tmp/smalltv-ci-png
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/ci tests/ci/test_png.cpp -lz -o /tmp/smalltv-ci-png-tests
/tmp/smalltv-ci-png-tests /tmp/smalltv-ci-png
