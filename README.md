# PhantomSDR-Plus WebSDR v2.0.0

<div align="center">
  <img src="/docs/websdr.PNG" alt="PhantomSDR-Plus WebSDR Interface" width="800"/>
  <p><em>The Heppen WebSDR running PhantomSDR-Plus v2.0.0</em></p>
</div>

## 🚀 What's New in v2.0.0

### Complete UI Overhaul
- **Fresh, modern look** - Complete visual redesign with improved usability
- **Codebase cleanup** - App.svelte reduced from ~5,000 → ~1,800 lines
- **Better component separation** - Improved code organization for easier maintenance
- **Custom backgrounds** - Drop a background.jpg/png into /assets to skin the UI

### New Features & Enhancements
- **Custom AGC controls** - Fine-tune attack (0.1–100 ms) and release (10–1,000 ms) timing
- **Smart Bands menu** - Auto-detects your ITU Region and shows the correct frequency bands
- **Bookmark Import/Export** - Save and share your favorite frequencies
- **Finetuning options** - Precise frequency control is back
- **Built-in lookups** - Callsign lookup & frequency lookup integrated
- **Advanced settings** - Settings & debug menus (AGC, Buffer, Custom AGC, etc.)
- **Config-based UI** - Server-info section moved to config.toml (toggle chatbox without rebuilding)
- **Smoother waterfall** - Improved waterfall drawing performance
- **Debug tab** - Signal-strength history and other diagnostic stats
- **Improved chat** - Chat history rewritten to eliminate crashes, and in a new json format.
- **Auto-reconnect** - More robust WebSocket handling with automatic reconnection

### Technical Improvements
- **Thread-safety fixes** - Resolved threading issues in the chat system
- **Error handling** - Error handling for file I/O operations
- **Memory safety** - Added bounds-checking for improved stability
- **Protocol refactor** - Cleaner protocol layer for easier future extensions


## 📋 Overview

PhantomSDR-Plus is a high-performance WebSDR server that can handle hundreds of concurrent users. This enhanced version focuses on Linux optimization and provides significant improvements over the original PhantomSDR.

### Key Features
- **High Performance** - Handle 100+ users simultaneously on modest hardware
- **Modern Interface** - Clean, responsive web interface with mobile support
- **Advanced Demodulation** - Support for common modes with professional features
- **Real-time Statistics** - Performance metrics and user analytics
- **Band Plan Integration** - Interactive frequency band information

## 🔧 System Requirements

### Recommended Hardware
- **CPU**: Modern multi-core processor (Intel i5/i7 or AMD Ryzen 5/7+)
- **RAM**: 4GB+ (8GB+ recommended for high user counts)
- **Storage**: 500MB for installation
- **Network**: Stable internet connection for multi-user operation

### Supported Operating Systems
- **Ubuntu 22.04 LTS** (Primary support)
- **Debian Bookworm** (Tested)
- **Fedora** (Community support)


## 📊 Performance Benchmarks

| Hardware | Sample Rate | CPU Usage | User Capacity |
|----------|-------------|-----------|---------------|
| Ryzen 5 2600 | 32MHz IQ | 38-40% | 200+ users |
| RX 580 (OpenCL) | 32MHz IQ | 28-35% | 300+ users |
| Intel i5-6500T + OpenCL | 30MHz IQ | 10-12% | 100+ users |

> 💡 **Tip**: GPU acceleration with OpenCL can significantly improve performance

## 🏗️ Installation

### Quick Start (Recommended)
The easiest way to install PhantomSDR-Plus is to use the pre-built releases:

1. **Install runtime dependencies** (Ubuntu 22.04+):
```bash
apt install libfftw3-bin libboost-iostreams1.83.0 libzstd1 libflac++10 \
           libopus0 libliquid1 libcurl4 libgomp1 libgcc-s1 libstdc++6
```

2. **Install OpenCL support** (REQUIRED for pre-built releases):
```bash
apt install ocl-icd-libopencl1 libclfft2

# For CPU-based OpenCL (if no GPU available):
apt install pocl-opencl-icd

# For NVIDIA GPUs:
apt install nvidia-opencl-icd-xxx  # (where xxx is your driver version)

# For AMD GPUs:
apt install mesa-opencl-icd
```

