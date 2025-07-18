#include "spectrumserver.h"
#include "logger.h"

#define PHANTOMSDR_VERSION "2.0.0"
#include "samplereader.h"

#include <cstdio>
#include <iostream>
#include <random>
#include <thread>
#include <boost/algorithm/string.hpp>
#include <toml++/toml.h>
#include <cstdlib> // For rand() and srand()
#include <ctime>   // For time()
#include <sstream> // For std::stringstream
#include <iostream> // For std::cout
#include <curl/curl.h> // For cURL functionality
#include "glaze/glaze.hpp"
#include <nlohmann/json.hpp>


toml::table config;
nlohmann::json markers;

void broadcast_server::check_and_update_markers() {
    while (marker_update_running) {
        

        std::string marker_file = "markers.json";
        std::ifstream file(marker_file);
        if (file.is_open()) {
            nlohmann::json new_markers;
            try {
                file >> new_markers;
                file.close();

                if (new_markers != markers) {
                    LOG_DEBUG("Markers", "Markers updated");
                    markers = new_markers;
                }
            } catch (nlohmann::json::parse_error& e) {
                LOG_ERROR("Markers", "Error parsing marker.json: " + std::string(e.what()));
            }
        } else {
            LOG_DEBUG("Markers", "marker.json not found (this is normal)");
        }
        std::this_thread::sleep_for(std::chrono::minutes(1));
    }
}

