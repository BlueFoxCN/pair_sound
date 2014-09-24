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

  char *buffer;

  while (true) {
    sleep(5);
    char *l = get_local_ip("wlan0");
    char local_ip[100];
    strcpy(local_ip, l);
    // if (strcmp(local_ip, "0.0.0.0") == 0) {
    //   continue;
    // }
    // get gateway ip address
    // char *gateway_ip = get_gateway_ip_from_local_ip(local_ip, sizeof(local_ip));
    // char real_gateway_ip[100];
    // sprintf(real_gateway_ip, "%s\0", gateway_ip);
    char real_gateway_ip[100] = "192.168.1.25";

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
    /* Open PCM device for recording (capture). */
    rc = snd_pcm_open(&handle, "hw:0,0",
            SND_PCM_STREAM_CAPTURE, 0);
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

    /* 48000 bits/second sampling rate (CD quality) */
    val = 48000;
    snd_pcm_hw_params_set_rate_near(handle, params, &val, &dir);

    /* Set period size to 160 frames. */
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

      // if (send(socket_desc, buffer, frames, 0) < 0) {
      if (sendto(socket_src, buffer, size, 0, (struct sockaddr*)&server, sizeof(server)) < 0) {
        break;
      }
    }

    free(buffer);
    close(socket_src);

    snd_pcm_drain(handle);
    snd_pcm_close(handle);
  }
}
