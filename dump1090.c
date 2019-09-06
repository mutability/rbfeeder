// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// dump1090.c: main program & miscellany
//
// Copyright (c) 2014-2016 Oliver Jowett <oliver@mutability.co.uk>
//
// This file is free software: you may copy, redistribute and/or modify it  
// under the terms of the GNU General Public License as published by the
// Free Software Foundation, either version 2 of the License, or (at your  
// option) any later version.  
//
// This file is distributed in the hope that it will be useful, but  
// WITHOUT ANY WARRANTY; without even the implied warranty of  
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU  
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License  
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

// This file incorporates work covered by the following copyright and  
// permission notice:
//
//   Copyright (C) 2012 by Salvatore Sanfilippo <antirez@gmail.com>
//
//   All rights reserved.
//
//   Redistribution and use in source and binary forms, with or without
//   modification, are permitted provided that the following conditions are
//   met:
//
//    *  Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//    *  Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
//   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
//   HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
//   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
//   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
//   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
//   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "dump1090.h"

#include <rtl-sdr.h>

#include <stdarg.h>
#include "airnav.h"

static int verbose_device_search(char *s);

//
// ============================= Utility functions ==========================
//

static void log_with_timestamp(const char *format, ...) __attribute__ ((format(printf, 1, 2)));

static void log_with_timestamp(const char *format, ...) {
    char timebuf[128];
    char msg[1024];
    time_t now;
    struct tm local;
    va_list ap;

    now = time(NULL);
    localtime_r(&now, &local);
    strftime(timebuf, 128, "%c %Z", &local);
    timebuf[127] = 0;

    va_start(ap, format);
    vsnprintf(msg, 1024, format, ap);
    va_end(ap);
    msg[1023] = 0;

    fprintf(stderr, "%s  %s\n", timebuf, msg);
}

static void sigintHandler(int dummy) {
    MODES_NOTUSED(dummy);
    signal(SIGINT, SIG_DFL); // reset signal handler - bit extra safety
    Modes.exit = 1; // Signal to threads that we are done
    log_with_timestamp("Caught SIGINT, shutting down..\n");
    #ifdef RBCSRBLC
    led_off(LED_ADSB);
        led_off(LED_STATUS);
        led_off(LED_GPS);
        led_off(LED_PC);
        led_off(LED_ERROR);
        led_off(LED_VHF);
    #endif
}

static void sigtermHandler(int dummy) {
    MODES_NOTUSED(dummy);
    signal(SIGTERM, SIG_DFL); // reset signal handler - bit extra safety
    Modes.exit = 1; // Signal to threads that we are done
    log_with_timestamp("Caught SIGTERM, shutting down..\n");
    #ifdef RBCSRBLC
    led_off(LED_ADSB);
        led_off(LED_STATUS);
        led_off(LED_GPS);
        led_off(LED_PC);
        led_off(LED_ERROR);
        led_off(LED_VHF);
    #endif
}
//
// =============================== Terminal handling ========================
//
#ifndef _WIN32
// Get the number of rows after the terminal changes size.

int getTermRows() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return (w.ws_row);
}

// Handle resizing terminal

void sigWinchCallback() {
    signal(SIGWINCH, SIG_IGN);
    Modes.interactive_rows = getTermRows();
    interactiveShowData();
    signal(SIGWINCH, sigWinchCallback);
}
#else 

int getTermRows() {
    return MODES_INTERACTIVE_ROWS;
}
#endif

static void start_cpu_timing(struct timespec *start_time) {
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, start_time);
}

static void end_cpu_timing(const struct timespec *start_time, struct timespec *add_to) {
    struct timespec end_time;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end_time);
    add_to->tv_sec += (end_time.tv_sec - start_time->tv_sec - 1);
    add_to->tv_nsec += (1000000000L + end_time.tv_nsec - start_time->tv_nsec);
    add_to->tv_sec += add_to->tv_nsec / 1000000000L;
    add_to->tv_nsec = add_to->tv_nsec % 1000000000L;
}

//
// =============================== Initialization ===========================
//

void modesInitConfig(void) {
    // Default everything to zero/NULL
    memset(&Modes, 0, sizeof (Modes));

    // Now initialise things that should not be 0/NULL to their defaults
    Modes.gain = MODES_MAX_GAIN;
    Modes.freq = MODES_DEFAULT_FREQ;
    Modes.ppm_error = MODES_DEFAULT_PPM;
    Modes.check_crc = 1;
    Modes.net_heartbeat_interval = MODES_NET_HEARTBEAT_INTERVAL;
    //Modes.net_input_raw_ports     = strdup("30001");
    //Modes.net_output_raw_ports    = strdup("30002");
    //Modes.net_output_sbs_ports    = strdup("30003");
    //Modes.net_input_beast_ports   = strdup("30004,30104");
    //Modes.net_output_beast_ports  = strdup("30005");
#ifdef ENABLE_WEBSERVER
#ifdef RBCSRBLC
    //Modes.net_http_ports          = strdup("32084");
#endif  
#endif
    Modes.interactive_rows = getTermRows();
    Modes.interactive_display_ttl = MODES_INTERACTIVE_DISPLAY_TTL;
    Modes.html_dir = HTMLPATH;
    Modes.json_interval = 1000;
    Modes.json_location_accuracy = 1;
    Modes.maxRange = 1852 * 300; // 300NM default max range
}
//
//=========================================================================
//