broadcast_server::broadcast_server(
    std::unique_ptr<SampleConverterBase> reader, toml::parse_result &config)
    : reader{std::move(reader)}, frame_num{0}, marker_update_running(false) {

    LOG_DEBUG("Server", "Initializing broadcast server");
    

    server_threads = config["server"]["threads"].value_or(1);

    // Read in configuration
    std::optional<int> sps_config = config["input"]["sps"].value<int>();
    if (!sps_config.has_value()) {
        throw "Missing sample rate";
    }
    sps = sps_config.value();

    std::optional<int64_t> frequency =
        config["input"]["frequency"].value<int64_t>();
    if (!frequency.has_value()) {
        throw "Missing frequency";
    }

    std::string accelerator_str =
        config["input"]["accelerator"].value_or("none");

    fft_threads = config["input"]["fft_threads"].value_or(1);

    std::optional<std::string> signal_type =
        config["input"]["signal"].value<std::string>();
    std::string signal_type_str =
        signal_type.has_value()
            ? boost::algorithm::to_lower_copy(signal_type.value())
            : "";
    if (!signal_type.has_value() ||
        (signal_type_str != "real" && signal_type_str != "iq")) {
        LOG_CRITICAL("Server", "Invalid signal type, specify either real or IQ input");
        throw "Invalid signal type, specify either real or IQ input";
    }

    is_real = signal_type_str == "real";

    fft_size = config["input"]["fft_size"].value_or(131072);
    audio_max_sps = config["input"]["audio_sps"].value_or(12000);
    min_waterfall_fft = config["input"]["waterfall_size"].value_or(1024);
    brightness_offset = config["input"]["brightness_offset"].value_or(0);
    show_other_users = config["server"]["otherusers"].value_or(1) > 0;

    default_frequency =
        config["input"]["defaults"]["frequency"].value_or(basefreq);
    default_mode_str = boost::algorithm::to_upper_copy<std::string>(
        config["input"]["defaults"]["modulation"].value_or("USB"));

    waterfall_compression_str =
        config["input"]["waterfall_compression"].value_or("zstd");
    audio_compression_str =
        config["input"]["audio_compression"].value_or("flac");

    m_docroot = config["server"]["html_root"].value_or("html/");

    limit_audio = config["limits"]["audio"].value_or(1000);
    limit_waterfall = config["limits"]["waterfall"].value_or(1000);
    limit_events = config["limits"]["events"].value_or(1000);

    // Set the parameters correct for real and IQ input
    // For IQ signal Leftmost frequency of IQ signal needs to be shifted left by
    // the sample rate
    if (is_real) {
        fft_result_size = fft_size / 2;
        basefreq = frequency.value();
    } else {
        fft_result_size = fft_size;
        basefreq = frequency.value() - sps / 2;
    }

    if (default_frequency == -1) {
        default_frequency = basefreq + sps / 2;
    }

    if (is_real) {
        default_m =
            (double)(default_frequency - basefreq) * fft_result_size * 2 / sps;
    } else {
        default_m =
            (double)(default_frequency - basefreq) * fft_result_size / sps;
    }
    int offsets_3 = (3000LL) * fft_result_size / sps;
    int offsets_5 = (5000LL) * fft_result_size / sps;
    int offsets_96 = (96000LL) * fft_result_size / sps;

    if (default_mode_str == "LSB") {
        default_mode = LSB;
        default_l = default_m - offsets_3;
        default_r = default_m;
    } else if (default_mode_str == "AM") {
        default_mode = AM;
        default_l = default_m - offsets_5;
        default_r = default_m + offsets_5;
    } else if (default_mode_str == "SAM") {
        default_mode = SAM;
        default_l = default_m - offsets_5;
        default_r = default_m + offsets_5;
    } else if (default_mode_str == "FM") {
        default_mode = FM;
        default_l = default_m - offsets_5;
        default_r = default_m + offsets_5;
    } else if (default_mode_str == "WBFM") {
        default_mode = FM;
        default_l = default_m - offsets_96;
        default_r = default_m + offsets_96;
    } else {
        default_mode = USB;
        default_l = default_m;
        default_r = default_m + offsets_3;
    }

    default_m = std::max(0., std::min((double)fft_result_size, default_m));
    default_l = std::max(0, std::min(fft_result_size, default_l));
    default_r = std::max(0, std::min(fft_result_size, default_r));

    audio_max_fft_size = ceil((double)audio_max_sps * fft_size / sps / 4.) * 4;

    if (waterfall_compression_str == "zstd") {
        waterfall_compression = WATERFALL_ZSTD;
    } else if (waterfall_compression_str == "av1") {
#ifdef HAS_LIBAOM
        waterfall_compression = WATERFALL_AV1;
#else
        LOG_CRITICAL("Server", "AV1 support not compiled in");
        throw "AV1 support not compiled in";
#endif
    }

    if (audio_compression_str == "flac") {
        audio_compression = AUDIO_FLAC;
    } else if (audio_compression_str == "opus") {
#ifdef HAS_OPUS
        audio_compression = AUDIO_OPUS;
#else
        LOG_CRITICAL("Server", "Opus support not compiled in");
        throw "Opus support not compiled in";
#endif
    }

    fft_accelerator accelerator = CPU_FFTW;
    if (accelerator_str == "cuda") {
        accelerator = GPU_cuFFT;
        std::cout << "Using CUDA" << std::endl;
    } else if (accelerator_str == "opencl") {
        accelerator = GPU_clFFT;
        std::cout << "Using OpenCL" << std::endl;
    } else if (accelerator_str == "mkl") {
        accelerator = CPU_mklFFT;
        std::cout << "Using MKL" << std::endl;
    }

    // Calculate number of downsampling levels for fft
    downsample_levels = 0;
    for (int cur_fft = fft_result_size; cur_fft >= min_waterfall_fft;
         cur_fft /= 2) {
        downsample_levels++;
    }

    if (accelerator == GPU_cuFFT) {
#ifdef CUFFT
        fft = std::make_unique<cuFFT>(fft_size, fft_threads, downsample_levels, brightness_offset);
#else
        throw "CUDA support is not compiled in";
#endif
    } else if (accelerator == GPU_clFFT) {
#ifdef CLFFT
        fft = std::make_unique<clFFT>(fft_size, fft_threads, downsample_levels, brightness_offset);
#else
        throw "OpenCL support is not compiled in";
#endif
    } else if (accelerator == CPU_mklFFT) {
#ifdef MKL
        fft =
            std::make_unique<mklFFT>(fft_size, fft_threads, downsample_levels, brightness_offset);
#else
        throw "MKL support is not compiled in";
#endif
    } else {
        fft = std::make_unique<FFTW>(fft_size, fft_threads, downsample_levels, brightness_offset);
    }
    fft->set_output_additional_size(audio_max_fft_size);

    // Initialize the websocket server
    m_server.init_asio();
    LOG_INFO("Server", "FFT:" + std::to_string(fft_size) + 
                       " | SPS:" + std::to_string(sps) + 
                       " | Freq:" + std::to_string(basefreq/1000000) + "MHz" +
                       " | " + std::string(is_real ? "Real" : "IQ") +
                       " | Audio:" + audio_compression_str +
                       " | WF:" + waterfall_compression_str);

    m_server.clear_access_channels(websocketpp::log::alevel::frame_header |
                                   websocketpp::log::alevel::frame_payload);

    m_server.set_open_handler(
        std::bind(&broadcast_server::on_open, this, std::placeholders::_1));
    m_server.set_http_handler(
        std::bind(&broadcast_server::on_http, this, std::placeholders::_1));

    // Init data structures
    waterfall_slices.resize(downsample_levels);
    waterfall_slice_mtx.resize(downsample_levels);

 
    
}

