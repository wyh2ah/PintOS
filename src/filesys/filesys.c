#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "filesys/cache.h"

#define MAX_NAME_SIZE 14

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  cache_init();
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  cache_out_all();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
 block_sector_t inode_sector = 0;

  struct dir *current_dir = thread_current()->work_dir;
  if (*name == NULL) return false;
  if (strlen(name) > MAX_NAME_SIZE) return false;
  if (current_dir == NULL) current_dir = dir_open_root ();
  else current_dir = dir_reopen (current_dir);
  struct dir * return_dir = NULL;
  char * return_name = malloc(MAX_NAME_SIZE + 1);
  struct inode * inode = NULL;
  bool success = 0;
  if (dir_split (name, current_dir, &return_dir, &return_name) ){
    if(!dir_lookup (return_dir, return_name, &inode)){
      success = (return_dir != NULL&& free_map_allocate (1, &inode_sector)&& inode_create (inode_sector, initial_size)&& dir_add (return_dir, return_name, inode_sector));
      dir_close (return_dir);
      free (return_name);
      return success; 
    }  
  }
  dir_close (return_dir);
  inode_close (inode);
  free (return_name);
  return NULL;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *current_dir = thread_current()->work_dir;
  if (*name == NULL)return NULL;
  if (current_dir == NULL)current_dir = dir_open_root ();
  else current_dir = dir_reopen (current_dir);
  struct dir *return_dir = NULL;
  char *return_name = malloc(MAX_NAME_SIZE + 1);
  struct inode *inode = NULL;
  if (dir_split (name, current_dir, &return_dir, &return_name)){
    if(dir_lookup (return_dir, return_name, &inode)){
      dir_close (return_dir);
      free (return_name);
      return file_open (inode);
    }
  }
  dir_close (return_dir);
  free (return_name);
  return NULL;
}


bool
filesys_remove (const char *name) 
{
  if (*name == NULL)return false;
  struct dir *cur_dir = thread_current()->work_dir;
  if (cur_dir == NULL)cur_dir = dir_open_root ();
  else cur_dir = dir_reopen (cur_dir);

  struct dir *ret_dir = NULL;
  char *ret_name = malloc(15);
  struct inode *inode = NULL;

  bool success = dir_split (name, cur_dir, &ret_dir, &ret_name);
  if (dir_lookup (ret_dir, ret_name, &inode)){
    if(success){
      if (!inode->data.is_dir) success = success && dir_remove (ret_dir, ret_name);
      else{
        struct dir * dir_to_remove = dir_open (inode);
        success = success && dir_empty (dir_to_remove) && (inode->open_cnt <= 1) &&  dir_remove (ret_dir, ret_name);
      }
      inode_close (inode);
      dir_close (ret_dir);
      free (ret_name);

      return success;
    }
  }
  dir_close (ret_dir);
  free (ret_name);
  return false;
  
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

bool 
filesys_chdir (const char *name)
{
  struct dir *cur_dir = thread_current()->work_dir;
  if (*name == NULL) return false;
  if (cur_dir == NULL) cur_dir = dir_open_root ();
  else cur_dir = dir_reopen (cur_dir);
  struct dir * ret_dir = NULL;
  char *ret_name = malloc(MAX_NAME_SIZE + 1);
  struct inode *inode = NULL;
  if (dir_split (name, cur_dir, &ret_dir, &ret_name) && dir_lookup (ret_dir, ret_name, &inode) && ((inode)->data.is_dir)){
    if (thread_current()->work_dir != NULL) dir_close (thread_current()->work_dir);
    thread_current()->work_dir = dir_open (inode);
    dir_close (ret_dir);
    free (ret_name);
    return true;
  }
  dir_close (ret_dir);
  free (ret_name);
  return false;
}


bool 
filesys_mkdir (const char *name)
{
  struct dir *cur_dir = thread_current()->work_dir;
  if (*name == NULL)return false;
  if (cur_dir == NULL)cur_dir = dir_open_root ();
  else cur_dir = dir_reopen (cur_dir);
  struct dir *ret_dir = NULL;
  char *ret_name = malloc(MAX_NAME_SIZE + 1);
  struct inode *inode = NULL;
  block_sector_t sector;
  bool success=0;
  if (dir_split (name, cur_dir, &ret_dir, &ret_name) &&!dir_lookup (ret_dir, ret_name, &inode)){
    if (free_map_allocate (1, &sector) &&dir_create (sector, 0)){
      success = dir_add (ret_dir, ret_name, sector);
      dir_close (ret_dir);
      free (ret_name);
      return success;
    }
    dir_close (ret_dir);
    free (ret_name);
    return false;
  }
  dir_close (ret_dir);
  inode_close (inode);
  free (ret_name);
  return false;
}
