#ifndef FS_H
#define FS_H

#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// Define constants
#define BLOCKSIZE 4096
#define SEGSIZE (1024 * 1024 * 100) // 100 MB segment size
#define INODESPERSEG 1024
#define BLOCKSPERSEG ((SEGSIZE / BLOCKSIZE))
#define DIRENTRIESPERBLOCK (BLOCKSIZE / sizeof(dirent))

#include <sys/types.h>

typedef struct superblock {
  int blocksize;
  int segsize;
  int inodesperseg;
  int blockid;
  int blocksperseg;
} superblock;

typedef struct freeblocklist {
  char bitmap[(SEGSIZE / BLOCKSIZE) / 8];
  int blocks[BLOCKSPERSEG];
} freeblocklist;

typedef struct inode {
  int inuse;
  int type;
  int size;
  int blocks[(BLOCKSIZE / 4) - 4];
  int indirect;
  int dindirect;
  int tindirect;
  char name[1000];
} inode;

typedef struct block {
  char data[BLOCKSIZE];
} block;

typedef struct dirent {
  char name[256];
  int type;
  int inodenum;
  int inuse;
} dirent;

typedef struct segment {
  int type;
  superblock SB;
  freeblocklist FBL;
  inode inodes[INODESPERSEG];
  block blocks[BLOCKSPERSEG];
} segment;

extern unsigned char *fs;

void mapfs(int fd);
void unmapfs();
void formatfs();
void loadfs();
void lsfs();
void addfilefs(char *fname);
void removefilefs(char *fname);
void extractfilefs(char *fname);

#endif