void broadcast_server::run(uint16_t port) {
    // Start the threads and handle the network
    LOG_DEBUG("Server", "Starting server threads");
    running = true;
    marker_update_running = true;
    marker_update_thread = std::thread(&broadcast_server::check_and_update_markers, this);
    m_server.set_listen_backlog(8192);
    m_server.set_reuse_addr(true);
    try {
        m_server.listen(port);
    } catch (...) { // Listen on IPv4 only if IPv6 is not supported
        m_server.listen(websocketpp::lib::asio::ip::tcp::v4(), port);
    }
    m_server.start_accept();
    fft_thread = std::thread(&broadcast_server::fft_task, this);

    set_event_timer();
    std::vector<std::thread> threads;
    // Spawn one less thread, use main thread as well
    for (int i = 0; i < server_threads - 1; i++) {
        threads.emplace_back(std::thread([&] {
            try {
                m_server.run();
            } catch (const std::exception& e) {
                LOG_ERROR("Server", "Thread error: " + std::string(e.what()));
                running = false;
            }
        }));
    }
    
    try {
        m_server.run();
    } catch (const std::exception& e) {
        LOG_ERROR("Server", "Main server error: " + std::string(e.what()));
        running = false;
    }
    
    for (int i = 0; i < server_threads - 1; i++) {
        threads[i].join();
    }
    fft_thread.join();
}





