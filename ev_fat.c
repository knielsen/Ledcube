#include "ev_fat.h"

#ifdef ENABLE_DEBUG_PRINT
#include <stdio.h>
#define DEBUG(x) printf x
#else
#define DEBUG(x) do { } while(0)
#endif

/* Internal state machine states. */
enum {
  ST_STARTING = 0,
  ST_STREAM_BLOCK_0,
  ST_STREAM_PART_BLOCK_0,
  ST_FIND_IN_DIR,
  ST_STREAM_DIR,
  ST_LOOKUP_DIR_FAT,
  ST_NEXT_CLUSTER_DIR,
  ST_LOOKUP_FILE_FAT
};

/* Internal flag values. */

#define FL_TYPEMASK 3
#define FL_FAT12 0
#define FL_FAT16 1
#define FL_FAT32 2
#define FL_FIXED_DIR 4

#define FL_FAT_NOFAT16   1
#define FL_FAT_NOFAT32   2
#define FL_FAT_NO55AA    4
#define FL_FAT_PART1     8
#define FL_FAT_SECCNT32 16
#define FL_FAT_FATSZ32  32

#define FL_DIR_DONE    1
#define FL_DIR_FOUND   2
#define FL_DIR_NOMATCH 4
#define FL_DIR_END     8

static uint32_t
clust2sect(uint32_t cluster, struct ev_file_status *st)
{
  if (cluster < 2)
    return 0;
  cluster -= 2;
  if (cluster >= st->number_of_clusters)
    return 0;
  return st->data_first_sector + st->cluster_size*cluster;
}


static int
prep_read_fat_entry(uint32_t clust, uint8_t state, struct ev_file_status *st)
{
  uint32_t sec;
  uint16_t offset;
  uint8_t len;

  switch (st->flags & FL_TYPEMASK)
  {
  case FL_FAT12:
    /*
      FAT12 is a bit annoying, as FAT entries can span a sector boundary.

      ToDo.
    */
    return EV_FILE_ST_EUNSPC;
  case FL_FAT16:
    sec = clust / 256;
    offset = (clust % 256) * 2;
    len = 2;
    break;
  case FL_FAT32:
    sec = clust / 128;
    offset = (clust % 128) * 4;
    len = 4;
    break;
  default:
    /* NotReached */
    return EV_FILE_ST_EUNSPC;
  }

  /* Read in just the couple bytes needed for one FAT entry. */
  st->st_stream_bytes.sec = st->fat_first_sector + sec;
  st->st_stream_bytes.offset = offset;
  st->st_stream_bytes.len = len;

  st->idx = 0;
  st->locate_dir.cur_cluster = 0;
  st->state = state;
  return EV_FILE_ST_STREAM_BYTES;
}


