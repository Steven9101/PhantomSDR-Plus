#!/bin/bash

killall -s 9 spectrumserver
killall -s 9 rx888_stream

RUST_BACKTRACE=1

rx888_stream -f ./rx888_stream/SDDC_FX3.img -s 60000000 -g 60 -m low --pga -r -d -o - | build/spectrumserver --config config-rx888mk2.toml > /dev/null 2>&1 &

exit