// To register on http://sdr-list.xyz
void broadcast_server::update_websdr_list() {
    LOG_INFO("SDRList", "Starting SDR list registration thread");
    
    // Seed the random number generator
    std::srand(std::time(nullptr));

    int port = config["server"]["port"].value_or(9002);
    std::optional<int64_t> center_frequency = config["input"]["frequency"].value<int64_t>();
    std::optional<int64_t> bandwidth = config["input"]["sps"].value<int64_t>();
    std::string antenna = config["websdr"]["antenna"].value_or("N/A");
    std::string grid_locator = config["websdr"]["grid_locator"].value_or("-");
    std::string hostname = config["websdr"]["hostname"].value_or("");
    std::string websdr_name = config["websdr"]["name"].value_or("WebSDR_" + std::to_string(std::rand()));
    std::string signal_type = config["input"]["signal"].value_or("real");
    std::optional<int64_t> max_users = config["limits"]["audio"].value<int64_t>();
    bool should_register = config["websdr"]["register_online"].value_or(false);

    std::string websdr_id = std::to_string(std::rand());
    
    LOG_INFO("SDRList", "SDR ID: " + websdr_id + ", Name: " + websdr_name);
    if(signal_type == "real")
    {
        bandwidth = bandwidth.value_or(30000000) / 2;
    }

    if(center_frequency.value_or(15000000) == 0){
        center_frequency = bandwidth.value_or(30000000) / 2;

    }

    // Initialize cURL outside the loop
    CURL *curl = curl_easy_init();
    CURLcode res;
    if (!curl) {
        LOG_ERROR("SDRList", "Failed to initialize cURL");
        return;
    }

    FILE *devnull = fopen("/dev/null", "w+");
    if (!devnull) {
        LOG_ERROR("SDRList", "Failed to open /dev/null");
        curl_easy_cleanup(curl);
        return;
    }
    
    int retry_count = 0;
    int backoff_seconds = 30; // Start with 30 seconds
    const int max_backoff = 3600; // Max 1 hour
    
    while(running && should_register) {
        int user_count = static_cast<int>(get_events_connections_size());

        // Construct JSON payload manually
        glz::json_t json_data = {
            {"id", websdr_id},
            {"name", websdr_name},
            {"antenna", antenna},
            {"bandwidth", bandwidth.value_or(30000000)},
            {"users", user_count},
            {"center_frequency", center_frequency.value_or(15000000)},
            {"grid_locator", grid_locator},
            {"hostname", hostname},
            {"max_users", max_users.value_or(100)},
            {"port", port}
        };

        std::string serialized_json = glz::write_json(json_data);

       
        if(curl) {
            // Set the URL for the POST request
            curl_easy_setopt(curl, CURLOPT_URL, "https://sdr-list.xyz/api/update_websdr");


            // Dont print to stdout - This is the only way to do it sadly...
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, devnull);


            // Set the JSON data
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, serialized_json.c_str());

            // Disable verbose output
            curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);

            // Set the Content-Type header
            struct curl_slist *headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, "Host: sdr-list.xyz"); // Add Host header
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            // Set the Content-Length header
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, serialized_json.length());

            // Perform the request
            res = curl_easy_perform(curl);

            // Check for errors
            if(res != CURLE_OK) {
                LOG_ERROR("SDRList", "Registration failed: " + std::string(curl_easy_strerror(res)));
                retry_count++;
                
                // Exponential backoff
                int sleep_time = std::min(backoff_seconds * (1 << retry_count), max_backoff);
                LOG_INFO("SDRList", "Retrying in " + std::to_string(sleep_time) + " seconds (attempt " + std::to_string(retry_count) + ")");
                
                curl_slist_free_all(headers);
                std::this_thread::sleep_for(std::chrono::seconds(sleep_time));
                continue;
            } else {
                // Success - reset retry count
                if (retry_count > 0) {
                    LOG_INFO("SDRList", "Registration successful after " + std::to_string(retry_count) + " retries");
                }
                retry_count = 0;
                backoff_seconds = 30; // Reset backoff
                
                LOG_DEBUG("SDRList", "Updated SDR list - Users: " + std::to_string(user_count));
            }

            curl_slist_free_all(headers);
        }

        // Regular update interval (60 seconds)
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
               // Clean up

            curl_easy_cleanup(curl);
            fclose(devnull);
}


void broadcast_server::stop() {
    LOG_INFO("Server", "Shutting down server");
    running = false;
    marker_update_running = false;
    fft_processed.notify_all();

    if (marker_update_thread.joinable()) {
        marker_update_thread.join();
    }

    m_server.stop_listening();
    for (auto &[slice, data] : signal_slices) {
        websocketpp::lib::error_code ec;
        try {
            m_server.close(data->hdl, websocketpp::close::status::going_away,
                           "", ec);
        } catch (...) {
        }
    }
    for (auto &waterfall_slice : waterfall_slices) {
        for (auto &[slice, data] : waterfall_slice) {
            websocketpp::lib::error_code ec;
            try {
                m_server.close(data->hdl,
                               websocketpp::close::status::going_away, "", ec);
            } catch (...) {
            }
        }
    }
    event_con_list connections_copy;
    {
        std::scoped_lock lock(events_connections_mtx);
        connections_copy = events_connections;
    }
    for (auto &it : connections_copy) {
        websocketpp::lib::error_code ec;
        try {
            m_server.close(it, websocketpp::close::status::going_away, "", ec);
        } catch (...) {
        }
    }
}



