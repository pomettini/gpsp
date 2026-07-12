/* Playdate implementation of the tiny slice of libretro-common's
 * file_stream API that the gpSP core uses (ROM paging in gba_memory.c).
 * Backed by pd->file so the core stays untouched; libretro-common's own
 * stdio implementation is not compiled on Playdate. */

#include <string.h>

#include "pd_api.h"
#include "streams/file_stream.h"

struct RFILE
{
  SDFile *f;
  int64_t size;
};

static PlaydateAPI *pd_fs;

/* Single open file is all the core needs (the gamepak). Avoids malloc in
 * the I/O path; bump if a second concurrent stream ever shows up. */
#define PD_FS_MAX_STREAMS 2
static struct RFILE stream_pool[PD_FS_MAX_STREAMS];

void pd_filestream_init(PlaydateAPI *pd)
{
  pd_fs = pd;
}

RFILE *filestream_open(const char *path, unsigned mode, unsigned hints)
{
  SDFile *f;
  int i;
  FileStat st;

  (void)hints;
  if (!pd_fs || mode != RETRO_VFS_FILE_ACCESS_READ)
    return NULL;

  /* Data folder first (ROM picker paths live there), then the pdx bundle
   * (bundled Source/test.gba fallback). */
  f = pd_fs->file->open(path, kFileReadData);
  if (!f)
    f = pd_fs->file->open(path, kFileRead);
  if (!f)
    return NULL;

  for (i = 0; i < PD_FS_MAX_STREAMS; i++)
  {
    if (!stream_pool[i].f)
    {
      stream_pool[i].f = f;
      stream_pool[i].size = -1;
      if (pd_fs->file->stat(path, &st) == 0)
        stream_pool[i].size = (int64_t)st.size;
      return &stream_pool[i];
    }
  }

  pd_fs->file->close(f);
  return NULL;
}

int64_t filestream_get_size(RFILE *stream)
{
  if (!stream)
    return -1;
  return stream->size;
}

int64_t filestream_seek(RFILE *stream, int64_t offset, int seek_position)
{
  int whence;
  if (!stream)
    return -1;

  switch (seek_position)
  {
    case RETRO_VFS_SEEK_POSITION_START:   whence = SEEK_SET; break;
    case RETRO_VFS_SEEK_POSITION_CURRENT: whence = SEEK_CUR; break;
    case RETRO_VFS_SEEK_POSITION_END:     whence = SEEK_END; break;
    default: return -1;
  }

  if (pd_fs->file->seek(stream->f, (int)offset, whence) != 0)
    return -1;
  return pd_fs->file->tell(stream->f);
}

int64_t filestream_read(RFILE *stream, void *data, int64_t len)
{
  unsigned char *dst = (unsigned char *)data;
  int64_t total = 0;

  if (!stream || len < 0)
    return -1;

  while (total < len)
  {
    int got = pd_fs->file->read(stream->f, dst + total,
                                (unsigned int)(len - total));
    if (got <= 0)
      break;
    total += got;
  }
  return total;
}

int64_t filestream_tell(RFILE *stream)
{
  if (!stream)
    return -1;
  return pd_fs->file->tell(stream->f);
}

int filestream_close(RFILE *stream)
{
  int ret;
  if (!stream || !stream->f)
    return -1;
  ret = pd_fs->file->close(stream->f);
  stream->f = NULL;
  stream->size = -1;
  return ret;
}
