#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "ev_fat.h"

static int fd;

static void
dump_buffer(uint8_t *buf, uint32_t max_bytes)
{
  int i, j;

  for (i = 0; i < 32; ++i)
  {
    if (i*16 >= max_bytes)
      break;
    printf("0x%03x ", (unsigned)i*16);
    for (j = 0; j < 16; ++j)
    {
      if (i*16+j >= max_bytes)
        printf("   ");
      else
        printf(" %02X", buf[i*16+j]);
    }
    printf("  ");
    for (j = 0; j < 16; ++j)
    {
      if (i*16+j >= max_bytes)
        printf(" ");
      else
        printf("%c", isprint(buf[i*16+j]) ? buf[i*16+j] : '.');
    }
    printf("\n");
  }
}

static void
dump_sector(uint32_t sector, uint32_t max_bytes)
{
  uint8_t buf[512];
  int res;

  res = pread(fd, buf, 512, (off_t)sector * 512);
  if (res != 512)
  {
    fprintf(stderr, "Error: cannot dump sector %u: %d: %s\n", (unsigned)sector,
            errno, strerror(errno));
    exit(1);
  }

  //fflush(stdout); write(1, buf, 512); return;
  printf("\nSector: %u\n", (unsigned)sector);
  dump_buffer(buf, max_bytes);
}


static void
handle_res(int res, struct ev_file_status *st)
{
  uint8_t buf[512];
  unsigned i;

  if (res == EV_FILE_ST_DONE)
    return;
  if (res < 0)
  {
    fprintf(stderr, "Got error %d\n", res);
    exit(1);
  }
  if (res != EV_FILE_ST_STREAM_BYTES)
  {
    fprintf(stderr, "Got unimplemented action %d\n", res);
    exit(1);
  }

  printf("!!! Read(%u, %u, %u)\n", (unsigned)st->st_stream_bytes.sec,
         (unsigned)st->st_stream_bytes.offset,
         (unsigned)st->st_stream_bytes.len);

  res = pread(fd, buf, st->st_stream_bytes.len,
              st->st_stream_bytes.sec * 512 + st->st_stream_bytes.offset);
  if (res != (int)(st->st_stream_bytes.len))
  {
    fprintf(stderr, "Error reading file sector %u off %u len %u: "
            "res=%d errno=%d %s\n", (unsigned)st->st_stream_bytes.sec,
            (unsigned)st->st_stream_bytes.offset,
            (unsigned)st->st_stream_bytes.len, res, errno, strerror(errno));
    exit(1);
  }
  dump_buffer(buf, 512);
  for (i = 0; i < st->st_stream_bytes.len; ++i)
    if (ev_file_stream_bytes(buf[i], st))
      break;
}


int
main(int argc, char *argv[])
{
  int res;
  struct ev_file_status st;
  uint32_t remain;

  fd = open("/kvm/sdcard/phone-sdcard.img", O_RDONLY);
  if (fd < 0)
  {
    fprintf(stderr, "Error: cannot open FAT image: %d: %s\n",
            errno, strerror(errno));
    exit(1);
  }

  memset(&st, 0, sizeof(st));
  do {
    res = ev_file_get_first_block("LED-CUBE.000", &st);
    //res = ev_file_get_first_block("led-cube.001", &st);
    handle_res(res, &st);
  } while (res != EV_FILE_ST_DONE);

  remain = st.st_get_block_done.length;
  for (;;)
  {
    dump_sector(st.st_get_block_done.sector, remain);
    if (remain <= 512)
      break;
    remain -= 512;

    do {
      res = ev_file_get_next_block(&st);
      handle_res(res, &st);
    } while (res != EV_FILE_ST_DONE);
  }
  return 0;
}