broadcast_server *g_signal;

int main(int argc, char **argv) {
    // Parse the options
    std::string config_file = "config.toml";
    std::string log_file = "";
    bool debug_mode = false;
    
    for (int i = 1; i < argc; i++) {
        if ((std::string(argv[i]) == "-c" ||
             std::string(argv[i]) == "--config") &&
            i + 1 < argc) {
            config_file = argv[i + 1];
            i++;
        }
        else if (std::string(argv[i]) == "-l" ||
                 std::string(argv[i]) == "--log") {
            if (i + 1 < argc) {
                log_file = argv[i + 1];
                i++;
            } else {
                std::cerr << "Error: -l/--log option requires a filename argument" << std::endl;
                return 1;
            }
        }
        else if (std::string(argv[i]) == "-d" || std::string(argv[i]) == "--debug") {
            debug_mode = true;
        }
        else if (std::string(argv[i]) == "-h" || std::string(argv[i]) == "--help") {
            std::cout
                << "PhantomSDR+ v" << PHANTOMSDR_VERSION << " - Open Source WebSDR\n\n"
                << "Options:\n"
                   "  -h, --help                          Show this help message\n"
                   "  -c, --config <file>                 Config file (default: config.toml)\n"
                   "  -l, --log <file>                    Log to file\n"
                   "  -d, --debug                         Enable debug logging\n";
            return 0;
        }
    }
    
    // Initialize logger
    Logger& logger = Logger::getInstance();
    if (!log_file.empty()) {
        if (!logger.set_log_file(log_file)) {
            std::cerr << "Error: Cannot open log file '" << log_file << "' for writing" << std::endl;
            return 1;
        }
    }
    if (debug_mode) {
        logger.set_min_level(LogLevel::DEBUG);
    }

    // ANSI color codes for banner
    const std::string RESET = "\033[0m";
    const std::string BOLD = "\033[1m";
    const std::string CYAN = "\033[36m";
    const std::string GREEN = "\033[32m";
    const std::string YELLOW = "\033[33m";
    const std::string MAGENTA = "\033[35m";
    const std::string BLUE = "\033[34m";
    const std::string RED = "\033[31m";

    // Create banner with proper alignment
    std::string line1 = std::string("       PhantomSDR+ v") + PHANTOMSDR_VERSION + "         ";
    std::string line2 = "     Open Source WebSDR System       ";
    
    // Ensure both lines are exactly 41 chars (box width - 2 for borders)
    line1.resize(41, ' ');
    line2.resize(41, ' ');
    
    logger.banner("");
    logger.banner(CYAN + "┌─────────────────────────────────────────┐" + RESET);
    logger.banner(CYAN + "│" + RESET + BOLD + YELLOW + line1 + RESET + CYAN + "│" + RESET);
    logger.banner(CYAN + "│" + RESET + GREEN + line2 + RESET + CYAN + "│" + RESET);
    logger.banner(CYAN + "└─────────────────────────────────────────┘" + RESET);
    
    logger.banner(BLUE + " ◆ " + RESET + "AGC Speed " + BLUE + "◆ " + RESET + "Buffer Control " + BLUE + "◆ " + RESET + "Keybinds " + BLUE + "◆ " + RESET + "Server Info");
    logger.banner(BLUE + " ◆ " + RESET + "Freq Lookup " + BLUE + "◆ " + RESET + "Callsigns " + BLUE + "◆ " + RESET + "Audio Stats " + BLUE + "◆ " + RESET + "Logging\n");

    LOG_INFO("Main", "Loading configuration from: " + config_file);
    try {
        config = toml::parse_file(config_file);
    } catch (const toml::parse_error& err) {
        LOG_CRITICAL("Main", "Failed to parse config file: " + std::string(err.what()));
        return 1;
    }

    std::string host = config["server"]["host"].value_or("0.0.0.0");

    std::optional<std::string> driver_type =
        config["input"]["driver"]["name"].value<std::string>();
    if (!driver_type.has_value()) {
        LOG_CRITICAL("Main", "No input driver specified in config");
        return 1;
    }
    std::string driver_str = driver_type.value();

    std::string input_format =
        config["input"]["driver"]["format"].value_or("f32");
    boost::algorithm::to_lower(input_format);

    // Initialise FFT threads if requested for multithreaded
    int fft_threads = config["input"]["fft_threads"].value_or(1);
    if (fft_threads > 1) {
        fftwf_init_threads();
    }

    // Create sample reader based on driver type
    std::unique_ptr<SampleReader> reader;
    
    if (driver_str == "stdin") {
        // Set input to binary
        freopen(NULL, "rb", stdin);
        reader = std::make_unique<FileSampleReader>(stdin);
    }
    else {
        LOG_CRITICAL("Main", "Unknown driver: " + driver_str);
        return 1;
    }
    
    std::unique_ptr<SampleConverterBase> driver;

    if (input_format == "u8") {
        driver = std::make_unique<SampleConverter<uint8_t>>(std::move(reader));
    } else if (input_format == "s8") {
        driver = std::make_unique<SampleConverter<int8_t>>(std::move(reader));
    } else if (input_format == "u16") {
        driver = std::make_unique<SampleConverter<uint16_t>>(std::move(reader));
    } else if (input_format == "s16" || input_format == "cs16") {
        // Both s16 and cs16 use int16_t converter
        driver = std::make_unique<SampleConverter<int16_t>>(std::move(reader));
    } else if (input_format == "f32" || input_format == "cf32") {
        // Both f32 and cf32 use float converter
        driver = std::make_unique<SampleConverter<float>>(std::move(reader));
    } else if (input_format == "f64") {
        driver = std::make_unique<SampleConverter<double>>(std::move(reader));
    } else {
        std::cout << "Unknown input format: " << input_format << std::endl;
        return 1;
    }


    int port = config["server"]["port"].value_or(9002);
    bool register_online = config["websdr"]["register_online"].value_or(false);
    
    // Display configured server information in a compact format
    std::string chat_status = config["websdr"]["chat_enabled"].value_or(true) ? "ON" : "OFF";
    std::string reg_status = register_online ? "YES" : "NO";
    
    logger.banner("\n" + GREEN + "[CONFIG] " + RESET + 
                  "Port:" + std::to_string(port) + 
                  " | " + config["websdr"]["name"].value_or("NoName") + 
                  " @ " + config["websdr"]["grid_locator"].value_or("NoLoc") + 
                  " | Chat:" + chat_status + 
                  " | Register:" + reg_status);
    
    try {
        broadcast_server server(std::move(driver), config);

        // Always start the SDR list thread - it will check the config internally
        std::thread websdr_thread(&broadcast_server::update_websdr_list, &server);
        websdr_thread.detach();
        
        if(register_online) {
            LOG_INFO("Main", "SDR list registration enabled");
        } else {
            LOG_INFO("Main", "SDR list registration disabled - set register_online=true to enable");
        }
        g_signal = &server;
        std::signal(SIGINT, [](int) { g_signal->stop(); });
        
        logger.banner(CYAN + "\n→ Starting on http://localhost:" + std::to_string(port) + RESET);
        
        server.run(port);
    } catch (const std::bad_alloc& e) {
        LOG_CRITICAL("Main", "Out of memory: " + std::string(e.what()));
        LOG_CRITICAL("Main", "Try reducing fft_size in config.toml");
        return 1;
    } catch (const std::runtime_error& e) {
        LOG_CRITICAL("Main", "Runtime error: " + std::string(e.what()));
        return 1;
    } catch (const std::exception& e) {
        LOG_CRITICAL("Main", "Unexpected error: " + std::string(e.what()));
        return 1;
    } catch (...) {
        LOG_CRITICAL("Main", "Unknown error occurred");
        return 1;
    }
    
    std::exit(0);
}
