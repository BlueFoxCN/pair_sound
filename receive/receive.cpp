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
  FILE *fp;
  // char *buffer;
  char *buffer;
  int channel_num = 1;

  /* Open PCM device for playback. */
  rc = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
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
  val = 48000;
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
  addr.sin_addr.s_addr=inet_addr("192.168.0.1");
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

  fp = fopen("t.wav", "r");

  while (true) {
    // r = recvfrom(fd, buffer, size, 0, (struct sockaddr*)&from, &len);
    fread(buffer, sizeof(char), size, fp);

    // fwrite(buffer, sizeof(char), size, fp);

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
