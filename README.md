# PhantomSDR-Plus WebSDR
## Note: Please dont use Ubuntu 24.04, stick to Ubuntu 22.04 as it wont compile on 24.04!
This is different Repo than the Official PhantomSDR Repo
In case you want to use Debian, it has been tested on Bookworm.

We offer more features as we only maintain support for Linux instead of the official repo
- Futuristic Design
- Decoders
- Band Plan in the Waterfall
- Statistics
- Better Colorsmap
- A different WebSDR List (https://sdr-list.xyz)
- More to come

## Issues
- If you face issues try to run it on Ubuntu, as most was tested on Ubuntu


## Features
- WebSDR which can handle multiple hundreds of users depending on the Hardware
- Common demodulation modes
- Can handle high sample rate SDRs (70MSPS real, 35MSPS IQ)
- Support for both IQ and real data

## Benchmarks
- Ryzen 5* 2600 - All Cores - RX888 MKii with 64MHZ Sample Rate, 32MHZ IQ takes up 38-40% - per User it takes about nothing, 50 Users dont even take 1% of the CPU.
- RX 580 - RX888 MKII with 64MHZ Sample Rate, 32MHZ IQ takes up 28-35% - same as the Ryzen per User it takes about nothing (should handle many)
- Intel i5-6500T - RX888 MKII - 60MHz Sample Rate, 30MHz IQ, OpenCL installed and enabled, about 10-12%. 100 users is no problem with OpenCL as the GPU does the heavy lifting.

(* Ryzen CPU's with internal GPU do not support OpenCL, if you expect high performance add a videocard or use an modern Intel i5 or i7 that supports OpenCL.)

## Screenshots

The Aachen WebSDR:
![Screenshot](/docs/websdr.PNG)

(https://sdr-list.xyz)

## Building
Optional dependencies such as cuFFT or clFFT can be installed too.
### Ubuntu Prerequisites
```
apt install build-essential cmake pkg-config meson libfftw3-dev libwebsocketpp-dev libflac++-dev zlib1g-dev libzstd-dev libboost-all-dev libopus-dev libliquid-dev git psmisc
```

### Fedora Prerequisites
```
dnf install g++ meson cmake fftw3-devel websocketpp-devel flac-devel zlib-devel boost-devel libzstd-devel opus-devel liquid-dsp-devel git
```

### Building the binary automatically
Restart your Terminal after you ran install.sh otherwise it wont work..
```
git clone --recursive https://github.com/Steven9101/PhantomSDR-Plus.git
cd PhantomSDR-Plus
chmod +x *.sh
sudo ./install.sh
```

### Building the binary manually
```
git clone --recursive https://github.com/Steven9101/PhantomSDR-Plus.git
cd PhantomSDR-Plus
meson build --optimization 3
meson compile -C build
```

## Examples
Remember to set the frequency and sample rate correctly. Default html directory is 'html/', change it with the `htmlroot` option in config.toml.
### RTL-SDR
```
rtl_sdr -f 145000000 -s 3200000 - | ./build/spectrumserver --config config.toml
```
### HackRF
```
rx_sdr -f 145000000 -s 20000000 -d driver=hackrf - | ./build/spectrumserver --config config.toml
```
## Added start files and configs for various recievers. 
Some need Soapy and RX_TOOLS installed else they do not work, e.g. Airspy Discovery and SDRPlay RSP1A.
I also added psutils as it's needed for killall command.
Do not forget to disable opencl if you didn't install it, it's recommened you do.
-- Bas ON5HB