void modesInit(void) {
    int i, q;

    pthread_mutex_init(&Modes.data_mutex, NULL);
    pthread_cond_init(&Modes.data_cond, NULL);

    Modes.sample_rate = 2400000.0;

    // Allocate the various buffers used by Modes
    Modes.trailing_samples = (MODES_PREAMBLE_US + MODES_LONG_MSG_BITS + 16) * 1e-6 * Modes.sample_rate;

    if (((Modes.maglut = (uint16_t *) malloc(sizeof (uint16_t) * 256 * 256)) == NULL) ||
            ((Modes.log10lut = (uint16_t *) malloc(sizeof (uint16_t) * 256 * 256)) == NULL)) {
        fprintf(stderr, "Out of memory allocating data buffer.\n");
        exit(1);
    }

    for (i = 0; i < MODES_MAG_BUFFERS; ++i) {
        if ((Modes.mag_buffers[i].data = calloc(MODES_MAG_BUF_SAMPLES + Modes.trailing_samples, sizeof (uint16_t))) == NULL) {
            fprintf(stderr, "Out of memory allocating magnitude buffer.\n");
            exit(1);
        }

        Modes.mag_buffers[i].length = 0;
        Modes.mag_buffers[i].dropped = 0;
        Modes.mag_buffers[i].sampleTimestamp = 0;
    }

    // Validate the users Lat/Lon home location inputs
    if ((Modes.fUserLat > 90.0) // Latitude must be -90 to +90
            || (Modes.fUserLat < -90.0) // and 
            || (Modes.fUserLon > 360.0) // Longitude must be -180 to +360
            || (Modes.fUserLon < -180.0)) {
        Modes.fUserLat = Modes.fUserLon = 0.0;
    } else if (Modes.fUserLon > 180.0) { // If Longitude is +180 to +360, make it -180 to 0
        Modes.fUserLon -= 360.0;
    }
    // If both Lat and Lon are 0.0 then the users location is either invalid/not-set, or (s)he's in the 
    // Atlantic ocean off the west coast of Africa. This is unlikely to be correct. 
    // Set the user LatLon valid flag only if either Lat or Lon are non zero. Note the Greenwich meridian 
    // is at 0.0 Lon,so we must check for either fLat or fLon being non zero not both. 
    // Testing the flag at runtime will be much quicker than ((fLon != 0.0) || (fLat != 0.0))
    Modes.bUserFlags &= ~MODES_USER_LATLON_VALID;
    if ((Modes.fUserLat != 0.0) || (Modes.fUserLon != 0.0)) {
        Modes.bUserFlags |= MODES_USER_LATLON_VALID;
    }

    // Limit the maximum requested raw output size to less than one Ethernet Block 
    if (Modes.net_output_flush_size > (MODES_OUT_FLUSH_SIZE)) {
        Modes.net_output_flush_size = MODES_OUT_FLUSH_SIZE;
    }
    if (Modes.net_output_flush_interval > (MODES_OUT_FLUSH_INTERVAL)) {
        Modes.net_output_flush_interval = MODES_OUT_FLUSH_INTERVAL;
    }
    if (Modes.net_sndbuf_size > (MODES_NET_SNDBUF_MAX)) {
        Modes.net_sndbuf_size = MODES_NET_SNDBUF_MAX;
    }

    // compute UC8 magnitude lookup table
    for (i = 0; i <= 255; i++) {
        for (q = 0; q <= 255; q++) {
            float fI, fQ, magsq;

            fI = (i - 127.5) / 127.5;
            fQ = (q - 127.5) / 127.5;
            magsq = fI * fI + fQ * fQ;
            if (magsq > 1)
                magsq = 1;

            Modes.maglut[le16toh((i * 256) + q)] = (uint16_t) round(sqrtf(magsq) * 65535.0);
        }
    }

    // Prepare the log10 lookup table: 100log10(x)
    Modes.log10lut[0] = 0; // poorly defined..
    for (i = 1; i <= 65535; i++) {
        Modes.log10lut[i] = (uint16_t) round(100.0 * log10(i));
    }

    // Prepare error correction tables
    modesChecksumInit(Modes.nfix_crc);
    icaoFilterInit();

    if (Modes.show_only)
        icaoFilterAdd(Modes.show_only);

    // Prepare sample conversion
    if (!Modes.net_only) {
        if (Modes.filename == NULL) // using a real RTLSDR, use UC8 input always
            Modes.input_format = INPUT_UC8;

        Modes.converter_function = init_converter(Modes.input_format,
                Modes.sample_rate,
                Modes.dc_filter,
                &Modes.converter_state);
        if (!Modes.converter_function) {
            fprintf(stderr, "Can't initialize sample converter, giving up.\n");
            exit(1);
        }
    }
}

static void convert_samples(void *iq,
        uint16_t *mag,
        unsigned nsamples,
        double *power) {
    Modes.converter_function(iq, mag, nsamples, Modes.converter_state, power);
}

//
// =============================== RTLSDR handling ==========================
//

int modesInitRTLSDR(void) {
    int j;
    int device_count, dev_index = 0;
    char vendor[256], product[256], serial[256];

    if (Modes.dev_name) {
        if ((dev_index = verbose_device_search(Modes.dev_name)) < 0)
            return -1;
    }

    device_count = rtlsdr_get_device_count();
    if (!device_count) {
        airnav_log("No supported RTLSDR devices found.\n");
        return -1;
    }

    airnav_log("Found %d device(s):\n", device_count);
    for (j = 0; j < device_count; j++) {
        if (rtlsdr_get_device_usb_strings(j, vendor, product, serial) != 0) {
            airnav_log("%d: unable to read device details\n", j);
        } else {
            airnav_log("%d: %s, %s, SN: %s %s\n", j, vendor, product, serial,
                    (j == dev_index) ? "(currently selected)" : "");
        }
    }

    if (rtlsdr_open(&Modes.dev, dev_index) < 0) {
        airnav_log("Error opening the RTLSDR device: %s\n",
                strerror(errno));
        return -1;
    }

    int alternative_init = 0;
    alternative_init = ini_getBoolean("/etc/rbfeeder.ini", "client", "alternative_rtl_init", strcmp(F_ARCH, "rblc") == 0 ? 1 : 0);



    // RBLC/ Dongle Diferente
    if (alternative_init == 1) {
        airnav_log("Using alterative dongle init procedure!\n");
        unsigned char data[2] = {0x18, 0x18};
        int r = -1;
        r = libusb_control_transfer(((struct libusb_device_handle **) (Modes.dev))[1], (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT), 0, 288, 17, data, 1, 300);
        if (r < 0) {
            fprintf(stderr, "Failed to enable repeater");
        }
        data[0] = 0x05;
        data[1] = 0x83; // or 0x9F
        r = libusb_control_transfer(((struct libusb_device_handle **) (Modes.dev))[1], (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT), 0, 52, 1552, data, 2, 300);
        if (r < 0) {
            fprintf(stderr, "Failed to write reg");
        }
        data[0] = 0x1F;
        data[1] = 0x00;
        r = libusb_control_transfer(((struct libusb_device_handle **) (Modes.dev))[1], (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT), 0, 52, 1552, data, 2, 300);
        if (r < 0) {
            fprintf(stderr, "Failed to write reg 2");
        }
        data[0] = 0x10;
        data[1] = 0x10;
        r = libusb_control_transfer(((struct libusb_device_handle **) (Modes.dev))[1], (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT), 0, 288, 17, data, 1, 300);
        if (r < 0) {
            fprintf(stderr, "Failed to disable repeater");
        }
    }



    // Set gain, frequency, sample rate, and reset the device
    rtlsdr_set_tuner_gain_mode(Modes.dev,
            (Modes.gain == MODES_AUTO_GAIN) ? 0 : 1);
    if (Modes.gain != MODES_AUTO_GAIN) {
        int *gains;
        int numgains;

        numgains = rtlsdr_get_tuner_gains(Modes.dev, NULL);
        if (numgains <= 0) {
            airnav_log("Error getting tuner gains\n");
            return -1;
        }

        gains = malloc(numgains * sizeof (int));
        if (rtlsdr_get_tuner_gains(Modes.dev, gains) != numgains) {
            airnav_log("Error getting tuner gains\n");
            free(gains);
            return -1;
        }

        if (Modes.gain == MODES_MAX_GAIN) {
            int highest = -1;
            int i;

            for (i = 0; i < numgains; ++i) {
                if (gains[i] > highest)
                    highest = gains[i];
            }

            Modes.gain = highest;
            airnav_log("Max available gain is: %.2f dB\n", Modes.gain / 10.0);
        } else {
            int closest = -1;
            int i;

            for (i = 0; i < numgains; ++i) {
                if (closest == -1 || abs(gains[i] - Modes.gain) < abs(closest - Modes.gain))
                    closest = gains[i];
            }

            if (closest != Modes.gain) {
                Modes.gain = closest;
                airnav_log("Closest available gain: %.2f dB\n", Modes.gain / 10.0);
            }
        }

        free(gains);

        airnav_log("Setting gain to: %.2f dB\n", Modes.gain / 10.0);
        if (rtlsdr_set_tuner_gain(Modes.dev, Modes.gain) < 0) {
            airnav_log("Error setting tuner gains\n");
            return -1;
        }
    } else {
        airnav_log("Using automatic gain control.\n");
    }
    rtlsdr_set_freq_correction(Modes.dev, Modes.ppm_error);
    if (Modes.enable_agc) rtlsdr_set_agc_mode(Modes.dev, 1);
    rtlsdr_set_center_freq(Modes.dev, Modes.freq);
    rtlsdr_set_sample_rate(Modes.dev, (unsigned) Modes.sample_rate);

    rtlsdr_reset_buffer(Modes.dev);
    airnav_log("Gain reported by device: %.2f dB\n",
            rtlsdr_get_tuner_gain(Modes.dev) / 10.0);

#ifdef HAVE_RTL_BIAST
    if (Modes.enable_rtlsdr_biast) {
        rtlsdr_set_bias_tee(Modes.dev, 1);
    } else {
        rtlsdr_set_bias_tee(Modes.dev, 0);
    }
#endif

    return 0;
}
//
//=========================================================================
//
// We use a thread reading data in background, while the main thread
// handles decoding and visualization of data to the user.
//
// The reading thread calls the RTLSDR API to read data asynchronously, and
// uses a callback to populate the data buffer.
//
// A Mutex is used to avoid races with the decoding thread.
//

