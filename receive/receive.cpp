#include <iostream>
#include <alsa/asoundlib.h>
#include <stdlib.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <list>
#include <time.h>
#include <dirent.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "receive.h"
#include "tool.h"
#include "log.h"

using namespace std;

Receive::Receive() {
}

void Receive::start() {
  // the audio initialization part
  long loops;
  int rc;
  int size;
  snd_pcm_t *handle;
  snd_pcm_hw_params_t *params;
  unsigned int val;
  int dir;
  snd_pcm_uframes_t frames;
  char *buffer;
  int factor;
  int channel_num = 1;

  // adpcm compress
  bool adpcm = true;
  short code, sb, delta, cur_sample, cur_data;
  int index = 0;
  char *adpcm_buffer;
  short temp1 = 0, temp2 = 0;
  int index_adjust[8] = {-1,-1,-1,-1,2,4,6,8};
  int step_table[89] = 
    {
      7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,
      50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,
      408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,
      2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,
      10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767
    };

  /* Open PCM device for playback. */
  rc = snd_pcm_open(&handle, "plughw:0,0", SND_PCM_STREAM_PLAYBACK, 0);
  if (rc < 0) {
    log_error("unable to open pcm device: %s", snd_strerror(rc));
    exit(1);
  }

  /* Allocate a hardware parameters object. */
  snd_pcm_hw_params_alloca(&params);

  /* Fill it in with default values. */
  snd_pcm_hw_params_any(handle, params);

  /* Set the desired hardware parameters. */

  /* Interleaved mode */
  snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);

  /* Signed 16-bit little-endian format */
  snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);

  /* Two channels (stereo) */
  snd_pcm_hw_params_set_channels(handle, params, channel_num);

  /* 44100 bits/second sampling rate (CD quality) */
  factor = 6;
  val = 48000 / factor;
  snd_pcm_hw_params_set_rate_near(handle, params, &val, &dir);

  /* Set period size to 64 frames. */
  frames = 160;
  snd_pcm_hw_params_set_period_size_near(handle, params, &frames, &dir);

  /* Write the parameters to the driver */
  rc = snd_pcm_hw_params(handle, params);
  if (rc < 0) {
    log_error("unable to set hw parameters: %s", snd_strerror(rc));
    exit(1);
  }

  /* Use a buffer large enough to hold one period */
  snd_pcm_hw_params_get_period_size(params, &frames, &dir);
  size = frames * 2 * channel_num; /* 2 bytes/sample, 2 channels */
  buffer = (char *) malloc(size);

  /* We want to loop for 5 seconds */
  snd_pcm_hw_params_get_period_time(params, &val, &dir);

  int ap_recv_port = 9003;

  // the network part
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if(fd == -1) {
    log_error("socket create error!");
    exit(-1);
  }
  printf("socket fd=%d\n",fd);

  struct sockaddr_in addr;
  addr.sin_family=AF_INET;
  addr.sin_port=htons(ap_recv_port);
  addr.sin_addr.s_addr=inet_addr("192.168.3.1");
  // addr.sin_addr.s_addr=inet_addr("127.0.0.1");

  int r;
  r = ::bind(fd, (struct sockaddr*)&addr, sizeof(addr));
  if(r == -1) {
    log_error("bind error!");
    close(fd);
    exit(-1);
  }

  struct sockaddr_in from;
  socklen_t len;
  len = sizeof(from);

  adpcm_buffer = (char *) malloc(size / 4);
  while (true) {
    if (adpcm) {
      r = recvfrom(fd, adpcm_buffer, size / 4, 0, (struct sockaddr*)&from, &len);
      for (int i = 0; i < size / 4; i++) {
        code = ( (short)adpcm_buffer[i] ) & 15;
        if ((code & 8) != 0) {
          sb = 1;
        } else {
          sb = 0;
        }
        code &= 7;
        delta = (step_table[index]*code) / 4 + step_table[index] / 8;
        if (sb == 1) {
          delta = -delta;
        }
        cur_sample += delta;
        if (cur_sample > 32767) {
          cur_data = 32767;
        } else if (cur_sample < -32767) {
          cur_data = -32767;
        } else {
          cur_data = cur_sample;
        }
        index += index_adjust[code];
        if (index < 0) {
          index = 0;
        } else if (index > 88) {
          index = 88;
        }
        buffer[i * 4] = cur_data;
        buffer[i * 4 + 1] = cur_data >> 8;

        code = ( (short)adpcm_buffer[i] >> 8) & 15;
        if ((code & 8) != 0) {
          sb = 1;
        } else {
          sb = 0;
        }
        code &= 7;
        delta = (step_table[index]*code) / 4 + step_table[index] / 8;
        if (sb == 1) {
          delta = -delta;
        }
        cur_sample += delta;
        if (cur_sample > 32767) {
          cur_data = 32767;
        } else if (cur_sample < -32767) {
          cur_data = -32767;
        } else {
          cur_data = cur_sample;
        }
        index += index_adjust[code];
        if (index < 0) {
          index = 0;
        } else if (index > 88) {
          index = 88;
        }
        buffer[i * 4 + 2] = cur_data;
        buffer[i * 4 + 3] = cur_data >> 8;
      }
      log_warn("1: %d", (((short)buffer[2 * 0 + 1]) << 8) | buffer[2 * 0]);
      log_warn("2: %d", (((short)buffer[2 * 1 + 1]) << 8) | buffer[2 * 1]);
      log_warn("3: %d", (((short)buffer[2 * 2 + 1]) << 8) | buffer[2 * 2]);
      log_warn("4: %d", (((short)buffer[2 * 3 + 1]) << 8) | buffer[2 * 3]);
      log_warn("5: %d", (((short)buffer[2 * 4 + 1]) << 8) | buffer[2 * 4]);
    } else {
      r = recvfrom(fd, buffer, size, 0, (struct sockaddr*)&from, &len);
    }

    rc = snd_pcm_writei(handle, buffer, frames);
    if (rc == -EPIPE) {
      // EPIPE means underrun
      fprintf(stderr, "underrun occurred\n");
      snd_pcm_prepare(handle);
    } else if (rc < 0) {
      fprintf(stderr, "error from writei: %s\n", snd_strerror(rc));
    }  else if (rc != (int)frames) {
      fprintf(stderr, "short write, write %d frames\n", rc);
    }
  }
  close(fd);

  // free audio resources
  snd_pcm_drain(handle);
  snd_pcm_close(handle);
  free(buffer);
}