int
ev_file_get_first_block(const char *filename, struct ev_file_status *st)
{
  uint8_t si, di;
  char c;
  uint32_t clust;

  /*
    Reset st->state here by default, so we can just return error code in
    all error cases without worrying about forgetting to reset state
    in some corner case.
  */
  uint8_t state = st->state;
  st->state = ST_STARTING;

  switch (state)
  {
  case ST_STARTING:
    /*
      First we need to read sector 0 to look for the root of a FAT file
      system, or possibly a BIOS partition table.
      (We don't strictly read the first few bytes, but it seems cleaner to
      just read a full sector).
    */
    st->st_stream_bytes.sec = 0;
    st->st_stream_bytes.offset = 0;
    st->st_stream_bytes.len = 512;
    st->state = ST_STREAM_BLOCK_0;
    st->idx = 0;
    st->locate_fat.flags = 0;
    DEBUG(("Start reading FAT\n"));
    return EV_FILE_ST_STREAM_BYTES;

  case ST_STREAM_BLOCK_0:
    if (st->locate_fat.flags & FL_FAT_NO55AA)
      return EV_FILE_ST_EBADFS;
    /*
      We've read the first block on the device.

      Check if it is a FAT file system; if not check if it is a BIOS partition
      table and try the first partition.
    */
    if ((st->locate_fat.flags & (FL_FAT_NOFAT16|FL_FAT_NOFAT32)) ==
        (FL_FAT_NOFAT16|FL_FAT_NOFAT32))
    {
      /*
        Not a FAT file system, try find partition 0 in a BIOS
        partition table.
      */
      DEBUG(("Not FAT in sector 0, try first BIOS partion\n"));
      if (!(st->locate_fat.flags & FL_FAT_PART1))
        return EV_FILE_ST_EBADFS;
      /*
        Save partition start temporarily, as partition_start_lba will be
        overwritten during the next sector read.
      */
      st->fat_first_sector = st->locate_fat.partition_start_lba;
      st->st_stream_bytes.sec = st->locate_fat.partition_start_lba;
      st->st_stream_bytes.offset = 0;
      st->st_stream_bytes.len = 512;
      st->state = ST_STREAM_PART_BLOCK_0;
      st->idx = 0;
      st->locate_fat.flags = 0;
      return EV_FILE_ST_STREAM_BYTES;
    }
    DEBUG(("Using FAT file system in sector 0\n"));
    /* Fallthrough. */

  case ST_STREAM_PART_BLOCK_0:
    if ((st->locate_fat.flags & FL_FAT_NO55AA) ||
        (st->locate_fat.flags & (FL_FAT_NOFAT16|FL_FAT_NOFAT32)) ==
          (FL_FAT_NOFAT16|FL_FAT_NOFAT32))
      return EV_FILE_ST_EBADFS;

    /*
      We found something that has the signatures to look like a FAT file system.

      Now do some sanity checks on the read data, and load it into the permanent
      part of our structure if it looks ok.
    */
    uint32_t base_sector = (state == ST_STREAM_PART_BLOCK_0 ?
                            st->fat_first_sector : 0);
    uint32_t remain_sectors = st->locate_fat.number_of_sectors;

    if (st->locate_fat.reserved_sectors >= remain_sectors)
      return EV_FILE_ST_EBADFS;
    remain_sectors -= st->locate_fat.reserved_sectors;
    st->fat_first_sector = base_sector + st->locate_fat.reserved_sectors;

    uint32_t fat_tot_size =
      st->locate_fat.fat_sector_size * st->locate_fat.number_of_fats;
    if (fat_tot_size >= remain_sectors)
      return EV_FILE_ST_EBADFS;
    remain_sectors -= fat_tot_size;
    uint16_t num_rootdir_sectors = (st->num_rootdir_entries + 15)/16;
    if (num_rootdir_sectors >= remain_sectors)
      return EV_FILE_ST_EBADFS;
    remain_sectors -= num_rootdir_sectors;
    st->number_of_clusters = remain_sectors / st->cluster_size;

    st->data_first_sector =
      st->fat_first_sector + fat_tot_size + num_rootdir_sectors;

    if (st->number_of_clusters >= 0xfff5)
      st->flags = FL_FAT32;
    else if (st->number_of_clusters >= 0xff5)
      st->flags = FL_FAT16;
    else
      st->flags = FL_FAT12;
    DEBUG(("Found %s in sector %u\n", st->flags == FL_FAT32 ? "FAT32" :
           (st->flags == FL_FAT16 ? "FAT16" : "FAT12"), base_sector));

    /* For FAT12/FAT16, root dir is of fixed size, just after the FAT. */
    if ((st->flags & FL_TYPEMASK) != FL_FAT32)
    {
      st->root_dir_start = st->fat_first_sector + fat_tot_size;
      st->flags |= FL_FIXED_DIR;
    }
    DEBUG(("#sectors=%u #clusters=%u FAT size=%u rootdir at %u\n",
           st->locate_fat.number_of_sectors, st->number_of_clusters,
           st->locate_fat.fat_sector_size, st->root_dir_start));

    state = ST_FIND_IN_DIR;
    /* Fall through ...*/

  case ST_FIND_IN_DIR:
    /* Read the first sector of the directory, looking for the file name. */
    si = 0;
    di = 0;

    /* Copy over file name, padding and uppercasing for easy comparison. */
    for (;;)
    {
      c = filename[si++];
      if (c == '\0' || c == '.')
        break;
      if (di >= 8)
        return EV_FILE_ST_ENAME;
      if (c >= 'a' && c <= 'z')
        c = c - ('a' - 'A');
      st->locate_dir.name[di++] = c;
    }
    while (di < 8)
      st->locate_dir.name[di++] = ' ';
    if (c)
    {
      for (;;)
      {
        c = filename[si++];
        if (c == '\0')
          break;
        if (di >= 11 || c == '.')
          return EV_FILE_ST_ENAME;
      if (c >= 'a' && c <= 'z')
        c = c - ('a' - 'A');
        st->locate_dir.name[di++] = c;
      }
    }
    while (di < 11)
      st->locate_dir.name[di++] = ' ';

    st->idx = 0;
    st->locate_dir.remain_entries_in_dir = 16;
    st->locate_dir.flags = 0;
    st->locate_dir.cur_sector = 0;
    if (st->flags & FL_FIXED_DIR)
      st->st_stream_bytes.sec = st->root_dir_start;
    else
    {
      st->locate_dir.cur_cluster = st->root_dir_start;
      st->st_stream_bytes.sec = clust2sect(st->locate_dir.cur_cluster, st);
    }
    st->st_stream_bytes.offset = 0;
    st->st_stream_bytes.len = 512;
    st->state = ST_STREAM_DIR;
    return EV_FILE_ST_STREAM_BYTES;

  case ST_STREAM_DIR:
    /*
      We read and scanned a directory entry.

      Check if we found what we were looking for, else read the FAT to
      find the next directory entry, if any.
    */
    if (st->locate_dir.flags & FL_DIR_FOUND)
    {
      /* Found it! */
      st->file_cluster = st->locate_dir.start_cluster;
      st->st_get_block_done.length = st->locate_dir.file_length;
      st->st_get_block_done.sector = clust2sect(st->file_cluster, st);
      DEBUG(("File found! sector=%u len=%u cluster=%u\n",
             st->st_get_block_done.sector, st->locate_dir.file_length,
             st->locate_dir.start_cluster));
      return EV_FILE_ST_DONE;
    }

    if (st->locate_dir.flags & FL_DIR_END)
    {
      /* Reached the end without finding anything. */
      DEBUG(("Reached end of directory without finding file.\n"));
      return EV_FILE_ST_ENOENTRY;
    }

    /* We need to go scan the next sector of the directory, if any. */
    ++st->locate_dir.cur_sector;
    if (st->locate_dir.cur_sector < (st->flags & FL_FIXED_DIR ?
            st->num_rootdir_entries : st->cluster_size))
    {
      /* Scan the next sector in this cluster. */
      st->idx = 0;
      st->locate_dir.remain_entries_in_dir = 16;
      st->locate_dir.flags = 0;

      st->st_stream_bytes.sec = (st->flags & FL_FIXED_DIR ? st->root_dir_start :
        clust2sect(st->locate_dir.cur_cluster, st)) + st->locate_dir.cur_sector;
      st->st_stream_bytes.offset = 0;
      st->st_stream_bytes.len = 512;
      st->state = ST_STREAM_DIR;
      return EV_FILE_ST_STREAM_BYTES;
    }

    /* Read FAT entry to get next cluster. */
    state = ST_NEXT_CLUSTER_DIR;
    /* Fall through ... */

  case ST_NEXT_CLUSTER_DIR:
    clust = st->locate_dir.cur_cluster;

    if (clust == 0)
      return EV_FILE_ST_ENOENTRY;
    if (clust < 2 || clust >= st->number_of_clusters + 2)
      return EV_FILE_ST_EBADFS;

    /*
      Now compute the sector(s) we need to read to get the FAT entry, as
      well as the offset/length.
    */
    return prep_read_fat_entry(clust, ST_LOOKUP_DIR_FAT, st);

  case ST_LOOKUP_DIR_FAT:
    if (st->locate_dir.cur_cluster >= (uint32_t)0xfffffff8)
    {
      /* End of directory reached. */
      return EV_FILE_ST_ENOENTRY;
    }

    if (st->locate_dir.cur_cluster < 2 ||
        st->locate_dir.cur_cluster - 2 >= st->number_of_clusters)
      return EV_FILE_ST_EBADFS;

    /* Now go read the first sector of the next cluster in directory. */
    st->idx = 0;
    st->locate_dir.remain_entries_in_dir = 16;
    st->locate_dir.flags = 0;

    st->st_stream_bytes.sec = clust2sect(st->locate_dir.cur_cluster, st);
    st->st_stream_bytes.offset = 0;
    st->st_stream_bytes.len = 512;
    st->state = ST_STREAM_DIR;
    return EV_FILE_ST_STREAM_BYTES;

  default:
    return EV_FILE_ST_EUNSPC;
  };
}


