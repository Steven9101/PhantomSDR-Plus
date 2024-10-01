#!/bin/bash

killall -s 9 spectrumserver
killall -s 9 rx888_stream

RUST_BACKTRACE=1

# use -d for dithering in example below

# rx888_stream -f ./rx888_stream/SDDC_FX3.img -s 60000000 -g 60 -m low -d --pga -o - | build/spectrumserver --config config-rx888mk2.toml > /dev/null 2>&1 &

# use -r for randomise rounding of sampled. Seems to work a bit better then dithering for weak signals.

rx888_stream -f ./rx888_stream/SDDC_FX3.img -s 60000000 -g 60 -m low -d --pga -o - | build/spectrumserver --config config-rx888mk2.toml > /dev/null 2>&1 &

exit


