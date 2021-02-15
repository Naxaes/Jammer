#include <stdio.h>

#include "soundio/soundio.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>


#define ASSERT(statement, ...) do {                                            \
    if (!(statement)) {                                                        \
        char* buffer = (char*)malloc(255);                                     \
        snprintf(buffer, 255, __VA_ARGS__);                                    \
        fprintf(stderr, "[ASSERT] (" #statement ") %s:%d\n\t%s", __FILE__, __LINE__, buffer); \
        std::exit(22);                                                         \
        free(buffer);  /* Not necessary, but whatever... */                    \
    }                                                                          \
} while (false)


struct SoundIoRingBuffer* ring_buffer = NULL;


static int Min(int a, int b) { return (a < b) ? a : b; }
static void ReadCallback(struct SoundIoInStream* instream, int frame_count_min, int frame_count_max) 
{
    struct SoundIoChannelArea* areas;
    int error_code;
    
    char* write_ptr  = soundio_ring_buffer_write_ptr(ring_buffer);   // Do not write more than capacity.
    int   free_bytes = soundio_ring_buffer_free_count(ring_buffer);  // Returns how many bytes of the buffer is free, ready for writing.
    int   free_count = free_bytes / instream->bytes_per_frame;

    if (free_count < frame_count_min)
    {
        printf("Ring buffer overflow!\n");
        return;
    }

    int write_frames = Min(free_count, frame_count_max);
    int frames_left  = write_frames;
    while (1)
    {
        int frame_count = frames_left;
        ASSERT(!(error_code = soundio_instream_begin_read(instream, &areas, &frame_count)), "Begin read error: %s\n", soundio_strerror(error_code));

        if (!frame_count)
            break;
        
        if (!areas) 
        {
            // Due to an overflow there is a hole. Fill the ring buffer with
            // silence for the size of the hole.
            memset(write_ptr, 0, frame_count * instream->bytes_per_frame);
            fprintf(stderr, "Dropped %d frames due to internal overflow.\n", frame_count);
        } 
        else 
        {
            for (int frame = 0; frame < frame_count; frame += 1) 
            {
                for (int ch = 0; ch < instream->layout.channel_count; ch += 1) 
                {
                    memcpy(write_ptr, areas[ch].ptr, instream->bytes_per_sample);
                    areas[ch].ptr += areas[ch].step;
                    write_ptr += instream->bytes_per_sample;
                }
            }
        }

        ASSERT(!(error_code = soundio_instream_end_read(instream)), "End read error: %s", soundio_strerror(error_code));

        frames_left -= frame_count;
        if (frames_left <= 0)
            break;
    }

    int advance_bytes = write_frames * instream->bytes_per_frame;
    soundio_ring_buffer_advance_write_ptr(ring_buffer, advance_bytes);
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

const int size = 3310848;
static char buffer[size];

// Called when we need more data to play.
static void WriteCallback(struct SoundIoOutStream* outstream, int frame_count_min, int frame_count_max)
{

    static int frame = 0;
    char temp[4];
    char s[46];

    static int current = 0;
    int numbytes;
    struct sockaddr_storage their_addr;
    int socket_fd = *(int*)outstream->userdata;
    unsigned int addr_len = sizeof(their_addr);
    if ((numbytes = recvfrom(socket_fd, temp, 4 , 0, (struct sockaddr *)&their_addr, &addr_len)) != -1)
    {
        printf("%d: %s\n\t", frame, inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof(s)));
        printf("%d ", *(unsigned short*)temp);
        printf("%d ", *(short*)temp+2);
        printf("\n");
        memcpy(buffer + 10*frame++, temp+1, 10);
    }




    struct SoundIoChannelArea* areas;
    int frame_count;
    int error_code;

    char* read_ptr   = soundio_ring_buffer_read_ptr(ring_buffer);    // Do not read more than capacity.
    int   fill_bytes = soundio_ring_buffer_fill_count(ring_buffer);  // Returns how many bytes of the buffer is used, ready for reading.
    int   fill_count = fill_bytes / outstream->bytes_per_frame;
    
    if (frame_count_min > fill_count) 
    {
        // Ring buffer does not have enough data, fill with zeroes.
        while (1)
        {
            ASSERT(!(error_code = soundio_outstream_begin_write(outstream, &areas, &frame_count)), "Begin write error: %s\n", soundio_strerror(error_code));
            
            if (frame_count <= 0)
                return;
            
            for (int frame = 0; frame < frame_count; frame += 1) 
            {
                for (int ch = 0; ch < outstream->layout.channel_count; ch += 1) 
                {
                    memset(areas[ch].ptr, 0, outstream->bytes_per_sample);
                    areas[ch].ptr += areas[ch].step;
                }
            }
           ASSERT(!(error_code = soundio_outstream_end_write(outstream)), "End write error: %s\n", soundio_strerror(error_code));
        }
    }

    int read_count  = Min(frame_count_max, fill_count);
    int frames_left = read_count;
    
    while (frames_left > 0) 
    {
        int frame_count = frames_left;
        ASSERT(!(error_code = soundio_outstream_begin_write(outstream, &areas, &frame_count)), "Begin write error: %s\n", soundio_strerror(error_code));
        
        if (frame_count <= 0)
            break;
        
        for (int frame = 0; frame < frame_count; frame += 1) 
        {
            for (int channel = 0; channel < outstream->layout.channel_count; channel += 1)
            {
                float *buf = (float *)areas[channel].ptr;
                *buf = float(buffer[(current++)%size]) / 127.0;

//                memcpy(areas[channel].ptr, read_ptr, outstream->bytes_per_sample);
                areas[channel].ptr += areas[channel].step;
                read_ptr += outstream->bytes_per_sample;
            }
        }
        
        ASSERT(!(error_code = soundio_outstream_end_write(outstream)), "End write error: %s", soundio_strerror(error_code));
        frames_left -= frame_count;
    }
    soundio_ring_buffer_advance_read_ptr(ring_buffer, read_count * outstream->bytes_per_frame);
}


