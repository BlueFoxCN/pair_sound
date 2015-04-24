#include <iostream>
#include <alsa/asoundlib.h>
#include <speex/speex.h>
#include <stdlib.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <list>
#include <time.h>
#include <dirent.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "transmit.h"
#include "tool.h"
#include "log.h"

using namespace std;

Transmit::Transmit() {
}

void Transmit::start() {
  int ap_recv_port = 9003;

  // about record
  int rc;
  int size;
  snd_pcm_t *handle;
  snd_pcm_hw_params_t *params;
  unsigned int val;
  int dir;
  snd_pcm_uframes_t frames;
  int channel_num = 1;

  // buffers
  char *buffer;
  char *t_buffer;
  int factor;

  // adpcm compress
  bool adpcm = false;
  short code, sb, delta, cur_sample, prev_sample = 0;
  int adpcm_cycle = 4, adpcm_index = 0;
  char *adpcm_buffer;
  short temp1 = 0, temp2 = 0, index = 15, packet_index = 0;
  int index_adjust[8] = {-1,-1,-1,-1,2,4,6,8};
  int step_table[89] = 
    {
      7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,
      50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,
      408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,
      2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,
      10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767
    };

  // speex compress
  bool speex = true;
  char s_buffer[70];
  void *enc_state;
  SpeexBits enc_bits;
  int nbBytes;
  enc_state = speex_encoder_init(&speex_nb_mode);
  int q = 8;
  speex_encoder_ctl(enc_state,SPEEX_SET_QUALITY,&q);
  // speex_encoder_ctl(enc_state,SPEEX_GET_FRAME_SIZE,&frames_size );
  speex_bits_init(&enc_bits);

  while (true) {
    sleep(1);
    char *l = get_local_ip("wlan0");
    char local_ip[100];
    strcpy(local_ip, l);
    if (strcmp(local_ip, "0.0.0.0") == 0) {
      continue;
    }
    // get gateway ip address
    char *gateway_ip = get_gateway_ip_from_local_ip(local_ip, sizeof(local_ip));
    char real_gateway_ip[100];
    sprintf(real_gateway_ip, "%s\0", gateway_ip);
    // char real_gateway_ip[100] = "127.0.0.1";

    int socket_src = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_src == -1)
    {
      break;
    }
    struct sockaddr_in server;
    server.sin_addr.s_addr = inet_addr(real_gateway_ip);
    server.sin_family = AF_INET;
    server.sin_port = htons(ap_recv_port);

    struct sockaddr_in addr_from;
    addr_from.sin_family = AF_INET;
    addr_from.sin_port = htons(0);
    addr_from.sin_addr.s_addr = htons(INADDR_ANY);
    int i = ::bind(socket_src, (struct sockaddr*)&addr_from, sizeof(addr_from));

    if (i < 0)
    {
      close(socket_src);
      break;
    }
    // has connected to the ap, begin to record and send data
    // Open PCM device for recording (capture).
    rc = snd_pcm_open(&handle, "hw:0,0",
            SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0) {
      log_error("unable to open pcm device: %s", snd_strerror(rc));
      exit(1);
    }

    // Allocate a hardware parameters object.
    snd_pcm_hw_params_alloca(&params);

    // Fill it in with default values.
    snd_pcm_hw_params_any(handle, params);

    // Set the desired hardware parameters.

    // Interleaved mode
    snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);

    // Signed 16-bit little-endian format
    snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);

    // Two channels (stereo)
    snd_pcm_hw_params_set_channels(handle, params, channel_num);

    // 48000 bits/second sampling rate (CD quality)
    val = 48000;
    snd_pcm_hw_params_set_rate_near(handle, params, &val, &dir);

    // Set period size to 160 frames.
    factor = 6;  // 160 = fraems / factor
    frames = 160 * factor;
    snd_pcm_hw_params_set_period_size_near(handle, params, &frames, &dir);

    // Write the parameters to the driver
    rc = snd_pcm_hw_params(handle, params);
    if (rc < 0) {
      log_error("unable to set hw parameters: %s", snd_strerror(rc));
      exit(1);
    }

    // Use a buffer large enough to hold one period
    snd_pcm_hw_params_get_period_size(params, &frames, &dir);
    size = frames * 2 * channel_num; // 2 bytes/sample, 2 channels
    buffer = (char *) malloc(size);
    t_buffer = (char *) malloc(size / factor);
    adpcm_buffer = (char *) malloc(size / factor / 4 * adpcm_cycle + 6);
    short speex_out[size / factor / 2];

    snd_pcm_hw_params_get_period_time(params, &val, &dir);

    while (true)
    {
      rc = snd_pcm_readi(handle, buffer, frames);
      if (rc == -EPIPE) {
        // EPIPE means overrun
        log_warn("overrun occurred");
        snd_pcm_prepare(handle);
      } else if (rc < 0) {
        log_error("error from read: %s", snd_strerror(rc));
      } else if (rc != (int)frames) {
        log_warn("short read, read %d frames", rc);
      }

      for (int i = 0; i < size / factor / 2; i ++) {
        t_buffer[2 * i] = buffer[2 * factor * i];
        t_buffer[2 * i + 1] = buffer[2 * factor * i + 1];
      }

      if (adpcm) {
        if (adpcm_index == 0) {
          adpcm_buffer[0] = prev_sample & 0xFF;
          adpcm_buffer[1] = prev_sample >> 8;
          adpcm_buffer[2] = index & 0xFF;
          adpcm_buffer[4] = packet_index & 0xFF;
          adpcm_buffer[5] = packet_index >> 8;
          packet_index++;
          if (packet_index == 10000) {
            packet_index = 0;
          }
        }
        // apply adpcm algorithm to the buffer data
        for (int i = 0; i < size / factor / 2; i++) {
          cur_sample = (((short)t_buffer[2 * i + 1]) << 8) | (t_buffer[2 * i] & 0xFF);
          delta = cur_sample - prev_sample;
          if (delta < 0) {
            delta = -delta;
            sb = 8;
          } else {
            sb = 0;
          }
          code = 4 * delta / step_table[index];
          if (code > 7) {
            code = 7;
          }
          index += index_adjust[code];
          if (index < 0) {
            index = 0;
          } else if (index > 88) {
            index = 88;
          }
          prev_sample = cur_sample;
          if (i % 2 == 0) {
            temp1 = code | sb;
          } else {
            temp2 = code | sb;
            adpcm_buffer[adpcm_index * (size / factor / 4) + (i - 1) / 2 + 6] = (temp2 << 4) | (temp1 & 0x0F);
          }
        }
        if (adpcm_index == adpcm_cycle - 1) {
          adpcm_index = 0;
          if (sendto(socket_src, adpcm_buffer, size / factor / 4 * adpcm_cycle + 6, 0, (struct sockaddr*)&server, sizeof(server)) < 0) {
            break;
          }
        } else {
          adpcm_index++;
        }
      } else if (speex) {
        for(int i = 0; i < size / factor / 2; i++) {
          // speex_out[i] = ( t_buffer[2 * i + 1] << 8 ) | (unsigned char)t_buffer[2 * i];
          speex_out[i] = (((short)t_buffer[2 * i + 1]) << 8) | (t_buffer[2 * i] & 0xFF);
        }
        speex_bits_reset(&enc_bits);
        speex_encode_int(enc_state, speex_out, &enc_bits);
        nbBytes = speex_bits_write(&enc_bits, s_buffer, 200);
        fprintf(stderr, "%d\n", nbBytes);
        if (sendto(socket_src, s_buffer, nbBytes, 0, (struct sockaddr*)&server, sizeof(server)) < 0) {
          break;
        }
      } else {
        if (sendto(socket_src, t_buffer, size / factor, 0, (struct sockaddr*)&server, sizeof(server)) < 0) {
          break;
        }
      }
    }

    free(buffer);
    close(socket_src);

    snd_pcm_drain(handle);
    snd_pcm_close(handle);
  }
}
