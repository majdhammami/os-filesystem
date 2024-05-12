#include "mfs.h"
#ifndef LOGFS_H
#define LOGFS_H
#define NumBlocks   14
#define NumInodes   4096
#define Cp_SIZE     6
#define BLOCKSIZE   4096
#define DSIZE       32
#define Numentries  (BLOCKSIZE/DSIZE)
#define MaxNameLen  28
#define Buffersize  4096
#define MaxNameSize 28

typedef struct __Payload {
    int Opayload;
    int inodeNum;
    int block;
    int type;

    char name[MaxNameSize];
    char buffer[Buffersize];
    MFS_Stat_t stat;
} Payload;

typedef struct __DirectoryEntries {
    char names[Numentries][MaxNameLen];
    int inodeNumbers[Numentries];
} DirectoryEntries;

typedef struct __Inode {
    int inodeNumber;
    int size;
    int type;
    int isBlockFilled[NumBlocks];
    int blockNumbers[NumBlocks];
} Inode;

// File system functions
int StartServer(int port, char* filePath);
int Server_Shutdown();

// Inode and block functions
void UpdateInodeMap(int inodeNumber, int block);
int GetInodeBlock(int inodeNumber);
int GetInode(int inodeNumber, Inode* inode);
int GetBlock(int isFirstBlock, int inodeNumber, int parentInodeNumber);
void InitializeInode(Inode* inode);
void InitializeDirectoryBlock(DirectoryEntries* directoryBlock);
void InitializeFileSystem();
void CreateRootNode();
int FindAvailableInode();
int IsDirectoryEmpty(Inode* inode);
void RemoveEntryFromBlock(DirectoryEntries* block, const char* entryName);
void UpdateDirectoryAndParent(Inode* parentInode, int blockIndex, DirectoryEntries* block);

// Directory operations
int SearchDirectoryBlocks(int parentInodeNum, const char* entryName);
int LookupServer(int parentInodeNumber, char* entryName);
int Server_Create(int parentInodeNum, int type, char* entryName);
int Server_Unlink(int parentInodeNum, char* entryName);

// File operations
int StatServer(int inodeNumber, MFS_Stat_t* mfsStat);
int WriteServer(int inodeNumber, char* buffer, int block);
int ReadServer(int inodeNumber, char* buffer, int block);

#endif // LOGFS_H