static void UnderflowCallback(struct SoundIoOutStream *outstream) 
{
    static int count = 0;
    fprintf(stderr, "Underflow %d\n", ++count);
}

static void OnBackendDisconnect(struct SoundIo* soundio, int error_code)
{
    fprintf(stderr, "%s disconnected with '%s'.\n",
        soundio_backend_name(soundio->current_backend),
        soundio_strerror(error_code)
    );
}

static void OnDeviceChange(struct SoundIo* soundio)
{
    fprintf(stderr, "Devices changed.\n");
}

static void OnEventsSignal(struct SoundIo* soundio)
{
    fprintf(stderr, "soundio_wait_events woke up.\n");
}


static void PrintDeviceInfo(struct SoundIoDevice* device, bool is_default) {
    const char* default_str = is_default     ? " (default)" : "";
    const char* raw_str     = device->is_raw ? " (raw)"     : "";

    fprintf(stderr, "%s%s%s\n", device->name, default_str, raw_str);
    fprintf(stderr, "\tID: %s\n", device->id);
    fprintf(stderr, "\tChannel layouts:\n");

    for (int i = 0; i < device->layout_count; i += 1) {
        const SoundIoChannelLayout* layout = &device->layouts[i];
        const char* current = (layout == &device->current_layout) ? " (current)" : "";

        if (layout->name)
        {
            fprintf(stderr, "\t\t* %s%s", layout->name, current);
        }
        else
        {
            fprintf(stderr, "\t\t* %s", soundio_get_channel_name(layout->channels[0]));
            for (int i = 1; i < layout->channel_count; i += 1)
                fprintf(stderr, ", %s", soundio_get_channel_name(layout->channels[i]));
            fprintf(stderr, "%s", current);
        }
        fprintf(stderr, "\n");
    }

    fprintf(stderr, "\tSample rates:\n");
    for (int i = 0; i < device->sample_rate_count; i += 1)
    {
        struct SoundIoSampleRateRange *range = &device->sample_rates[i];
        const char* current = (range->min <= device->sample_rate_current && device->sample_rate_current <= range->max) ? " (current)": "";
        fprintf(stderr, "\t\t* %d - %d%s\n", range->min, range->max, current);

    }

    fprintf(stderr, "\tFormats: ");
    for (int i = 0; i < device->format_count; i += 1)
    {
        const char* comma   = (i == device->format_count - 1) ? "" : ", ";
        const char* current = (device->current_format == device->formats[i]) ? " (current)": "";  // TODO: Doesn't work.
        fprintf(stderr, "%s%s%s", soundio_format_string(device->formats[i]), current, comma);
    }
    fprintf(stderr, "\n");

    fprintf(stderr, "\tMin software latency: %0.8f sec\n", device->software_latency_min);
    fprintf(stderr, "\tMax software latency: %0.8f sec\n", device->software_latency_max);
    if (device->software_latency_current != 0.0)
        fprintf(stderr, "\tCurrent software latency: %0.8f sec\n", device->software_latency_current);

    fprintf(stderr, "\n");
}

