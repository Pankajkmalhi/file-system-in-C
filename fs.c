#include "fs.h"
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// Define constants
#define DIRECTORY 1
#define REGULAR_FILE 2

// Define global variables
unsigned char *fs;
superblock *SB;
freeblocklist *FBL;
inode *inodes;

// Function declarations
int find_free_inode();
int find_free_block();
int find_inode_by_path(char *path);
void free_block(int block_id);
int find_directory_entry_by_name(int block_id, char *name);

void mapfs(int fd) {
  if ((fs = mmap(NULL, SEGSIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) ==
      MAP_FAILED) {
    perror("mmap failed");
    exit(EXIT_FAILURE);
  }

  // Initialize pointers to superblock, free block list, and inodes
  SB = (superblock *)fs;
  FBL = (freeblocklist *)(fs + sizeof(superblock));
  inodes = (inode *)(fs + sizeof(superblock) + sizeof(freeblocklist));
}

void unmapfs() { munmap(fs, SEGSIZE); }

void formatfs() {
  // Write the superblock to the file system
  SB->blocksize = BLOCKSIZE;
  SB->segsize = SEGSIZE;
  SB->inodesperseg = INODESPERSEG;
  SB->blockid = 0;
  SB->blocksperseg = BLOCKSPERSEG;

  // Initialize the free block list
  memset(FBL, 0, sizeof(freeblocklist));

  // Initialize inodes
  for (int i = 0; i < INODESPERSEG; ++i) {
    inodes[i].inuse = 0;
    inodes[i].type = 0;
    inodes[i].size = 0;
    memset(inodes[i].blocks, 0, sizeof(inodes[i].blocks));
    inodes[i].indirect = 0;
    inodes[i].dindirect = 0;
    inodes[i].tindirect = 0;
    memset(inodes[i].name, 0, sizeof(inodes[i].name)); // Initialize name field
  }
}

void loadfs() {
  // Read the superblock from the file system
  memcpy(SB, fs, sizeof(superblock));

  // Read the free block list from the file system
  memcpy(FBL, fs + sizeof(superblock), sizeof(freeblocklist));

  // Read inodes from the file system
  memcpy(inodes, fs + sizeof(superblock) + sizeof(freeblocklist),
         INODESPERSEG * sizeof(inode));

  // Additional tasks can be performed here if needed
}

void lsfs() {
  // Iterate over inodes and print directory entries
  for (int i = 0; i < INODESPERSEG; ++i) {
    if (inodes[i].inuse && inodes[i].type == DIRECTORY) {
      // Print directory name
      printf("Directory: %s\n", inodes[i].name);

      // Print files in directory
      for (int j = 0; j < inodes[i].size; ++j) {
        dirent entry;
        memcpy(&entry, fs + inodes[i].blocks[j] * BLOCKSIZE, sizeof(dirent));
        printf("    %s\n", entry.name);
      }
    }
  }
}

void addfilefs(char *fname) {
  // Check if the file exists
  if (access(fname, F_OK) == -1) {
    perror("File does not exist");
    return;
  }

  // Open the file
  int fd = open(fname, O_RDONLY);
  if (fd == -1) {
    perror("Failed to open file");
    return;
  }

  // Read file size
  off_t size = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);

  // Find a free inode
  int inode_idx = find_free_inode();
  if (inode_idx == -1) {
    perror("No free inode available");
    close(fd);
    return;
  }

  // Set inode attributes
  inodes[inode_idx].inuse = 1;
  inodes[inode_idx].type = REGULAR_FILE;
  inodes[inode_idx].size = size;

  // Allocate data blocks
  int num_blocks_needed = (size + BLOCKSIZE - 1) / BLOCKSIZE;
  int allocated_blocks[num_blocks_needed];
  for (int i = 0; i < num_blocks_needed; ++i) {
    allocated_blocks[i] = find_free_block();
    if (allocated_blocks[i] == -1) {
      perror("Insufficient free blocks");
      close(fd);
      return;
    }
  }

  // Write file data to blocks
  char buffer[BLOCKSIZE];
  for (int i = 0; i < num_blocks_needed; ++i) {
    int bytes_to_read =
        (i == num_blocks_needed - 1) ? (size % BLOCKSIZE) : BLOCKSIZE;
    ssize_t bytes_read = read(fd, buffer, bytes_to_read);
    if (bytes_read == -1) {
      perror("Error reading file");
      // Free allocated blocks
      for (int j = 0; j < i; ++j) {
        free_block(allocated_blocks[j]);
      }
      close(fd);
      return;
    }
    memcpy(fs + allocated_blocks[i] * BLOCKSIZE, buffer, bytes_read);
  }

  // Update inode with block references
  memcpy(inodes[inode_idx].blocks, allocated_blocks, sizeof(allocated_blocks));

  // Close the file
  close(fd);
}