static struct timespec reader_thread_start;

void rtlsdrCallback(unsigned char *buf, uint32_t len, void *ctx) {
    struct mag_buf *outbuf;
    struct mag_buf *lastbuf;
    uint32_t slen;
    unsigned next_free_buffer;
    unsigned free_bufs;
    unsigned block_duration;

    static int was_odd = 0; // paranoia!!
    static int dropping = 0;

    MODES_NOTUSED(ctx);

    // Lock the data buffer variables before accessing them
    pthread_mutex_lock(&Modes.data_mutex);
    if (Modes.exit) {
        rtlsdr_cancel_async(Modes.dev); // ask our caller to exit
    }

    next_free_buffer = (Modes.first_free_buffer + 1) % MODES_MAG_BUFFERS;
    outbuf = &Modes.mag_buffers[Modes.first_free_buffer];
    lastbuf = &Modes.mag_buffers[(Modes.first_free_buffer + MODES_MAG_BUFFERS - 1) % MODES_MAG_BUFFERS];
    free_bufs = (Modes.first_filled_buffer - next_free_buffer + MODES_MAG_BUFFERS) % MODES_MAG_BUFFERS;

    // Paranoia! Unlikely, but let's go for belt and suspenders here

    if (len != MODES_RTL_BUF_SIZE) {
        airnav_log("weirdness: rtlsdr gave us a block with an unusual size (got %u bytes, expected %u bytes)\n",
                (unsigned) len, (unsigned) MODES_RTL_BUF_SIZE);

        if (len > MODES_RTL_BUF_SIZE) {
            // wat?! Discard the start.
            unsigned discard = (len - MODES_RTL_BUF_SIZE + 1) / 2;
            outbuf->dropped += discard;
            buf += discard * 2;
            len -= discard * 2;
        }
    }

    if (was_odd) {
        // Drop a sample so we are in sync with I/Q samples again (hopefully)
        ++buf;
        --len;
        ++outbuf->dropped;
    }

    was_odd = (len & 1);
    slen = len / 2;

    if (free_bufs == 0 || (dropping && free_bufs < MODES_MAG_BUFFERS / 2)) {
        // FIFO is full. Drop this block.
        dropping = 1;
        outbuf->dropped += slen;
        pthread_mutex_unlock(&Modes.data_mutex);
        return;
    }

    dropping = 0;
    pthread_mutex_unlock(&Modes.data_mutex);

    // Compute the sample timestamp and system timestamp for the start of the block
    outbuf->sampleTimestamp = lastbuf->sampleTimestamp + 12e6 * (lastbuf->length + outbuf->dropped) / Modes.sample_rate;
    block_duration = 1e9 * slen / Modes.sample_rate;

    // Get the approx system time for the start of this block
    clock_gettime(CLOCK_REALTIME, &outbuf->sysTimestamp);
    outbuf->sysTimestamp.tv_nsec -= block_duration;
    normalize_timespec(&outbuf->sysTimestamp);

    // Copy trailing data from last block (or reset if not valid)
    if (outbuf->dropped == 0 && lastbuf->length >= Modes.trailing_samples) {
        memcpy(outbuf->data, lastbuf->data + lastbuf->length - Modes.trailing_samples, Modes.trailing_samples * sizeof (uint16_t));
    } else {
        memset(outbuf->data, 0, Modes.trailing_samples * sizeof (uint16_t));
    }

    // Convert the new data
    outbuf->length = slen;
    convert_samples(buf, &outbuf->data[Modes.trailing_samples], slen, &outbuf->total_power);

    // Push the new data to the demodulation thread
    pthread_mutex_lock(&Modes.data_mutex);

    Modes.mag_buffers[next_free_buffer].dropped = 0;
    Modes.mag_buffers[next_free_buffer].length = 0; // just in case
    Modes.first_free_buffer = next_free_buffer;

    // accumulate CPU while holding the mutex, and restart measurement
    end_cpu_timing(&reader_thread_start, &Modes.reader_cpu_accumulator);
    start_cpu_timing(&reader_thread_start);

    pthread_cond_signal(&Modes.data_cond);
    pthread_mutex_unlock(&Modes.data_mutex);
}
//
//=========================================================================
//
// This is used when --ifile is specified in order to read data from file
// instead of using an RTLSDR device
//

