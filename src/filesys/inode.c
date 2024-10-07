#include "filesys/inode.h"
#include <stdio.h>
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define INODE_TABLE_LENGTH 128


static char zeros[BLOCK_SECTOR_SIZE];


/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

int32_t
compare_substitute(int32_t a, int32_t b)
{
  int32_t ret = 0;
  if(a < b)
      ret = a;
  else
      ret = b;
  return ret;
}

int
get_direct_i(off_t pos)
{
  int direct_block_i = pos / BLOCK_SECTOR_SIZE;
  direct_block_i = direct_block_i < 8 ? direct_block_i : -1;
  return direct_block_i;
}
/********************** END NEW CODE *************************/

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
 if (pos >= inode->data.length) return -1;

  if (pos < DIRECT_PTR_NUM * BLOCK_SECTOR_SIZE){
    int direct_block_entry = pos / BLOCK_SECTOR_SIZE;
    return inode->data.direct_blocks[direct_block_entry];
  }else if(pos < DIRECT_PTR_NUM * BLOCK_SECTOR_SIZE + INDIRECT_PTR_NUM * INODE_TABLE_LENGTH * BLOCK_SECTOR_SIZE){
    int indirect_table_entry = (pos - DIRECT_PTR_NUM * BLOCK_SECTOR_SIZE) / (INODE_TABLE_LENGTH * BLOCK_SECTOR_SIZE);
    int table_entry = (pos - DIRECT_PTR_NUM * BLOCK_SECTOR_SIZE - indirect_table_entry * INODE_TABLE_LENGTH * BLOCK_SECTOR_SIZE) / BLOCK_SECTOR_SIZE;

    block_sector_t * table = (block_sector_t *)malloc(INODE_TABLE_LENGTH * sizeof(block_sector_t *));
    memset(table, 0, INODE_TABLE_LENGTH * sizeof(block_sector_t *));
    cache_read (inode->data.indirect_blocks[indirect_table_entry], table);

    block_sector_t result = table[table_entry];
    free (table);
    return result;
  }

  return -1;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static void