void removefilefs(char *fname) {
  // Find the inode of the file to remove
  int inode_idx = find_inode_by_path(fname);
  if (inode_idx == -1) {
    perror("File not found");
    return;
  }

  // Check if the inode represents a directory
  if (inodes[inode_idx].type == DIRECTORY) {
    perror("Cannot remove directory using removefilefs");
    return;
  }

  // Free blocks associated with the file
  for (int i = 0; i < (inodes[inode_idx].size + BLOCKSIZE - 1) / BLOCKSIZE;
       ++i) {
    free_block(inodes[inode_idx].blocks[i]);
  }

  // Mark inode as unused
  inodes[inode_idx].inuse = 0;

  // Remove directory entry
  char *parent_dir = dirname(fname);
  int parent_inode_idx = find_inode_by_path(parent_dir);
  if (parent_inode_idx != -1) {
    int entry_idx = find_directory_entry_by_name(
        inodes[parent_inode_idx].blocks[0], basename(fname));
    if (entry_idx != -1) {
      dirent empty_entry;
      empty_entry.inuse = 0;
      memcpy(fs + inodes[parent_inode_idx].blocks[0] * BLOCKSIZE +
                 entry_idx * sizeof(dirent),
             &empty_entry, sizeof(dirent));
    }
  }
}

void extractfilefs(char *fname) {
  // Find the inode of the file to extract
  int inode_idx = find_inode_by_path(fname);
  if (inode_idx == -1) {
    perror("File not found");
    return;
  }

  // Check if the inode represents a directory
  if (inodes[inode_idx].type == DIRECTORY) {
    perror("Cannot extract directory using extractfilefs");
    return;
  }

  // Open or create the destination file
  int fd = open(fname, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
  if (fd == -1) {
    perror("Failed to open destination file");
    return;
  }

  // Write file data to destination file
  for (int i = 0; i < (inodes[inode_idx].size + BLOCKSIZE - 1) / BLOCKSIZE;
       ++i) {
    int bytes_to_write =
        (i == (inodes[inode_idx].size + BLOCKSIZE - 1) / BLOCKSIZE - 1)
            ? (inodes[inode_idx].size % BLOCKSIZE)
            : BLOCKSIZE;
    ssize_t bytes_written =
        write(fd, fs + inodes[inode_idx].blocks[i] * BLOCKSIZE, bytes_to_write);
    if (bytes_written == -1) {
      perror("Error writing to destination file");
      close(fd);
      return;
    }
  }

  // Close the destination file
  close(fd);
}

// Find a free inode in the file system
int find_free_inode() {
  for (int i = 0; i < INODESPERSEG; ++i) {
    if (!inodes[i].inuse) {
      return i; // Return the index of the free inode
    }
  }
  return -1; // Return -1 if no free inode is found
}

// Find a free block in the file system
int find_free_block() {
  for (int i = 0; i < BLOCKSPERSEG; ++i) {
    if (!FBL->blocks[i]) {
      FBL->blocks[i] = 1; // Mark the block as used
      return i;           // Return the index of the free block
    }
  }
  return -1; // Return -1 if no free block is found
}

// Find the inode index corresponding to a given file path
int find_inode_by_path(char *path) {
  // Iterate over the inodes to find the matching path
  for (int i = 0; i < INODESPERSEG; ++i) {
    if (inodes[i].inuse && strcmp(inodes[i].name, path) == 0) {
      return i; // Return the index of the inode if path matches
    }
  }
  return -1; // Return -1 if path not found
}

// Free a block in the file system
void free_block(int block_id) {
  FBL->blocks[block_id] = 0; // Mark the block as free
}

// Find the directory entry index by name in a given block
int find_directory_entry_by_name(int block_id, char *name) {
  // Iterate over the directory entries in the block to find the matching name
  for (int i = 0; i < DIRENTRIESPERBLOCK; ++i) {
    dirent entry;
    memcpy(&entry, fs + block_id * BLOCKSIZE + i * sizeof(dirent),
           sizeof(dirent));
    if (entry.inuse && strcmp(entry.name, name) == 0) {
      return i; // Return the index of the directory entry if name matches
    }
  }
  return -1; // Return -1 if name not found
}