void readDataFromFile(void) {
    int eof = 0;
    struct timespec next_buffer_delivery;
    void *readbuf;
    int bytes_per_sample = 0;

    switch (Modes.input_format) {
        case INPUT_UC8:
            bytes_per_sample = 2;
            break;
        case INPUT_SC16:
        case INPUT_SC16Q11:
            bytes_per_sample = 4;
            break;
    }

    if (!(readbuf = malloc(MODES_MAG_BUF_SAMPLES * bytes_per_sample))) {
        fprintf(stderr, "failed to allocate read buffer\n");
        exit(1);
    }

    clock_gettime(CLOCK_MONOTONIC, &next_buffer_delivery);

    pthread_mutex_lock(&Modes.data_mutex);
    while (!Modes.exit && !eof) {
        ssize_t nread, toread;
        void *r;
        struct mag_buf *outbuf, *lastbuf;
        unsigned next_free_buffer;
        unsigned slen;

        next_free_buffer = (Modes.first_free_buffer + 1) % MODES_MAG_BUFFERS;
        if (next_free_buffer == Modes.first_filled_buffer) {
            // no space for output yet
            pthread_cond_wait(&Modes.data_cond, &Modes.data_mutex);
            continue;
        }

        outbuf = &Modes.mag_buffers[Modes.first_free_buffer];
        lastbuf = &Modes.mag_buffers[(Modes.first_free_buffer + MODES_MAG_BUFFERS - 1) % MODES_MAG_BUFFERS];
        pthread_mutex_unlock(&Modes.data_mutex);

        // Compute the sample timestamp and system timestamp for the start of the block
        outbuf->sampleTimestamp = lastbuf->sampleTimestamp + 12e6 * lastbuf->length / Modes.sample_rate;

        // Copy trailing data from last block (or reset if not valid)
        if (lastbuf->length >= Modes.trailing_samples) {
            memcpy(outbuf->data, lastbuf->data + lastbuf->length - Modes.trailing_samples, Modes.trailing_samples * sizeof (uint16_t));
        } else {
            memset(outbuf->data, 0, Modes.trailing_samples * sizeof (uint16_t));
        }

        // Get the system time for the start of this block
        clock_gettime(CLOCK_REALTIME, &outbuf->sysTimestamp);

        toread = MODES_MAG_BUF_SAMPLES * bytes_per_sample;
        r = readbuf;
        while (toread) {
            nread = read(Modes.fd, r, toread);
            if (nread <= 0) {
                // Done.
                eof = 1;
                break;
            }
            r += nread;
            toread -= nread;
        }

        slen = outbuf->length = MODES_MAG_BUF_SAMPLES - toread / bytes_per_sample;

        // Convert the new data
        convert_samples(readbuf, &outbuf->data[Modes.trailing_samples], slen, &outbuf->total_power);

        if (Modes.throttle) {
            // Wait until we are allowed to release this buffer to the main thread
            while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_buffer_delivery, NULL) == EINTR)
                ;

            // compute the time we can deliver the next buffer.
            next_buffer_delivery.tv_nsec += outbuf->length * 1e9 / Modes.sample_rate;
            normalize_timespec(&next_buffer_delivery);
        }

        // Push the new data to the main thread
        pthread_mutex_lock(&Modes.data_mutex);
        Modes.first_free_buffer = next_free_buffer;
        // accumulate CPU while holding the mutex, and restart measurement
        end_cpu_timing(&reader_thread_start, &Modes.reader_cpu_accumulator);
        start_cpu_timing(&reader_thread_start);
        pthread_cond_signal(&Modes.data_cond);
    }

    free(readbuf);

    // Wait for the main thread to consume all data
    while (!Modes.exit && Modes.first_filled_buffer != Modes.first_free_buffer)
        pthread_cond_wait(&Modes.data_cond, &Modes.data_mutex);

    pthread_mutex_unlock(&Modes.data_mutex);
}
//
//=========================================================================
//
// We read data using a thread, so the main thread only handles decoding
// without caring about data acquisition
//

void *readerThreadEntryPoint(void *arg) {
    MODES_NOTUSED(arg);

    start_cpu_timing(&reader_thread_start); // we accumulate in rtlsdrCallback() or readDataFromFile()

    if (Modes.filename == NULL) {
        while (!Modes.exit) {
            rtlsdr_read_async(Modes.dev, rtlsdrCallback, NULL,
                    MODES_RTL_BUFFERS,
                    MODES_RTL_BUF_SIZE);

            if (!Modes.exit) {
                airnav_log("Warning: lost the connection to the RTLSDR device.");
                rtlsdr_close(Modes.dev);
                Modes.dev = NULL;

                do {
                    sleep(5);
                    airnav_log("Trying to reconnect to the RTLSDR device..");
                } while (!Modes.exit && modesInitRTLSDR() < 0);
            }
        }

        if (Modes.dev != NULL) {
            rtlsdr_close(Modes.dev);
            Modes.dev = NULL;
        }
    } else {
        readDataFromFile();
    }

    // Wake the main thread (if it's still waiting)
    pthread_mutex_lock(&Modes.data_mutex);
    Modes.exit = 1; // just in case
    pthread_cond_signal(&Modes.data_cond);
    pthread_mutex_unlock(&Modes.data_mutex);

#ifndef _WIN32
    pthread_exit(NULL);
#else
    return NULL;
#endif
}
//
// ============================== Snip mode =================================
//
// Get raw IQ samples and filter everything is < than the specified level
// for more than 256 samples in order to reduce example file size
//

void snipMode(int level) {
    int i, q;
    uint64_t c = 0;

    while ((i = getchar()) != EOF && (q = getchar()) != EOF) {
        if (abs(i - 127) < level && abs(q - 127) < level) {
            c++;
            if (c > MODES_PREAMBLE_SIZE) continue;
        } else {
            c = 0;
        }
        putchar(i);
        putchar(q);
    }
}
//
// ================================ Main ====================================
//

void showHelp(void) {
    printf(
            "-----------------------------------------------------------------------------\n"
            "| dump1090 ModeS Receiver     %45s |\n"
            "-----------------------------------------------------------------------------\n"
            "--device-index <index>   Select RTL device (default: 0)\n"
            "--gain <db>              Set gain (default: max gain. Use -10 for auto-gain)\n"
            "--enable-agc             Enable the Automatic Gain Control (default: off)\n"
            "--freq <hz>              Set frequency (default: 1090 Mhz)\n"
            "--ifile <filename>       Read data from file (use '-' for stdin)\n"
            "--iformat <format>       Sample format for --ifile: UC8 (default), SC16, or SC16Q11\n"
            "--throttle               When reading from a file, play back in realtime, not at max speed\n"
            "--interactive            Interactive mode refreshing data on screen. Implies --throttle\n"
            "--interactive-rows <num> Max number of rows in interactive mode (default: 22)\n"
            "--interactive-ttl <sec>  Remove from list if idle for <sec> (default: 60)\n"
            "--interactive-rtl1090    Display flight table in RTL1090 format\n"
            "--raw                    Show only messages hex values\n"
            "--net                    Enable networking\n"
            "--modeac                 Enable decoding of SSR Modes 3/A & 3/C\n"
            "--net-only               Enable just networking, no RTL device or file used\n"
            "--net-bind-address <ip>  IP address to bind to (default: Any; Use 127.0.0.1 for private)\n"
#ifdef ENABLE_WEBSERVER
            "--net-http-port <ports>  HTTP server ports (default: 8080)\n"
#endif
            "--net-ri-port <ports>    TCP raw input listen ports  (default: 30001)\n"
            "--net-ro-port <ports>    TCP raw output listen ports (default: 30002)\n"
            "--net-sbs-port <ports>   TCP BaseStation output listen ports (default: 30003)\n"
            "--net-bi-port <ports>    TCP Beast input listen ports  (default: 30004,30104)\n"
            "--net-bo-port <ports>    TCP Beast output listen ports (default: 30005)\n"
            "--net-ro-size <size>     TCP output minimum size (default: 0)\n"
            "--net-ro-interval <rate> TCP output memory flush rate in seconds (default: 0)\n"
            "--net-heartbeat <rate>   TCP heartbeat rate in seconds (default: 60 sec; 0 to disable)\n"
            "--net-buffer <n>         TCP buffer size 64Kb * (2^n) (default: n=0, 64Kb)\n"
            "--net-verbatim           Do not apply CRC corrections to messages we forward; send unchanged\n"
            "--forward-mlat           Allow forwarding of received mlat results to output ports\n"
            "--lat <latitude>         Reference/receiver latitude for surface posn (opt)\n"
            "--lon <longitude>        Reference/receiver longitude for surface posn (opt)\n"
            "--max-range <distance>   Absolute maximum range for position decoding (in nm, default: 300)\n"
            "--fix                    Enable single-bits error correction using CRC\n"
            "--no-fix                 Disable single-bits error correction using CRC\n"
            "--no-crc-check           Disable messages with broken CRC (discouraged)\n"
#ifdef ALLOW_AGGRESSIVE
            "--aggressive             More CPU for more messages (two bits fixes, ...)\n"
#endif
            "--mlat                   display raw messages in Beast ascii mode\n"
            "--stats                  Print stats at exit\n"
            "--stats-range            Collect/show range histogram\n"
            "--stats-every <seconds>  Show and reset stats every <seconds> seconds\n"
            "--onlyaddr               Show only ICAO addresses (testing purposes)\n"
            "--metric                 Use metric units (meters, km/h, ...)\n"
            "--gnss                   Show altitudes as HAE/GNSS (with H suffix) when available\n"
            "--snip <level>           Strip IQ file removing samples < level\n"
            "--debug <flags>          Debug mode (verbose), see README for details\n"
            "--quiet                  Disable output to stdout. Use for daemon applications\n"
            "--show-only <addr>       Show only messages from the given ICAO on stdout\n"
            "--ppm <error>            Set receiver error in parts per million (default 0)\n"
#ifdef HAVE_RTL_BIAST
            "--enable-rtlsdr-biast    Set bias tee supply on (default off)\n"
#endif
            "--html-dir <dir>         Use <dir> as base directory for the internal HTTP server. Defaults to " HTMLPATH "\n"
            "--write-json <dir>       Periodically write json output to <dir> (for serving by a separate webserver)\n"
            "--write-json-every <t>   Write json output every t seconds (default 1)\n"
            "--json-location-accuracy <n>  Accuracy of receiver location in json metadata: 0=no location, 1=approximate, 2=exact\n"
            "--dcfilter               Apply a 1Hz DC filter to input data (requires lots more CPU)\n"
            "--help                   Show this help\n"
            "\n"
            "Debug mode flags: d = Log frames decoded with errors\n"
            "                  D = Log frames decoded with zero errors\n"
            "                  c = Log frames with bad CRC\n"
            "                  C = Log frames with good CRC\n"
            "                  p = Log frames with bad preamble\n"
            "                  n = Log network debugging info\n"
            "                  j = Log frames to frames.js, loadable by debug.html\n",
            MODES_DUMP1090_VARIANT " " MODES_DUMP1090_VERSION
            );
}