3. **Download and extract the latest release**:
   - Go to the [Releases](https://github.com/Steven9101/PhantomSDR-Plus/releases) page
   - Download the latest `phantomsdr-plus-x.x.x-linux-x86_64.tar.gz`
   - Extract: `tar -xzf phantomsdr-plus-*.tar.gz && cd phantomsdr-plus-*`

4. **Configure for your SDR**:
   - Copy `config.example.toml` to `config.toml`
   - Edit the configuration for your specific SDR device
   - See the [Wiki](https://github.com/Steven9101/PhantomSDR-Plus/wiki) for SDR-specific setup guides:
     - RTL-SDR setup
     - SDRPlay configuration  
     - HackRF setup
     - Other supported devices

5. **Run the server with your SDR**:
   - The server requires piped data input from your SDR
   - See the [Wiki](https://github.com/Steven9101/PhantomSDR-Plus/wiki) for complete examples:
     - RTL-SDR: `rtl_sdr -f 100000000 -s 2048000 - | ./spectrumserver --config config.toml`
     - HackRF: `hackrf_transfer -r - -f 100000000 -s 20000000 | ./spectrumserver --config config.toml`
     - SDRPlay: Use SDRPlay software to pipe data

### Building from Source (Advanced)

If you prefer to build from source or need to modify the code:

#### Build Dependencies (Ubuntu/Debian)
```bash
apt install build-essential cmake pkg-config meson libfftw3-dev \
           libwebsocketpp-dev libflac++-dev zlib1g-dev libzstd-dev \
           libboost-all-dev libopus-dev libliquid-dev git psmisc \
           libclfft-dev ocl-icd-opencl-dev nlohmann-json3-dev
```

#### Build Dependencies (Fedora)
```bash
dnf install g++ meson cmake fftw3-devel websocketpp-devel flac-devel \
           zlib-devel boost-devel libzstd-devel opus-devel \
           liquid-dsp-devel git
```

#### Compile
```bash
git clone --recursive https://github.com/Steven9101/PhantomSDR-Plus.git
cd PhantomSDR-Plus
meson build --optimization 3
meson compile -C build
```

#### Automated Build Script
```bash
chmod +x *.sh
sudo ./install.sh
```

> 🔄 **Important**: Restart your terminal after running install.sh

## 🚀 Usage Examples

### RTL-SDR
```bash
# For pre-built releases:
rtl_sdr -f 145000000 -s 3200000 - | ./spectrumserver --config config.toml

# For building from source:
rtl_sdr -f 145000000 -s 3200000 - | ./build/spectrumserver --config config.toml
```

### HackRF
```bash
# For pre-built releases:
rx_sdr -f 145000000 -s 20000000 -d driver=hackrf - | ./spectrumserver --config config.toml

# For building from source:
rx_sdr -f 145000000 -s 20000000 -d driver=hackrf - | ./build/spectrumserver --config config.toml
```

### Using Pre-built Configs
We provide optimized configurations for various SDR devices:
- `config-rsp1a.toml` - SDRPlay RSP1A
- `config-rx888mk2.toml` - RX888 MK2
- `config-airspyhf.toml` - Airspy HF+
- `config.example.rtlsdr.toml` - RTL-SDR dongles
- `config.example.hackrf.toml` - HackRF One

## ⚙️ Configuration

The server uses TOML configuration files. Key sections include:

```toml
[server]
port = 9002
html_root = "frontend/dist/"
threads = 4

[websdr]
name = "Your WebSDR Name"
operator = "Your Callsign"
register_online = true  # Register on sdr-list.xyz
chat_enabled = true     # Enable/disable chat (new in v2.0.0)
callsign_lookup_url = "https://www.qrz.com/db/"

[input]
sps = 2048000
frequency = 100000000
accelerator = "opencl"  # Enable GPU acceleration if available
```

## WebSDR Network

Join the PhantomSDR-Plus network at [sdr-list.xyz](https://sdr-list.xyz) to:
- Discover other WebSDR stations
- Share your WebSDR with the community
- Access global spectrum monitoring

## 🧪 Try the Preview

Test the new v2.0.0 interface at the live preview: [websdr.heppen.be](http://websdr.heppen.be/)

## Contributing

We welcome contributions! Please:
1. Fork the repository
2. Create a feature branch
3. Submit a pull request

## 📞 Support

- **Issues**: Report bugs on GitHub Issues
- **Documentation**: Check the Wiki for detailed guides
- **Community**: Join discussions in the [forum](https://phantomsdr.fun)

## 🔄 Migration from v1.x

Version 2.0.0 includes breaking changes. The codebase has diverged significantly from older versions.

## 📜 License

This project is licensed under the GPT License - see the LICENSE file for details.


---

<div align="center">
  <strong>⭐ Star this repo if you find it useful! ⭐</strong>
</div>
