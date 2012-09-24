#include <stdint.h>

enum ev_file_status_code {
  EV_FILE_ST_DONE,
  /* See union in struct ev_file_status for actions to take for these. */
  EV_FILE_ST_STREAM_BYTES,
  /* Error codes. */
  EV_FILE_ST_EUNSPC = -1,
  EV_FILE_ST_ENOENTRY = -2,
  EV_FILE_ST_EIOERR = -3,
  EV_FILE_ST_EBADFS = -4,                      /* FAT file system not found */
  EV_FILE_ST_ENAME = -5                        /* Bad file name */
};

struct ev_file_status {
  union {
    /*
      Action to take for EV_FILE_ST_STREAM_BYTES: read sector SEC, and stream
      LEN bytes starting at offset OFFSET from start of sector, by passing each
      byte to the callback ev_file_stream_bytes(). Requested bytes will not
      span a 512-byte sector boundary.
    */
    struct {
      uint32_t sec;
      uint16_t offset;
      uint16_t len;
    } st_stream_bytes;
    /*
      When ev_file_get_first_block() or ev_file_get_next_block() return
      EV_FILE_ST_DONE, then this holds the requested sector number.
      File length in bytes is also available.
    */
    struct {
      uint32_t sector;
      uint32_t length;
    } st_get_block_done;
  };

  /* The rest of this struct is internal state used by the library. */

  /* Current cluster of current file. */
  uint32_t file_cluster;
  /* The sector number of the location of the FAT. */
  uint32_t fat_first_sector;
  /* The sector number of the first data cluster. */
  uint32_t data_first_sector;
  /* Cluster (or sector if FL_FIXED_SIZE) where root directory starts. */
  uint32_t root_dir_start;
  /* Total number of clusters in file system. */
  uint32_t number_of_clusters;
  /* The size of a cluster, in sectors. */
  uint16_t cluster_size;
  /* Read pointer while streaming. */
  uint16_t idx;
  /* Number of entries in root dir (FAT12/FAT16 only). */
  uint16_t num_rootdir_entries;
  /* State of FAT reader state machine. */
  uint8_t state;
  /*
    Flags:
      bit 0/1  FL_FAT12, FL_FAT16, or FL_FAT32
      FL_FIXED_DIR  Root dir is fixed location and size
  */
  uint8_t flags;
  union {
    /*
      Temporary state for searching for FAT file system.
      Used in states ST_STREAM_BLOCK_0 and ST_STREAM_PART_BLOCK_0
    */
    struct {
      /* LBA of partition 1 start, if reading partition table. */
      uint32_t partition_start_lba;
      /* Number of sectors in one FAT. */
      uint32_t fat_sector_size;
      /* Number of sectors in file system. */
      uint32_t number_of_sectors;
      /* Reserved sectors. */
      uint16_t reserved_sectors;
      /* Number of FATs. */
      uint8_t number_of_fats;
      /*
        FL_FAT_NOFAT16  bad FAT12/16 0x46 0x41 type word.
        FL_FAT_NOFAT32  bad FAT32 0x46 0x41 type word.
        FL_FAT_NO55AA   bad 0x55 0xAA signature.
        FL_FAT_PART1    partition 1 present.
        FL_FAT_SECCNT32 use 32-bit number-of-sectors-in-filesystem.
        FL_FAT_FATSZ32  use 32-bit number-of-sectors-in-one-FAT.
      */
      uint8_t flags;
    } locate_fat;

    /*
      Temporary structure for searching for file in directory.
      Used in state ST_STREAM_DIR.
    */
    struct {
      /* Current cluster of dir being read. */
      uint32_t cur_cluster;
      /* First cluster of file. */
      uint32_t start_cluster;
      /* Length of file. */
      uint32_t file_length;
      /* Directory entries remaining to be read. */
      uint8_t remain_entries_in_dir;
      /* Index in cluster of sector being read. */
      uint8_t cur_sector;
      /*
        Flags:
          FL_DIR_DONE    set when done with this sector.
          FL_DIR_FOUND   set if file name found.
          FL_DIR_NOMATCH set when current entry does not match
          FL_DIR_END     set when finished scanning (found or reached EOF).
      */
      uint8_t flags;
      /* Name to search for, padded with space and upper-cased. */
      char name[11];
    } locate_dir;
  };
};

/*
  Open a named file in root dir of FAT file system.
  Before calling, st->state must be initialised to 0.
  Then the function must be repeatedly called until it returns
  EV_FILE_ST_DONE or negative error code EV_FILE_ST_E*.

  The returned status tells the next action to take, see comments in struct
  ev_file_status for details.

  When EV_FILE_ST_DONE is returned, the first sector of the file, and the
  length in bytes of the file, is returned in st->st_get_block_done.
*/
int
ev_file_get_first_block(const char *filename, struct ev_file_status *st);

/*
  After opening a file, this finds the next sector in the file. When calling
  this function, st->st_get_block_done must be set to / retain the value set
  by the previous call to ev_file_get_first_block() /
  ev_file_get_next_block().  After EV_FILE_ST_DONE is returned the new sector
  number is then found in st->st_get_block_done.
*/
int
ev_file_get_next_block(struct ev_file_status *st);

/*
  This callback is used to stream bytes read as a response to a request
  EV_FILE_ST_STREAM_BYTES. Each byte requested must be passed in, in
  sequence. The return value is true if no more data needs to be streamed;
  in this case it is permissible, but not required, to stop the read early
  and not stream the rest of the requested bytes.
*/
int
ev_file_stream_bytes(uint8_t byte_read, struct ev_file_status *st);