static void display_total_stats(void) {
    struct stats added;
    add_stats(&Modes.stats_alltime, &Modes.stats_current, &added);
    display_stats(&added);
}

//
//=========================================================================
//
// This function is called a few times every second by main in order to
// perform tasks we need to do continuously, like accepting new clients
// from the net, refreshing the screen in interactive mode, and so forth
//

void backgroundTasks(void) {
    static uint64_t next_stats_display;
    static uint64_t next_stats_update;
    static uint64_t next_json, next_history;

    uint64_t now = mstime();

    icaoFilterExpire();
    trackPeriodicUpdate();

    if (Modes.net) {
        modesNetPeriodicWork();
    }


    // Refresh screen when in interactive mode
    if (Modes.interactive) {
        interactiveShowData();
    }

    // always update end time so it is current when requests arrive
    Modes.stats_current.end = now;

    if (now >= next_stats_update) {
        int i;

        if (next_stats_update == 0) {
            next_stats_update = now + 60000;
        } else {
            Modes.stats_latest_1min = (Modes.stats_latest_1min + 1) % 15;
            Modes.stats_1min[Modes.stats_latest_1min] = Modes.stats_current;

            add_stats(&Modes.stats_current, &Modes.stats_alltime, &Modes.stats_alltime);
            add_stats(&Modes.stats_current, &Modes.stats_periodic, &Modes.stats_periodic);

            reset_stats(&Modes.stats_5min);
            for (i = 0; i < 5; ++i)
                add_stats(&Modes.stats_1min[(Modes.stats_latest_1min - i + 15) % 15], &Modes.stats_5min, &Modes.stats_5min);

            reset_stats(&Modes.stats_15min);
            for (i = 0; i < 15; ++i)
                add_stats(&Modes.stats_1min[i], &Modes.stats_15min, &Modes.stats_15min);

            reset_stats(&Modes.stats_current);
            Modes.stats_current.start = Modes.stats_current.end = now;

            if (Modes.json_dir) {
                writeJsonToFile("stats.json", generateStatsJson);
                //writeJsonToFile("status.json", generateStatusJson);
            }

            next_stats_update += 60000;
        }
    }

    if (Modes.stats && now >= next_stats_display) {
        if (next_stats_display == 0) {
            next_stats_display = now + Modes.stats;
        } else {
            add_stats(&Modes.stats_periodic, &Modes.stats_current, &Modes.stats_periodic);
            display_stats(&Modes.stats_periodic);
            reset_stats(&Modes.stats_periodic);

            next_stats_display += Modes.stats;
            if (next_stats_display <= now) {
                /* something has gone wrong, perhaps the system clock jumped */
                next_stats_display = now + Modes.stats;
            }
        }
    }

    if (Modes.json_dir && now >= next_json) {
        writeJsonToFile("aircraft.json", generateAircraftJson);
        writeJsonToFile("status.json", generateStatusJson);
        next_json = now + Modes.json_interval;
    }

    if (now >= next_history) {
        int rewrite_receiver_json = (Modes.json_dir && Modes.json_aircraft_history[HISTORY_SIZE - 1].content == NULL);

        free(Modes.json_aircraft_history[Modes.json_aircraft_history_next].content); // might be NULL, that's OK.
        Modes.json_aircraft_history[Modes.json_aircraft_history_next].content =
                generateAircraftJson("/data/aircraft.json", &Modes.json_aircraft_history[Modes.json_aircraft_history_next].clen);

        if (Modes.json_dir) {
            char filebuf[PATH_MAX];
            snprintf(filebuf, PATH_MAX, "history_%d.json", Modes.json_aircraft_history_next);
            writeJsonToFile(filebuf, generateHistoryJson);
        }

        Modes.json_aircraft_history_next = (Modes.json_aircraft_history_next + 1) % HISTORY_SIZE;

        if (rewrite_receiver_json)
            writeJsonToFile("receiver.json", generateReceiverJson); // number of history entries changed

        next_history = now + HISTORY_INTERVAL;
    }
}

//
//=========================================================================
//

int verbose_device_search(char *s) {
    int i, device_count, device, offset;
    char *s2;
    char vendor[256], product[256], serial[256];
    device_count = rtlsdr_get_device_count();
    if (!device_count) {
        fprintf(stderr, "No supported devices found.\n");
        return -1;
    }
    airnav_log("Found %d device(s):\n", device_count);
    for (i = 0; i < device_count; i++) {
        if (rtlsdr_get_device_usb_strings(i, vendor, product, serial) != 0) {
            airnav_log("  %d:  unable to read device details\n", i);
        } else {
            airnav_log("  %d:  %s, %s, SN: %s\n", i, vendor, product, serial);
        }
    }
    airnav_log("\n");
    /* does string look like raw id number */
    device = (int) strtol(s, &s2, 0);
    if (s2[0] == '\0' && device >= 0 && device < device_count) {
        airnav_log("Using device %d: %s\n",
                device, rtlsdr_get_device_name((uint32_t) device));
        return device;
    }
    /* does string exact match a serial */
    for (i = 0; i < device_count; i++) {
        rtlsdr_get_device_usb_strings(i, vendor, product, serial);
        if (strcmp(s, serial) != 0) {
            continue;
        }
        device = i;
        airnav_log("Using device %d: %s\n",
                device, rtlsdr_get_device_name((uint32_t) device));
        return device;
    }
    /* does string prefix match a serial */
    for (i = 0; i < device_count; i++) {
        rtlsdr_get_device_usb_strings(i, vendor, product, serial);
        if (strncmp(s, serial, strlen(s)) != 0) {
            continue;
        }
        device = i;
        airnav_log("Using device %d: %s\n",
                device, rtlsdr_get_device_name((uint32_t) device));
        return device;
    }
    /* does string suffix match a serial */
    for (i = 0; i < device_count; i++) {
        rtlsdr_get_device_usb_strings(i, vendor, product, serial);
        offset = strlen(serial) - strlen(s);
        if (offset < 0) {
            continue;
        }
        if (strncmp(s, serial + offset, strlen(s)) != 0) {
            continue;
        }
        device = i;
        airnav_log("Using device %d: %s\n",
                device, rtlsdr_get_device_name((uint32_t) device));
        return device;
    }
    airnav_log("No matching devices found.\n");
    return -1;
}
//
//=========================================================================
//