int
ev_file_get_next_block(struct ev_file_status *st)
{
  uint32_t sector_offset, clust;

  /*
    Reset st->state here by default, so we can just return error code in
    all error cases without worrying about forgetting to reset state
    in some corner case.
  */
  uint8_t state = st->state;
  st->state = ST_STARTING;

  switch (state)
  {
  case ST_STARTING:
    ++st->st_get_block_done.sector;
    /* Figure out if we need to cross a cluster boundary. */
    sector_offset = st->st_get_block_done.sector - st->data_first_sector;
    if (sector_offset % st->cluster_size)
      return EV_FILE_ST_DONE;

    /* Load next cluster entry from FAT. */
    clust = st->file_cluster;

    if (clust < 2 || clust >= st->number_of_clusters + 2)
      return EV_FILE_ST_EUNSPC;
    return prep_read_fat_entry(clust, ST_LOOKUP_FILE_FAT, st);

  case ST_LOOKUP_FILE_FAT:
    st->file_cluster = st->locate_dir.cur_cluster;
    st->st_get_block_done.sector = clust2sect(st->file_cluster, st);
    return EV_FILE_ST_DONE;

  default:
    return EV_FILE_ST_EUNSPC;
  };
}


int
ev_file_stream_bytes(uint8_t b, struct ev_file_status *st)
{
  uint8_t idx;

  switch (st->state)
  {
  case ST_STREAM_BLOCK_0:
  case ST_STREAM_PART_BLOCK_0:
    /*
      We are reading first block of device, looking for FAT file system or
      partition table.

      We want to see 0x55 0xAA at the end of the sector. In addition, we want
      to see 0x46 0x41 at either offset 54 (FAT12/FAT16) or 82 (FAT82).

      We set corresponding flags if we do not find the correct bytes.
    */
    switch (st->idx++)
    {
    case 13:
      /* Sectors per cluster. */
      st->cluster_size = (uint16_t)b;
      return 0;
    case 14:
      /* Reserved sectors low byte. */
      st->locate_fat.reserved_sectors = b;
      return 0;
    case 15:
      /* Reserved sectors high byte. */
      st->locate_fat.reserved_sectors |= ((uint16_t)b << 8);
      return 0;
    case 16:
      /* Number of FATs. */
      st->locate_fat.number_of_fats = b;
      return 0;
    case 17:
      /* Number of entries in the root directory, for FAT12/FAT16. */
      st->num_rootdir_entries = b;
      return 0;
    case 18:
      st->num_rootdir_entries |= ((uint16_t)b << 8);
      return 0;
    case 19:
      /* 16-bit number of sectors in file system, or 0 to use 32-bit count. */
      st->locate_fat.number_of_sectors = b;
      return 0;
    case 20:
      st->locate_fat.number_of_sectors |= ((uint16_t)b << 8);
      if (!st->locate_fat.number_of_sectors)
        st->locate_fat.flags |= FL_FAT_SECCNT32;
      return 0;
    case 22:
      /* 16-bit number of sectors in one FAT, or 0 to use 32-bit count. */
      st->locate_fat.fat_sector_size = b;
      return 0;
    case 23:
      st->locate_fat.fat_sector_size |= ((uint16_t)b << 8);
      if (!st->locate_fat.fat_sector_size)
        st->locate_fat.flags |= FL_FAT_FATSZ32;
      return 0;
    case 32:
      /* 32-bit number of sectors in file system, if 16-bit count is zero. */
      if (st->locate_fat.flags & FL_FAT_SECCNT32)
        st->locate_fat.number_of_sectors = b;
      return 0;
    case 33:
      if (st->locate_fat.flags & FL_FAT_SECCNT32)
        st->locate_fat.number_of_sectors |= ((uint16_t)b << 8);
      return 0;
    case 34:
      if (st->locate_fat.flags & FL_FAT_SECCNT32)
        st->locate_fat.number_of_sectors |= ((uint32_t)b << 16);
      return 0;
    case 35:
      if (st->locate_fat.flags & FL_FAT_SECCNT32)
        st->locate_fat.number_of_sectors |= ((uint32_t)b << 24);
      return 0;
    case 36:
      /* 32-bit number of sectors in one FAT, if 16-bit count is zero. */
      if (st->locate_fat.flags & FL_FAT_FATSZ32)
        st->locate_fat.fat_sector_size = b;
      return 0;
    case 37:
      if (st->locate_fat.flags & FL_FAT_FATSZ32)
        st->locate_fat.fat_sector_size |= ((uint16_t)b << 8);
      return 0;
    case 38:
      if (st->locate_fat.flags & FL_FAT_FATSZ32)
        st->locate_fat.fat_sector_size |= ((uint32_t)b << 16);
      return 0;
    case 39:
      if (st->locate_fat.flags & FL_FAT_FATSZ32)
        st->locate_fat.fat_sector_size |= ((uint32_t)b << 24);
      return 0;
    case 44:
      /* Root directory start cluster, if FAT32. */
      st->root_dir_start = b;
      return 0;
    case 45:
      st->root_dir_start |= ((uint16_t)b << 8);
      return 0;
    case 46:
      st->root_dir_start |= ((uint32_t)b << 16);
      return 0;
    case 47:
      st->root_dir_start |= ((uint32_t)b << 24);
      return 0;
    case 54:
      if (b != 0x46)
        st->locate_fat.flags |= FL_FAT_NOFAT16;
      return 0;
    case 55:
      if (b != 0x41)
        st->locate_fat.flags |= FL_FAT_NOFAT16;
      return 0;
    case 82:
      if (b != 0x46)
        st->locate_fat.flags |= FL_FAT_NOFAT32;
      return 0;
    case 83:
      if (b != 0x41)
        st->locate_fat.flags |= FL_FAT_NOFAT32;
      return 0;
    case 446+4:
      /* Non-zero in entry 1 in partition table means partition 1 present. */
      st->locate_fat.flags |= FL_FAT_PART1;
      return 0;
    case 446+8:
      st->locate_fat.partition_start_lba = b;
      return 0;
    case 446+8+1:
      st->locate_fat.partition_start_lba |= ((uint16_t)b << 8);
      return 0;
    case 446+8+2:
      st->locate_fat.partition_start_lba |= ((uint32_t)b << 16);
      return 0;
    case 446+8+3:
      st->locate_fat.partition_start_lba |= ((uint32_t)b << 24);
      return 0;
    case 510:
      if (b != 0x55)
      {
        st->locate_fat.flags |= FL_FAT_NO55AA;
        return 1;
      }
      else
        return 0;
    case 511:
      if (b != 0xaa)
        st->locate_fat.flags |= FL_FAT_NO55AA;
      return 1;
    default:
      return 0;
    }
    /* NotReached */
    break;

  case ST_STREAM_DIR:
    if (st->locate_dir.flags & FL_DIR_DONE)
      return 1;                                 /* Already done. */

    idx = st->idx++;

    /* Just skip entry if we already know that it does not match. */
    if (idx != 31 && (st->locate_dir.flags & FL_DIR_NOMATCH))
      return 0;

    switch (idx)
    {
    case 11:
      /*
        Attributes.
        Skip directory (bit 4 set) and long filename entry (bit 0-3 set).
      */
      if ((b & 0x10) || (b & 0x0f) == 0x0f)
        st->locate_dir.flags |= FL_DIR_NOMATCH;
      return 0;
    case 20:
      /* Start cluster high 16 bit. */
      st->locate_dir.start_cluster = (uint32_t)b << 16;
      return 0;
    case 21:
      st->locate_dir.start_cluster |= (uint32_t)b << 24;
      return 0;
    case 26:
      /* Start cluster low 16 bit. */
      st->locate_dir.start_cluster |= b;
      return 0;
    case 27:
      st->locate_dir.start_cluster |= (uint16_t)b << 8;
      return 0;
    case 28:
      /* File length. */
      st->locate_dir.file_length = b;
      return 0;
    case 29:
      st->locate_dir.file_length |= (uint16_t)b << 8;
      return 0;
    case 30:
      st->locate_dir.file_length |= (uint32_t)b << 16;
      return 0;
    case 31:
      if (!(st->locate_dir.flags & FL_DIR_NOMATCH))
      {
        /* We found the correct entry. */
        st->locate_dir.file_length |= (uint32_t)b << 24;
        st->locate_dir.flags |= (FL_DIR_DONE|FL_DIR_FOUND|FL_DIR_END);
        return 1;
      }

      /* Try the next entry. */
      st->idx = 0;
      if (--st->locate_dir.remain_entries_in_dir > 0)
      {
        st->locate_dir.flags = 0;
        return 0;
      }

      /* We have read everything in this sector. */
      st->locate_dir.flags |= FL_DIR_DONE;
      return 1;
    case 0:
      /* Check for end-of-directory. */
      if (b == 0)
      {
        st->locate_dir.flags |= (FL_DIR_DONE|FL_DIR_NOMATCH|FL_DIR_END);
        return 1;
      }

      /* Check for unused entry. */
      if (b == 0xe5)
      {
        st->locate_dir.flags |= FL_DIR_NOMATCH;
        return 0;
      }
      /* Fall through to compare against search name ... */
    default:
      /* Compare 11 characters in name. */
      if (idx < 11 && b != st->locate_dir.name[idx])
        st->locate_dir.flags |= FL_DIR_NOMATCH;
      return 0;
    }

  case ST_LOOKUP_DIR_FAT:
  case ST_LOOKUP_FILE_FAT:
    idx = st->idx++;
    if ((st->flags & FL_TYPEMASK) == FL_FAT32)
    {
      st->locate_dir.cur_cluster |= (uint32_t)b << (idx*8);
      return idx == 3;
    }
    if ((st->flags & FL_TYPEMASK) == FL_FAT16)
    {
      st->locate_dir.cur_cluster |= (uint16_t)b << (idx*8);
      return idx == 1;
    }
    /* ToDo: FAT12. */
    return 1;

  default:
    /* NotReached */
    return EV_FILE_ST_EUNSPC;
  }
  /*
    No return statement here, so compiler will warn if we forget a return
    statement in some case above.
  */
}