struct Audio
{
    struct SoundIo*          soundio;
    struct SoundIoDevice*    in_device;
    struct SoundIoDevice*    out_device;
    struct SoundIoInStream*  instream;
    struct SoundIoOutStream* outstream;
};


struct Device
{
    SoundIoDevice* device;
    int index;
};

Audio InitAudio()
{
    int error_code;
    double microphone_latency = 0.001; // seconds

    // ---- Create SoundIO device ----
    struct SoundIo* soundio = soundio_create();
    ASSERT(soundio, "Out of memory.\n");
    ASSERT(!(error_code = soundio_connect(soundio)), "Error connecting: %s\n", soundio_strerror(error_code));

    // ---- Atomically update information for all connected devices. ----
    soundio_flush_events(soundio);

    soundio->on_backend_disconnect = OnBackendDisconnect;
    soundio->on_devices_change     = OnDeviceChange;
    soundio->on_events_signal      = OnEventsSignal;
    fprintf(stderr, "Connected to %s.\n", soundio_backend_name(soundio->current_backend));


    // ---- Collect output devices ----
    Device available_output_devices[16] = { 0 };
    int output_device_count = soundio_output_device_count(soundio);

    int default_out_device_index = soundio_default_output_device_index(soundio);
    ASSERT(default_out_device_index >= 0, "No output device found.\n");

    int out_device_index = default_out_device_index;
    for (int i = 0; i < output_device_count; i += 1)
    {
        struct SoundIoDevice* device = soundio_get_output_device(soundio, i);
        available_output_devices[i] = { device, out_device_index };
        out_device_index = i;
    }

    // ---- Collect default input device ----
    Device available_input_devices[16] = { 0 };
    int input_device_count = soundio_input_device_count(soundio);

    int default_in_device_index = soundio_default_input_device_index(soundio);
    ASSERT(default_in_device_index >= 0, "No input device found\n");

    int in_device_index = default_in_device_index;
    for (int i = 0; i < input_device_count; i += 1)
    {
        struct SoundIoDevice* device = soundio_get_input_device(soundio, i);
        available_input_devices[i] = { device, in_device_index };
        in_device_index = i;
    }

    printf("Select input device: \n");
    for (int i = 0; i < input_device_count; ++i)
    {
        struct SoundIoDevice* device = available_input_devices[i].device;
        printf(" %i: %s\n", i, device->name);
        soundio_device_unref(device);
    }
    scanf("%d", &in_device_index);


    printf("Select output device: \n");
    for (int i = 0; i < output_device_count; ++i)
    {
        struct SoundIoDevice* device = available_output_devices[i].device;
        printf(" %i: %s\n", i, device->name);
        soundio_device_unref(device);
    }
    scanf("%d", &out_device_index);


    struct SoundIoDevice* out_device = soundio_get_output_device(soundio, out_device_index);
    ASSERT(out_device, "Could not get output device: out of memory\n");
    struct SoundIoDevice* in_device = soundio_get_input_device(soundio, in_device_index);
    ASSERT(in_device, "Could not get input device: out of memory\n");

    ASSERT(!in_device->probe_error,  "Cannot probe indevice: %s\n", soundio_strerror(in_device->probe_error));
    ASSERT(!out_device->probe_error, "Cannot probe indevice: %s\n", soundio_strerror(out_device->probe_error));

    fprintf(stderr, "Input device:  %s\n", in_device->name);
    fprintf(stderr, "Output device: %s\n", out_device->name);

    // ---- Find compatible layout between input and output device ----
    soundio_device_sort_channel_layouts(out_device);
    const struct SoundIoChannelLayout* layout = soundio_best_matching_channel_layout(
        out_device->layouts, out_device->layout_count,
        in_device->layouts,  in_device->layout_count
   );
    ASSERT(layout, "Channel layouts not compatible between input and output device.\n");


    // ---- Checks whether the devices support the same sample rates.
    static int prioritized_sample_rates[] = { 48000, 44100, 96000, 24000, 0 };
    int* sample_rate;
    for (sample_rate = prioritized_sample_rates; *sample_rate; sample_rate += 1) 
        if (soundio_device_supports_sample_rate(in_device, *sample_rate) && soundio_device_supports_sample_rate(out_device, *sample_rate))
            break;
    ASSERT(*sample_rate, "Incompatible sample rates between input and output device.\n");


    // ---- Checks whether the devices support the same sample format.
    static enum SoundIoFormat prioritized_formats[] = { SoundIoFormatFloat32NE, SoundIoFormatFloat32FE, SoundIoFormatS32NE, SoundIoFormatS32FE, SoundIoFormatS24NE, SoundIoFormatS24FE, SoundIoFormatS16NE, SoundIoFormatS16FE, SoundIoFormatFloat64NE, SoundIoFormatFloat64FE, SoundIoFormatU32NE, SoundIoFormatU32FE, SoundIoFormatU24NE, SoundIoFormatU24FE, SoundIoFormatU16NE, SoundIoFormatU16FE, SoundIoFormatS8, SoundIoFormatU8, SoundIoFormatInvalid};
    enum SoundIoFormat* fmt;
    for (fmt = prioritized_formats; *fmt != SoundIoFormatInvalid; fmt += 1) {
        if (soundio_device_supports_format(in_device, *fmt) &&
            soundio_device_supports_format(out_device, *fmt))
        {
            break;
        }
    }
    ASSERT(*fmt != SoundIoFormatInvalid, "Incompatible sample formats between input and output device.\n");

    // ---- Create input stream ----
    struct SoundIoInStream* instream = soundio_instream_create(in_device);
    ASSERT(instream, "Out of memory\n");
    instream->format = *fmt;
    instream->sample_rate = *sample_rate;
    instream->layout = *layout;
    instream->software_latency = microphone_latency;
    instream->read_callback = ReadCallback;
    ASSERT(!(error_code = soundio_instream_open(instream)), "Unable to open input stream: %s\n", soundio_strerror(error_code));

    // ---- Create input stream ----
    struct SoundIoOutStream* outstream = soundio_outstream_create(out_device);
    ASSERT(outstream, "Out of memory\n");
    outstream->format = *fmt;
    outstream->sample_rate = *sample_rate;
    outstream->layout = *layout;
    outstream->software_latency = microphone_latency;
    outstream->write_callback = WriteCallback;
    outstream->underflow_callback = UnderflowCallback;
    ASSERT(!(error_code = soundio_outstream_open(outstream)), "Unable to open output stream: %s\n", soundio_strerror(error_code));


    // ---- Create ring buffer ----
    // capacity will be round up to nearest page size.
    int capacity   = 2 * microphone_latency * instream->sample_rate  * instream->bytes_per_frame;
    int fill_count =     microphone_latency * outstream->sample_rate * outstream->bytes_per_frame;

    fill_count = 256;

    ring_buffer = soundio_ring_buffer_create(soundio, capacity);
    ASSERT(ring_buffer, "Unable to create ring buffer: out of memory\n");
    char* buffer = soundio_ring_buffer_write_ptr(ring_buffer);
    memset(buffer, 0, fill_count);
    soundio_ring_buffer_advance_write_ptr(ring_buffer, fill_count);
    

    // --- Start the streams.
    ASSERT(!(error_code = soundio_instream_start(instream)),   "Unable to start input device: %s\n", soundio_strerror(error_code));
    ASSERT(!(error_code = soundio_outstream_start(outstream)), "Unable to start output device: %s\n", soundio_strerror(error_code));


    PrintDeviceInfo(in_device,  in_device_index  == default_in_device_index);
    PrintDeviceInfo(out_device, out_device_index == default_out_device_index);

    return { soundio, in_device, out_device, instream, outstream };
}

void QuitAudio(Audio* source)
{
    soundio_outstream_destroy(source->outstream);
    soundio_instream_destroy(source->instream);
    soundio_device_unref(source->in_device);
    soundio_device_unref(source->out_device);
    soundio_destroy(source->soundio);
}