int main(int argc, char** argv) {


    int i, j;

    for (i = j = 0; i < argc; i++) {
        if (!strcasecmp(argv[i], "-svc_start")) {
            j = 1;
        } else if (!strcasecmp(argv[i], "-svc_stop")) {
            j = 2;
        }
    }


    switch (j) {
        case 1:
        {
            //syslog(LOG_NOTICE, "Step A");
            dump_daemon();
            daemon_mode = 1;
            dump_main(argc, argv);
        }
            break;
        case 2:
        {
            //airnav_main();
        }
            break;
        default:
        {

            daemon_mode = 0;
            dump_main(argc, argv);
        }
            break;
    }


    return EXIT_SUCCESS;


}

/*
 * Function to create daemon
 */
static void dump_daemon() {
    pid_t pid;

    /* Fork off the parent process */
    pid = fork();

    /* An error occurred */
    if (pid < 0) {
        printf("Error 1\n");
        exit(EXIT_FAILURE);
    }

    /* Success: Let the parent terminate */
    if (pid > 0) {
        //printf("Terminando parent....\n");
        exit(EXIT_SUCCESS);
    }

    /* On success: The child process becomes session leader */
    if (setsid() < 0) {
        printf("Error 3\n");
        exit(EXIT_FAILURE);
    }

    /* Catch, ignore and handle signals */
    //TODO: Implement a working signal handler */
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    //signal(SIGPIPE, sigpipe_handler);
    //signal(SIGINT, intHandler);
    //sigaction(SIGCHLD, &sigchld_action, NULL);

    /* Fork off for the second time*/
    pid = fork();

    /* An error occurred */
    if (pid < 0) {
        printf("Error 4\n");
        exit(EXIT_FAILURE);
    }

    /* Success: Let the parent terminate */
    if (pid > 0) {

        //printf("Terminando parent 2....\n");
        exit(EXIT_SUCCESS);
    }

    /* Set new file permissions */
    umask(0);

    /* Change the working directory to the root directory */
    /* or another appropriated directory */
    //chdir("/radarbox/client/");

    /* Close all open file descriptors */
    //int x;
    //for (x = sysconf(_SC_OPEN_MAX); x >= 0; x--) {
    //    close(x);
    //}

    daemon_mode = 1;

    /* Open the log file */
    openlog("rbfeeder", LOG_PID, LOG_DAEMON);
}

