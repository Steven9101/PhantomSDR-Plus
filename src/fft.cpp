#include "fft.h"
#include "spectrumserver.h"
#include "utils.h"
#include "logger.h"

#include <numeric>

#include <fftw3.h>

// Main FFT loop to process input samples
void broadcast_server::fft_task() {
    try {
        // Attempt to import FFTW wisdom
        if (!fftwf_import_wisdom_from_filename("fftw_wisdom")) {
            LOG_INFO("FFT", "FFTW wisdom not found - first run will be slower");
        } else {
            LOG_DEBUG("FFT", "FFTW wisdom loaded");
        }

        // This is the buffer where it converts to a float

        std::unique_ptr<FFT> fft = std::move(this->fft);

        // Twice as many floats if it is complex
        float *input_buffers[3] = {nullptr, nullptr, nullptr};
        int input_buffer_size = fft_size / 2 * (2 - is_real);
        int input_buffer_idx = 0;
        
        try {
            input_buffers[0] = fft->malloc(input_buffer_size);
            input_buffers[1] = fft->malloc(input_buffer_size);
            input_buffers[2] = fft->malloc(input_buffer_size);
        } catch (const std::exception& e) {
            LOG_ERROR("FFT", "Failed to allocate input buffers: " + std::string(e.what()));
            if (input_buffers[0]) fft->free(input_buffers[0]);
            if (input_buffers[1]) fft->free(input_buffers[1]);
            running = false;
            return;
        }

        // FFT planning
        try {
            if (is_real) {
                fft->plan_r2c(FFTW_MEASURE | FFTW_DESTROY_INPUT);
            } else {
                fft->plan_c2c(FFT::FORWARD, FFTW_MEASURE | FFTW_DESTROY_INPUT);
            }
        } catch (const std::exception& e) {
            LOG_ERROR("FFT", "FFT planning failed: " + std::string(e.what()));
            fft->free(input_buffers[0]);
            fft->free(input_buffers[1]);
            fft->free(input_buffers[2]);
            running = false;
            return;
        }
    
    // Export FFTW wisdom after planning
    if (!fftwf_export_wisdom_to_filename("fftw_wisdom")) {
        std::cout << "Failed to export FFTW wisdom." << std::endl;
    }

    fft_buffer = reinterpret_cast<std::complex<float>*>(fft->get_output_buffer());

    // Target fps is 10, *2 since 50% overlap -- reduced to 5 for test
    int skip_num = std::max(1, (int)floor(((float)sps / fft_size) / 10.) * 2);
    LOG_INFO("FFT", "Waterfall rate: 1 frame per " + std::to_string(skip_num) + " FFTs");

    MovingAverage<double> sps_measured(60);
    auto prev_data = std::chrono::steady_clock::now();

    auto signal_loop_fn = std::bind(&broadcast_server::signal_loop, this);
    auto waterfall_loop_fn = std::bind(&broadcast_server::waterfall_loop, this,
                                       fft->get_quantized_buffer());

    std::future<void> buffer_read = std::async(std::launch::async, [] {});
    std::vector<std::future<void>> signal_futures;
    std::vector<std::future<void>> waterfall_futures;
    std::future<void> fft_future = std::async(std::launch::async, [] {});

        while (running) {
            try {
                // Read, convert and scale the input
                // 50% overlap is hardcoded for favourable downconverter properties
                buffer_read.wait();
                float *buf0 = input_buffers[input_buffer_idx];
                float *buf1 = input_buffers[(input_buffer_idx + 1) % 3];
                float *buf2 = input_buffers[(input_buffer_idx + 2) % 3];
        if (is_real) {
            // Read into buf2 asynchronously
            buffer_read = std::async(std::launch::async,
                                     [buf2, fft_size = fft_size, this] {
                                         reader->read(buf2, fft_size / 2);
                                     });

            fft->load_real_input(buf0, buf1);
        } else {
            // IQ data has twice as many floats
            buffer_read = std::async(std::launch::async,
                                     [buf2, fft_size = fft_size, this] {
                                         reader->read(buf2, fft_size);
                                     });
            fft->load_complex_input(buf0, buf1);
        }

        input_buffer_idx = (input_buffer_idx + 1) % 3;
        // If no users skip the FFT
        if (signal_slices.size() + std::accumulate(waterfall_slices.begin(),
                                                   waterfall_slices.end(), 0,
                                                   [](int val, auto &l) {
                                                       return val + l.size();
                                                   }) ==
            0) {
            continue;
        }

        // Wait for previous FFT to complete before starting a new one
        // This prevents buffer corruption when the same buffers are reused
        fft_future.wait();
        
        // Wait for all the signal and waterfall clients to finish
        for (auto &f : signal_futures) {
            f.wait();
        }
        for (auto &f : waterfall_futures) {
            f.wait();
        }

                fft->execute();
                if (!is_real) {

                    // If the user requested a range near the 0 frequency,
                    // the data will wrap around, copy the front to the back to make
                    // it contiguous
                    memcpy(&fft_buffer[fft_result_size], &fft_buffer[0],
                           sizeof(fftwf_complex) * audio_max_fft_size);
                }

                // Enqueue tasks once the fft is ready
                signal_futures = signal_loop_fn();
                if (frame_num % skip_num == 0) {
                    waterfall_futures = waterfall_loop_fn();
                }
                frame_num++;

                /*auto cur_data = std::chrono::steady_clock::now();
                std::chrono::duration<double> diff_time = cur_data - prev_data;
                sps_measured.insert(diff_time.count());
                prev_data = cur_data;
                if (frame_num % 10 == 0) {
                    // std::cout<<"SPS: "<<std::fixed<<(double)(fft_size / 2) /
                    // sps_measured.getAverage()<<std::endl;
                }*/
            } catch (const std::bad_alloc& e) {
                LOG_ERROR("FFT", "Out of memory in FFT processing loop");
                running = false;
                break;
            } catch (const std::exception& e) {
                LOG_ERROR("FFT", "FFT processing error: " + std::string(e.what()));
                // Try to continue if possible
            }
        }
        fft->free(input_buffers[0]);
        fft->free(input_buffers[1]);
        fft->free(input_buffers[2]);
    } catch (const std::exception& e) {
        LOG_ERROR("FFT", "Fatal error in FFT task: " + std::string(e.what()));
        running = false;
    }
}