byte_to_sector_write (struct inode *inode, off_t pos) 
{
  block_sector_t result;
  if (pos < inode->data.length){
      result = byte_to_sector (inode, pos);
      return result;
    }else{
      size_t sector_end = bytes_to_sectors (inode->data.length);
      size_t sector_off = bytes_to_sectors (pos+1);
      if (sector_end < INODE_DIRECT_N){
          size_t sectors_direct_new = sector_off < INODE_DIRECT_N ? sector_off : INODE_DIRECT_N;
          for (size_t i=sector_end; i<sectors_direct_new; i++){
              free_map_allocate (1, &inode->data.direct_blocks[i]);
              cache_write (inode->data.direct_blocks[i], zeros);
            }
        }

      if (sector_off <= INODE_DIRECT_N){
          inode->data.length = pos+1;
          cache_write (inode->sector, &inode->data);
          return inode->data.direct_blocks[sector_off-1];
        }

    
       /* Calculating the corresponding numbers of indirect pointers
            indirect_sector_end is the part that in indirect block
            table_cur_num is the number of tables that the extra part needs
            indirect_sector off is the part to be write in the indirect block */
      size_t indirect_sector_end = sector_end > INODE_DIRECT_N ? sector_end-INODE_DIRECT_N : 0;
      size_t table_n_old = (indirect_sector_end+INODE_TABLE_LENGTH-1) / INODE_TABLE_LENGTH;  // how many tables
      size_t indirect_sector_off = sector_off - INODE_DIRECT_N;
      block_sector_t *table = calloc(INODE_TABLE_LENGTH, sizeof (block_sector_t*));
    /* We start from the sector number of the first in the indirect area, step is INODE_TABLE_LENGTH */
      for (size_t i= (indirect_sector_end/ INODE_TABLE_LENGTH)*INODE_TABLE_LENGTH; i<indirect_sector_off; i+=INODE_TABLE_LENGTH){
          size_t bytes_left_end;
          size_t indirect_table_entry = i / INODE_TABLE_LENGTH;
          /* Calculate bytes_left_end, which is the bytes that is left in the last block 
                set the whole block to 0 as well. */
          if (indirect_table_entry+1 > table_n_old) {
              if (!free_map_allocate (1, &inode->data.indirect_blocks[indirect_table_entry])) {
                  free (table);
                  return -1;
                }
              memset (table, 0, BLOCK_SECTOR_SIZE);
              bytes_left_end = 0;
            }
          else
            {

              cache_read (inode->data.indirect_blocks[indirect_table_entry], table);
              // struct read_ahead * read_ahead_unit = (struct read_ahead*)malloc(sizeof(struct read_ahead*));
                  // read_ahead_unit->block_to_read = i;
                  // read_ahead_unit->buffer = inode->data.indirect_pointer[indirect_table_entry + 1];
                  // list_push_back(&read_ahead_list, &read_ahead_unit->elem);
                  // cond_signal(&read_ahead_cond, &read_ahead_lock);
              bytes_left_end = indirect_sector_end % INODE_TABLE_LENGTH;
            }
          
          size_t n_table_entry = (indirect_sector_off-i) < INODE_TABLE_LENGTH ? (indirect_sector_off-i) : INODE_TABLE_LENGTH;
          
          for (size_t j=bytes_left_end; j<n_table_entry; j+=1){
              if (!free_map_allocate (1, &table[j])){
                  free (table);
                  return -1;
                }
              cache_write (table[j], zeros);
            }
          cache_write (inode->data.indirect_blocks[indirect_table_entry], table);
        }
      result = table[(indirect_sector_off-1)%INODE_TABLE_LENGTH];

      free (table);
    }
    inode->data.length = pos+1;

    cache_write (inode->sector, &inode->data);
    return result;
}
/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL){
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = false;
      size_t n_direct_blocks = sectors < INODE_DIRECT_N ? sectors : INODE_DIRECT_N;
      for (size_t i=0; i<n_direct_blocks; i++){
          if (!free_map_allocate (1, &disk_inode->direct_blocks[i]))
            {
              free (disk_inode);
              return false;
            }

          cache_write (disk_inode->direct_blocks[i], zeros);
        }

      if (sectors <= INODE_DIRECT_N){
          cache_write (sector, disk_inode);
          free (disk_inode);
          return true;
      }
      size_t n_indirect_blocks = sectors - INODE_DIRECT_N;
      block_sector_t *table = calloc(INODE_TABLE_LENGTH, sizeof (block_sector_t*));

      for (size_t i=0; i<n_indirect_blocks; i+=INODE_TABLE_LENGTH){
          size_t indirect_table_entry = i / INODE_TABLE_LENGTH;
          if (!free_map_allocate (1, &disk_inode->indirect_blocks[indirect_table_entry])) {
              free (disk_inode);
              free (table);
              return false;
            }

          memset (table, 0, BLOCK_SECTOR_SIZE);
          size_t n_table_entry = (n_indirect_blocks-i) < INODE_TABLE_LENGTH ? (n_indirect_blocks-i) : INODE_TABLE_LENGTH;
          for (size_t j=0; j<n_table_entry; j++){
              if (!free_map_allocate (1, &table[j])){
                  free (disk_inode);
                  free (table);
                  return false;
                }
              cache_write (table[j], zeros);
            }
    
          cache_write (disk_inode->indirect_blocks[indirect_table_entry], table);
        }
      success = true;
      free (table);

      cache_write (sector, disk_inode);
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  cache_read (inode->sector, &inode->data);

  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;
  
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed){
          int32_t sector_num = bytes_to_sectors (inode->data.length);
          if(sector_num < DIRECT_PTR_NUM + 1) {
            free_map_release (inode->sector, 1);
            free (inode);
            return;
          }
          /* Free Direct blocks */
          for (int32_t i = 0; i < DIRECT_PTR_NUM; i++) free_map_release (inode->data.direct_blocks[i], 1);
          
          uint32_t * table = (uint32_t*)malloc(INODE_TABLE_LENGTH * sizeof (uint32_t*));
          memset(table, 0, INODE_TABLE_LENGTH * sizeof (uint32_t*));
          /* Free the indirect blocks */
          int32_t i = 0;
          while(i < sector_num - DIRECT_PTR_NUM){
            
            int32_t indirect_table_entry = i / INODE_TABLE_LENGTH;
            cache_read (inode->data.indirect_blocks[indirect_table_entry], table);

            if((sector_num - DIRECT_PTR_NUM - i) >= INODE_TABLE_LENGTH){
              for (int32_t j = 0; j < INODE_TABLE_LENGTH; j++)
                free_map_release (table[j], 1);
            }else{
              for (int32_t j = 0; j < sector_num - DIRECT_PTR_NUM - i; j++)
                free_map_release (table[j], 1);
            }
            i = i + INODE_TABLE_LENGTH;
          }

          free (table);
          free_map_release (inode->sector, 1);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */

          cache_read (sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          cache_read (sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;
  byte_to_sector_write (inode, offset+size-1);
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      // block_sector_t sector_idx = byte_to_sector_write (inode, offset);
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          cache_write (sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            cache_read (sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          cache_write (sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

bool 
inode_is_dir (const struct inode *inode)
{
  return inode->data.is_dir;
}