int dump_main(int argc, char **argv) {
    int j;

    signal(SIGPIPE, sigpipe_handler);

    // Set sane defaults
    modesInitConfig();

    // signal handlers:
    signal(SIGINT, sigintHandler);
    signal(SIGTERM, sigtermHandler);


    airnav_loadConfig(argc, argv);
    argc = 1;

    // Parse the command line options
    for (j = 1; j < argc; j++) {
        int more = j + 1 < argc; // There are more arguments

        if (!strcmp(argv[j], "--device-index") && more) {
            Modes.dev_name = strdup(argv[++j]);
        } else if (!strcmp(argv[j], "--gain") && more) {
            Modes.gain = (int) (atof(argv[++j])*10); // Gain is in tens of DBs
        } else if (!strcmp(argv[j], "--enable-agc")) {
            Modes.enable_agc++;
        } else if (!strcmp(argv[j], "--freq") && more) {
            Modes.freq = (int) strtoll(argv[++j], NULL, 10);
        } else if (!strcmp(argv[j], "--ifile") && more) {
            Modes.filename = strdup(argv[++j]);
        } else if (!strcmp(argv[j], "--iformat") && more) {
            ++j;
            if (!strcasecmp(argv[j], "uc8")) {
                Modes.input_format = INPUT_UC8;
            } else if (!strcasecmp(argv[j], "sc16")) {
                Modes.input_format = INPUT_SC16;
            } else if (!strcasecmp(argv[j], "sc16q11")) {
                Modes.input_format = INPUT_SC16Q11;
            } else {
                fprintf(stderr, "Input format '%s' not understood (supported values: UC8, SC16, SC16Q11)\n",
                        argv[j]);
                exit(1);
            }
        } else if (!strcmp(argv[j], "--dcfilter")) {
            Modes.dc_filter = 1;
        } else if (!strcmp(argv[j], "--measure-noise")) {
            // Ignored
        } else if (!strcmp(argv[j], "--fix")) {
            Modes.nfix_crc = 1;
        } else if (!strcmp(argv[j], "--no-fix")) {
            Modes.nfix_crc = 0;
        } else if (!strcmp(argv[j], "--no-crc-check")) {
            Modes.check_crc = 0;
        } else if (!strcmp(argv[j], "--phase-enhance")) {
            // Ignored, always enabled
        } else if (!strcmp(argv[j], "--raw")) {
            Modes.raw = 1;
        } else if (!strcmp(argv[j], "--net")) {
            Modes.net = 1;
        } else if (!strcmp(argv[j], "--modeac")) {
            Modes.mode_ac = 1;
        } else if (!strcmp(argv[j], "--net-beast")) {
            fprintf(stderr, "--net-beast ignored, use --net-bo-port to control where Beast output is generated\n");
        } else if (!strcmp(argv[j], "--net-only")) {
            Modes.net = 1;
            Modes.net_only = 1;
        } else if (!strcmp(argv[j], "--net-heartbeat") && more) {
            Modes.net_heartbeat_interval = (uint64_t) (1000 * atof(argv[++j]));
        } else if (!strcmp(argv[j], "--net-ro-size") && more) {
            Modes.net_output_flush_size = atoi(argv[++j]);
        } else if (!strcmp(argv[j], "--net-ro-rate") && more) {
            Modes.net_output_flush_interval = 1000 * atoi(argv[++j]) / 15; // backwards compatibility
        } else if (!strcmp(argv[j], "--net-ro-interval") && more) {
            Modes.net_output_flush_interval = (uint64_t) (1000 * atof(argv[++j]));
        } else if (!strcmp(argv[j], "--net-ro-port") && more) {
            free(Modes.net_output_raw_ports);
            Modes.net_output_raw_ports = strdup(argv[++j]);
        } else if (!strcmp(argv[j], "--net-ri-port") && more) {
            free(Modes.net_input_raw_ports);
            Modes.net_input_raw_ports = strdup(argv[++j]);
        } else if (!strcmp(argv[j], "--net-bo-port") && more) {
            free(Modes.net_output_beast_ports);
            Modes.net_output_beast_ports = strdup(argv[++j]);
        } else if (!strcmp(argv[j], "--net-bi-port") && more) {
            free(Modes.net_input_beast_ports);
            Modes.net_input_beast_ports = strdup(argv[++j]);
        } else if (!strcmp(argv[j], "--net-bind-address") && more) {
            free(Modes.net_bind_address);
            Modes.net_bind_address = strdup(argv[++j]);
        } else if (!strcmp(argv[j], "--net-http-port") && more) {
#ifdef ENABLE_WEBSERVER
#ifdef RBCSRBLC
            free(Modes.net_http_ports);
            Modes.net_http_ports = strdup(argv[++j]);
#endif
#else
            if (strcmp(argv[++j], "0")) {
                fprintf(stderr, "warning: --net-http-port not supported in this build, option ignored.\n");
            }
#endif
        } else if (!strcmp(argv[j], "--net-sbs-port") && more) {
            free(Modes.net_output_sbs_ports);
            Modes.net_output_sbs_ports = strdup(argv[++j]);
        } else if (!strcmp(argv[j], "--net-buffer") && more) {
            Modes.net_sndbuf_size = atoi(argv[++j]);
        } else if (!strcmp(argv[j], "--net-verbatim")) {
            Modes.net_verbatim = 1;
        } else if (!strcmp(argv[j], "--forward-mlat")) {
            Modes.forward_mlat = 1;
        } else if (!strcmp(argv[j], "--onlyaddr")) {
            Modes.onlyaddr = 1;
        } else if (!strcmp(argv[j], "--metric")) {
            Modes.metric = 1;
        } else if (!strcmp(argv[j], "--hae") || !strcmp(argv[j], "--gnss")) {
            Modes.use_gnss = 1;
        } else if (!strcmp(argv[j], "--aggressive")) {
#ifdef ALLOW_AGGRESSIVE
            Modes.nfix_crc = MODES_MAX_BITERRORS;
#else
            fprintf(stderr, "warning: --aggressive not supported in this build, option ignored.\n");
#endif
        } else if (!strcmp(argv[j], "--interactive")) {
            Modes.interactive = Modes.throttle = 1;
        } else if (!strcmp(argv[j], "--throttle")) {
            Modes.throttle = 1;
        } else if (!strcmp(argv[j], "--interactive-rows") && more) {
            Modes.interactive_rows = atoi(argv[++j]);
        } else if (!strcmp(argv[j], "--interactive-ttl") && more) {
            Modes.interactive_display_ttl = (uint64_t) (1000 * atof(argv[++j]));
        } else if (!strcmp(argv[j], "--lat") && more) {
            Modes.fUserLat = atof(argv[++j]);
        } else if (!strcmp(argv[j], "--lon") && more) {
            Modes.fUserLon = atof(argv[++j]);
        } else if (!strcmp(argv[j], "--max-range") && more) {
            Modes.maxRange = atof(argv[++j]) * 1852.0; // convert to metres
        } else if (!strcmp(argv[j], "--debug") && more) {
            char *f = argv[++j];
            while (*f) {
                switch (*f) {
                    case 'D': Modes.debug |= MODES_DEBUG_DEMOD;
                        break;
                    case 'd': Modes.debug |= MODES_DEBUG_DEMODERR;
                        break;
                    case 'C': Modes.debug |= MODES_DEBUG_GOODCRC;
                        break;
                    case 'c': Modes.debug |= MODES_DEBUG_BADCRC;
                        break;
                    case 'p': Modes.debug |= MODES_DEBUG_NOPREAMBLE;
                        break;
                    case 'n': Modes.debug |= MODES_DEBUG_NET;
                        break;
                    case 'j': Modes.debug |= MODES_DEBUG_JS;
                        break;
                    default:
                        fprintf(stderr, "Unknown debugging flag: %c\n", *f);
                        exit(1);
                        break;
                }
                f++;
            }
        } else if (!strcmp(argv[j], "--stats")) {
            if (!Modes.stats)
                Modes.stats = (uint64_t) 1 << 60; // "never"
        } else if (!strcmp(argv[j], "--stats-range")) {
            Modes.stats_range_histo = 1;
        } else if (!strcmp(argv[j], "--stats-every") && more) {
            Modes.stats = (uint64_t) (1000 * atof(argv[++j]));
        } else if (!strcmp(argv[j], "--snip") && more) {
            snipMode(atoi(argv[++j]));
            exit(0);
        } else if (!strcmp(argv[j], "--help")) {
            showHelp();
            exit(0);
        } else if (!strcmp(argv[j], "--ppm") && more) {
            Modes.ppm_error = atoi(argv[++j]);
#ifdef HAVE_RTL_BIAST
        } else if (!strcmp(argv[j], "--enable-rtlsdr-biast")) {
            Modes.enable_rtlsdr_biast = 1;
#endif
        } else if (!strcmp(argv[j], "--quiet")) {
            Modes.quiet = 1;
        } else if (!strcmp(argv[j], "--show-only") && more) {
            Modes.show_only = (uint32_t) strtoul(argv[++j], NULL, 16);
        } else if (!strcmp(argv[j], "--mlat")) {
            Modes.mlat = 1;
        } else if (!strcmp(argv[j], "--interactive-rtl1090")) {
            Modes.interactive = 1;
            Modes.interactive_rtl1090 = 1;
        } else if (!strcmp(argv[j], "--oversample")) {
            // Ignored
        } else if (!strcmp(argv[j], "--html-dir") && more) {
            Modes.html_dir = strdup(argv[++j]);
#ifndef _WIN32
        } else if (!strcmp(argv[j], "--write-json") && more) {
            Modes.json_dir = strdup(argv[++j]);
        } else if (!strcmp(argv[j], "--write-json-every") && more) {
            Modes.json_interval = (uint64_t) (1000 * atof(argv[++j]));
            if (Modes.json_interval < 100) // 0.1s
                Modes.json_interval = 100;
        } else if (!strcmp(argv[j], "--json-location-accuracy") && more) {
            Modes.json_location_accuracy = atoi(argv[++j]);
#endif
        } else {
            fprintf(stderr,
                    "Unknown or not enough arguments for option '%s'.\n\n",
                    argv[j]);
            showHelp();
            exit(1);
        }
    }

#ifdef _WIN32
    // Try to comply with the Copyright license conditions for binary distribution
    if (!Modes.quiet) {
        showCopyright();
    }
#endif

#ifndef _WIN32
    // Setup for SIGWINCH for handling lines
    if (Modes.interactive) {
        signal(SIGWINCH, sigWinchCallback);
    }
#endif

    // Initialization
    //log_with_timestamp("%s %s starting up.", MODES_DUMP1090_VARIANT, MODES_DUMP1090_VERSION);
    modesInit();

    if (Modes.net_only) {
        //fprintf(stderr,"Net-only mode, no RTL device or file open.\n");
    } else if (Modes.filename == NULL) {
        if (modesInitRTLSDR() < 0) {
            exit(1);
        }
    } else {
        if (Modes.filename[0] == '-' && Modes.filename[1] == '\0') {
            Modes.fd = STDIN_FILENO;
        } else if ((Modes.fd = open(Modes.filename,
#ifdef _WIN32
                (O_RDONLY | O_BINARY)
#else
                (O_RDONLY)
#endif
                )) == -1) {
            perror("Opening data file");
            exit(1);
        }
    }
	airnav_log_level(2, "Modes.net = %d\n", Modes.net);
    if (Modes.net) modesInitNet();

    // Call AirNav main function
    airnav_log_level(2, "Gain: %d\n", Modes.gain);
    airnav_log_level(2, "AGC: %d\n", Modes.enable_agc);
    airnav_log_level(2, "Fix: %d\n", Modes.nfix_crc);
    airnav_log_level(2, "ModeAC: %d\n", Modes.mode_ac);
    airnav_log_level(2, "DC Filter: %d\n", Modes.dc_filter);
    airnav_log_level(2, "Lat: %f\n", Modes.fUserLat);
    airnav_log_level(2, "Lon: %f\n", Modes.fUserLon);
    airnav_log_level(2, "Check CRC: %d\n", Modes.check_crc);
    airnav_log_level(2, "PPM Error: %d\n", Modes.ppm_error);

    airnav_main();

    // init stats:
    Modes.stats_current.start = Modes.stats_current.end =
            Modes.stats_alltime.start = Modes.stats_alltime.end =
            Modes.stats_periodic.start = Modes.stats_periodic.end =
            Modes.stats_5min.start = Modes.stats_5min.end =
            Modes.stats_15min.start = Modes.stats_15min.end = mstime();

    for (j = 0; j < 15; ++j)
        Modes.stats_1min[j].start = Modes.stats_1min[j].end = Modes.stats_current.start;

    // write initial json files so they're not missing
    writeJsonToFile("receiver.json", generateReceiverJson);
    writeJsonToFile("stats.json", generateStatsJson);
    writeJsonToFile("status.json", generateStatusJson);
    writeJsonToFile("aircraft.json", generateAircraftJson);

    // If the user specifies --net-only, just run in order to serve network
    // clients without reading data from the RTL device
    if (Modes.net_only) {
        while (!Modes.exit) {
            struct timespec start_time;

            start_cpu_timing(&start_time);
            backgroundTasks();
            end_cpu_timing(&start_time, &Modes.stats_current.background_cpu);

            usleep(100000);
        }
    } else {
        int watchdogCounter = 10; // about 1 second

        // Create the thread that will read the data from the device.
        pthread_mutex_lock(&Modes.data_mutex);
        pthread_create(&Modes.reader_thread, NULL, readerThreadEntryPoint, NULL);

        while (Modes.exit == 0) {
            struct timespec start_time;

            if (Modes.first_free_buffer == Modes.first_filled_buffer) {
                /* wait for more data.
                 * we should be getting data every 50-60ms. wait for max 100ms before we give up and do some background work.
                 * this is fairly aggressive as all our network I/O runs out of the background work!
                 */

                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 100000000;
                normalize_timespec(&ts);

                pthread_cond_timedwait(&Modes.data_cond, &Modes.data_mutex, &ts); // This unlocks Modes.data_mutex, and waits for Modes.data_cond
            }

            // Modes.data_mutex is locked, and possibly we have data.

            // copy out reader CPU time and reset it
            add_timespecs(&Modes.reader_cpu_accumulator, &Modes.stats_current.reader_cpu, &Modes.stats_current.reader_cpu);
            Modes.reader_cpu_accumulator.tv_sec = 0;
            Modes.reader_cpu_accumulator.tv_nsec = 0;

            if (Modes.first_free_buffer != Modes.first_filled_buffer) {
                // FIFO is not empty, process one buffer.

                struct mag_buf *buf;

                start_cpu_timing(&start_time);
                buf = &Modes.mag_buffers[Modes.first_filled_buffer];

                // Process data after releasing the lock, so that the capturing
                // thread can read data while we perform computationally expensive
                // stuff at the same time.
                pthread_mutex_unlock(&Modes.data_mutex);

                demodulate2400(buf);
                if (Modes.mode_ac) {
                    demodulate2400AC(buf);
                }

                Modes.stats_current.samples_processed += buf->length;
                Modes.stats_current.samples_dropped += buf->dropped;
                end_cpu_timing(&start_time, &Modes.stats_current.demod_cpu);

                // Mark the buffer we just processed as completed.
                pthread_mutex_lock(&Modes.data_mutex);
                Modes.first_filled_buffer = (Modes.first_filled_buffer + 1) % MODES_MAG_BUFFERS;
                pthread_cond_signal(&Modes.data_cond);
                pthread_mutex_unlock(&Modes.data_mutex);
                watchdogCounter = 10;
            } else {
                // Nothing to process this time around.
                pthread_mutex_unlock(&Modes.data_mutex);
                if (--watchdogCounter <= 0) {
                    //log_with_timestamp("No data received from the dongle for a long time, it may have wedged");
                    watchdogCounter = 600;
                }
            }

            start_cpu_timing(&start_time);
            backgroundTasks();
            end_cpu_timing(&start_time, &Modes.stats_current.background_cpu);

            pthread_mutex_lock(&Modes.data_mutex);
        }

        pthread_mutex_unlock(&Modes.data_mutex);

        //log_with_timestamp("Waiting for receive thread termination");
        pthread_join(Modes.reader_thread, NULL); // Wait on reader thread exit

        // Airnav        
        pthread_join(t_prepareData, NULL); // Wait on reader thread exit
        pthread_join(t_ext_source, NULL); // Wait on reader thread exit
        pthread_join(t_monitor, NULL); // Wait on reader thread exit
        pthread_join(t_statistics, NULL); // Wait on reader thread exit        
        pthread_join(t_stats, NULL); // Wait on reader thread exit
        pthread_join(t_send_data, NULL); // Wait on reader thread exit
        pthread_join(t_waitcmd, NULL); // Wait on reader thread exit
        pthread_join(t_led_adsb, NULL); // Wait on reader thread exit

#ifdef RBCSRBLC
        pthread_join(t_led_adsb, NULL); // Wait on reader thread exit
#ifdef RBCS
        pthread_join(t_vhf_led, NULL); // Wait on reader thread exit
#endif
        pthread_join(t_anrb, NULL); // ANRB Software

        airnav_log_level(2,"Turning off all leds.\n");

        led_off(LED_ADSB);
        led_off(LED_STATUS);
        led_off(LED_GPS);
        led_off(LED_PC);
        led_off(LED_ERROR);
        led_off(LED_VHF);
        sunxi_gpio_cleanup();

#endif

        if (checkMLATRunning()) {
            stopMLAT();
        }
        if (checkVhfRunning()) {
            stopVhf();
        }
        if (checkACARSRunning()) {
            stopACARS();
        }
        pthread_cond_destroy(&Modes.data_cond); // Thread cleanup - only after the reader thread is dead!
        pthread_mutex_destroy(&Modes.data_mutex);

        // Clear flist
        struct packet_list *tmp;
        while (flist != NULL) {
            free(flist->packet);
            tmp = flist->next;
            free(flist);
            flist = tmp;
        }

    }

    // If --stats were given, print statistics
    if (Modes.stats) {
        display_total_stats();
    }

    cleanup_converter(Modes.converter_state);
    //log_with_timestamp("Normal exit.");    
    remove(pidfile);

#ifndef _WIN32
    pthread_exit(0);
#else
    return (0);
#endif
}
//
//=========================================================================
//
