#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include "udp.h"
#include "mfs.h"
#include "server.h"

int InodeMap[NumInodes/16][16];
int LogEnd;
int FileSystemDescriptor;

typedef struct __Buffer {
    char data[BLOCKSIZE];
} Buffer;

// Update the InodeMap with the given inodeNumber and block mapping.
void UpdateInodeMap(int inodeNumber, int block) {
    int row = inodeNumber / 16;
    int col = inodeNumber % 16;
    InodeMap[row][col] = block;
}

// Retrieve the block mapping for the given inodeNumber.
int GetInodeBlock(int inodeNumber) {
    return InodeMap[inodeNumber/16][inodeNumber%16];
}
int IsValidInodeNumber(int inodeNumber) {
    return (inodeNumber >= 0 && inodeNumber < NumInodes);
}

int CalculateInodeBlockIndex(int inodeNumber) {
    return InodeMap[inodeNumber / 16][inodeNumber % 16];
}
// Retrieve the Inode information for the given inodeNumber.

int GetInode(int inodeNumber, Inode* inode) {
    if (!IsValidInodeNumber(inodeNumber)) {
        printf("Erreur : GetInode - inodeNumber invalide\n");
        return -1;
    }

    int inodeBlock = CalculateInodeBlockIndex(inodeNumber);
    if (inodeBlock == -1) {
        printf("Erreur : GetInode - Échec de récupération du bloc d'inode\n");
        return -1;
    }

    lseek(FileSystemDescriptor, inodeBlock * BLOCKSIZE, SEEK_SET);
    read(FileSystemDescriptor, inode, sizeof(Inode));
    return 0;
}
// Get a block for a new file or directory.
int GetBlock(int isFirstBlock, int inodeNumber, int parentInodeNumber) {
    DirectoryEntries directoryBlock;
    for (int i = 0; i < Numentries; i++) {
        directoryBlock.inodeNumbers[i] = -1;
        strcpy(directoryBlock.names[i], "\0");
    }

    if (isFirstBlock) {
        directoryBlock.inodeNumbers[0] = inodeNumber;
        strcpy(directoryBlock.names[0], ".\0");
        directoryBlock.inodeNumbers[1] = parentInodeNumber;
        strcpy(directoryBlock.names[1], "..\0");
    }

    lseek(FileSystemDescriptor, LogEnd * BLOCKSIZE, SEEK_SET);
    write(FileSystemDescriptor, &directoryBlock, BLOCKSIZE);
    LogEnd++;
    return LogEnd - 1;
}

// Update the Checkpoint Region with the current inodeNumber.
void UpdateCheckPointRegion(int inodeNumber) {
    if (inodeNumber != -1) {
        lseek(FileSystemDescriptor, inodeNumber * sizeof(int), SEEK_SET);
        write(FileSystemDescriptor, &InodeMap[inodeNumber/16][inodeNumber%16], sizeof(int));
    }

    lseek(FileSystemDescriptor, NumInodes * sizeof(int), SEEK_SET);
    write(FileSystemDescriptor, &LogEnd, sizeof(int));
}

// Initialize the Inode structure.
void InitializeInode(Inode* inode) {
    for (int i = 0; i < NumBlocks; i++) {
        inode->isBlockFilled[i] = 0;
        inode->blockNumbers[i] = -1;
    }
}

// Initialize the Directory Block structure.
void InitializeDirectoryBlock(DirectoryEntries *directoryBlock) {
    for (int i = 0; i < Numentries; i++) {
        directoryBlock->inodeNumbers[i] = -1;
        strcpy(directoryBlock->names[i], "\0");
    }
}

void InitializeFileSystem() {
    LogEnd = Cp_SIZE;

    for (int i = 0; i < NumInodes; i++) {
        UpdateInodeMap(i, -1);
    }

    lseek(FileSystemDescriptor, 0, SEEK_SET);
    write(FileSystemDescriptor, InodeMap, sizeof(int) * NumInodes);
    write(FileSystemDescriptor, &LogEnd, sizeof(int));
}

void CreateRootNode() {
    Inode rootNode;
    rootNode.inodeNumber = 0;
    rootNode.type = MFS_DIRECTORY;
    rootNode.size = BLOCKSIZE;
    rootNode.blockNumbers[0] = LogEnd;
    rootNode.isBlockFilled[0] = 1;

    for (int i = 1; i < NumBlocks; i++) {
        rootNode.isBlockFilled[i] = 0;
        rootNode.blockNumbers[i] = -1;
    }

    DirectoryEntries baseDirectoryBlock;
    baseDirectoryBlock.inodeNumbers[0] = 0;
    baseDirectoryBlock.inodeNumbers[1] = 0;
    strcpy(baseDirectoryBlock.names[0], ".\0");
    strcpy(baseDirectoryBlock.names[1], "..\0");

    for (int i = 2; i < Numentries; i++) {
        baseDirectoryBlock.inodeNumbers[i] = -1;
        strcpy(baseDirectoryBlock.names[i], "\0");
    }

    lseek(FileSystemDescriptor, LogEnd * BLOCKSIZE, SEEK_SET);
    write(FileSystemDescriptor, &baseDirectoryBlock, sizeof(DirectoryEntries));
    LogEnd++;

    UpdateInodeMap(0, LogEnd);

    lseek(FileSystemDescriptor, LogEnd * BLOCKSIZE, SEEK_SET);
    write(FileSystemDescriptor, &rootNode, sizeof(Inode));
    LogEnd++;

    UpdateCheckPointRegion(0);
}

int StartServer(int port, char* filePath) {
    if ((FileSystemDescriptor = open(filePath, O_RDWR)) == -1) {
        FileSystemDescriptor = open(filePath, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
        if (FileSystemDescriptor == -1)
            return -1;

        InitializeFileSystem();
        CreateRootNode();
    } else {
        printf("FS already exists.\n");

        lseek(FileSystemDescriptor, 0, SEEK_SET);
        read(FileSystemDescriptor, InodeMap, sizeof(int) * NumInodes);
        read(FileSystemDescriptor, &LogEnd, sizeof(int));
    }
    return 0;
}

// Function to check if the directory block contains the entryName
int DirectoryBlockContainsEntry(DirectoryEntries *block, const char *entryName) {
    for (int j = 0; j < Numentries; j++) {
        if (block->inodeNumbers[j] != -1 && strcmp(block->names[j], entryName) == 0) {
            return block->inodeNumbers[j];
        }
    }
    return -1;
}

// Function to search for entryName in the parent inode's directory blocks
int SearchDirectoryBlocks(int parentInodeNum, const char *entryName) {
    Inode parentInode;
    if (GetInode(parentInodeNum, &parentInode) == -1) {
        perror("Error in LookupServer: GetInode");
        printf("Error in LookupServer: Failed to get parent inode\n");
        return -1;
    }

    for (int i = 0; i < NumBlocks; i++) {
        if (parentInode.isBlockFilled[i]) {
            DirectoryEntries foundDirectoryBlock;
            lseek(FileSystemDescriptor, parentInode.blockNumbers[i] * BLOCKSIZE, SEEK_SET);
            if (read(FileSystemDescriptor, &foundDirectoryBlock, BLOCKSIZE) == -1) {
                perror("Error in LookupServer: read");
                printf("Error in LookupServer: Failed to read directory block\n");
                return -1;
            }

            int inodeNumber = DirectoryBlockContainsEntry(&foundDirectoryBlock, entryName);
            if (inodeNumber != -1) {
                return inodeNumber;
            }
        }
    }

    return -1;
}

// Main function to perform the lookup operation
int LookupServer(int parentInodeNumber, char *entryName) {
	printf("Entering function LookupServer..");
    printf("parentInodeNumber = %d, entryName = %s\n", parentInodeNumber, entryName);
    printf("Size of entryName: %lu\n", strlen(entryName));
    printf("Address of entryName: %p\n", (void *)entryName);
    // Search for entryName in the parent inode's directory blocks
    int inodeNumber = SearchDirectoryBlocks(parentInodeNumber, entryName);
    if (inodeNumber != -1) {
        printf("Found! Returning inodeNumber: %d\n", inodeNumber);
        return inodeNumber;
    } else {
        printf("Entry name not found!\n");
        return -1;
    }
}


// Get file/directory information for a given inodeNumber.
int StatServer(int inodeNumber, MFS_Stat_t *mfsStat) {
    printf("Entering StatServer...\n");
    Inode node;
    int inodeExistStatus = GetInode(inodeNumber, &node);
    if (inodeExistStatus == -1) {
        perror("StatServer: GetInode");
        printf("StatServer: Failed to get inode\n");
        return -1;
    }
    mfsStat->type = node.type;
    mfsStat->size = node.size;
    return 0;
}

// Write data to a file in the server's file system.
int WriteServer(int inodeNumber, char *buffer, int block) {
    printf("Entering WriteServer...\n");
    Inode node;
    int inodeExistStatus = GetInode(inodeNumber, &node);
    if (inodeExistStatus == -1) {
        perror("WriteServer: GetInode");
        printf("WriteServer: Failed to get inode\n");
        return -1;
    }

    if (node.type != MFS_REGULAR_FILE) {
        return -1;
    }

    if (block < 0 || block >= 14) {
        return -1;
    }

    int newFileSize = 0;
    if ((block + 1) * BLOCKSIZE > node.size) {
        newFileSize = (block + 1) * BLOCKSIZE;
    } else {
        newFileSize = node.size;
    }
    node.size = newFileSize;
    node.isBlockFilled[block] = 1;

    node.blockNumbers[block] = LogEnd + 1;
    lseek(FileSystemDescriptor, LogEnd * BLOCKSIZE, SEEK_SET);
    if (write(FileSystemDescriptor, &node, sizeof(Inode)) == -1) {
        perror("WriteServer: write");
        printf("WriteServer: Failed to write inode\n");
        return -1;
    }

    UpdateInodeMap(inodeNumber, LogEnd);
    LogEnd++;

    lseek(FileSystemDescriptor, LogEnd * BLOCKSIZE, SEEK_SET);
    write(FileSystemDescriptor, buffer, BLOCKSIZE);
    LogEnd++;

    UpdateCheckPointRegion(inodeNumber);

    return 0;
}

// Read data from a file or directory in the server's file system.
int ReadServer(int inodeNumber, char *buffer, int block) {
    printf("Entering ReadServer...\n");
    Inode node;
    if (GetInode(inodeNumber, &node) == -1) {
        printf("GetInode failed for inodeNumber %d.\n", inodeNumber);
        return -1;
    }

    if (block < 0 || block >= NumBlocks || !node.isBlockFilled[block]) {
        printf("Invalid block.\n");
        return -1;
    }

    if (node.type == MFS_REGULAR_FILE) {
        if (lseek(FileSystemDescriptor, node.blockNumbers[block] * BLOCKSIZE, SEEK_SET) == -1) {
            perror("ReadServer: lseek:");
            printf("ReadServer: lseek failed\n");
        }

        if (read(FileSystemDescriptor, buffer, BLOCKSIZE) == -1) {
            perror("ReadServer: read:");
            printf("ReadServer: read failed\n");
        }
    } else {
        DirectoryEntries directoryBlock;
        lseek(FileSystemDescriptor, node.blockNumbers[block], SEEK_SET);
        read(FileSystemDescriptor, &directoryBlock, BLOCKSIZE);

        MFS_DirectoryEntries_t entries[Numentries];
        int i;
        for (i = 0; i < Numentries; i++) {
            MFS_DirectoryEntries_t entry;
            strcpy(entry.name, directoryBlock.names[i]);
            entry.inum = directoryBlock.inodeNumbers[i];
            entries[i] = entry;
        }

        memcpy(buffer, entries, sizeof(MFS_DirectoryEntries_t) * Numentries);
    }

    return 0;
}

int FindAvailableInode() {
    for (int i = 0; i < NumInodes; i++) {
        if (GetInodeBlock(i) == -1) {
            return i;
        }
    }
    return -1;
}

int CreateInode(int parentInodeNum, int type, char *entryName) {
    Inode parentInode;
    int parentInodeExists = GetInode(parentInodeNum, &parentInode);
    if (parentInodeExists == -1) {
        printf("Parent Inode does not exist\n");
        return -1;
    }

    if (parentInode.type != MFS_DIRECTORY) {
        printf("Parent is not a directory\n");
        return -1;
    }

    int inodeNum = FindAvailableInode();
    if (inodeNum == -1) {
        printf("No more available Inodes\n");
        return -1;
    }

    int blockIndex;
    int entryIndex;
    DirectoryEntries block;

    for (blockIndex = 0; blockIndex < NumBlocks; blockIndex++) {
        if (parentInode.isBlockFilled[blockIndex]) {
            lseek(FileSystemDescriptor, parentInode.blockNumbers[blockIndex] * BLOCKSIZE, SEEK_SET);
            read(FileSystemDescriptor, &block, BLOCKSIZE);

            for (entryIndex = 0; entryIndex < Numentries; entryIndex++) {
                if (block.inodeNumbers[entryIndex] == -1) {
                    lseek(FileSystemDescriptor, GetInodeBlock(parentInodeNum) * BLOCKSIZE, SEEK_SET);
                    write(FileSystemDescriptor, &parentInode, sizeof(Inode));

                    block.inodeNumbers[entryIndex] = inodeNum;
                    strcpy(block.names[entryIndex], entryName);
                    lseek(FileSystemDescriptor, parentInode.blockNumbers[blockIndex] * BLOCKSIZE, SEEK_SET);
                    write(FileSystemDescriptor, &block, BLOCKSIZE);

                    Inode node;
                    node.inodeNumber = inodeNum;
                    node.size = 0;
                    InitializeInode(&node);

                    node.type = type;
                    if (type == MFS_DIRECTORY) {
                        node.isBlockFilled[0] = 1;
                        node.blockNumbers[0] = LogEnd;

                        GetBlock(1, inodeNum, parentInodeNum);
                        node.size += BLOCKSIZE;
                    } else if (type != MFS_DIRECTORY && type != MFS_REGULAR_FILE) {
                        return -1;
                    }

                    UpdateInodeMap(inodeNum, LogEnd);
                    lseek(FileSystemDescriptor, LogEnd * BLOCKSIZE, SEEK_SET);
                    write(FileSystemDescriptor, &node, sizeof(Inode));
                    LogEnd++;

                    UpdateCheckPointRegion(inodeNum);
                    return 0;
                }
            }
        } else {
            int dirBlock = GetBlock(0, inodeNum, -1);
            parentInode.size += BLOCKSIZE;
            parentInode.isBlockFilled[blockIndex] = 1;
            parentInode.blockNumbers[blockIndex] = dirBlock;
            blockIndex--;
        }
    }

    return -1;
}

int Server_Create(int parentInodeNum, int type, char *entryName) {
    printf("Entering Server_Create...\n");
    int exists = LookupServer(parentInodeNum, entryName);
    if (exists != -1) {
        printf("Entry already exists\n");
        return 0;
    }

    int createResult = CreateInode(parentInodeNum, type, entryName);
    return createResult;
}


// Check if a directory is empty
int IsDirectoryEmpty(Inode *inode) {
    for (int blockIndex = 0; blockIndex < NumBlocks; blockIndex++) {
        if (inode->isBlockFilled[blockIndex]) {
            DirectoryEntries block;
            lseek(FileSystemDescriptor, inode->blockNumbers[blockIndex] * BLOCKSIZE, SEEK_SET);
            read(FileSystemDescriptor, &block, BLOCKSIZE);

            for (int entryIndex = 0; entryIndex < Numentries; entryIndex++) {
                if (block.inodeNumbers[entryIndex] != -1 && strcmp(block.names[entryIndex], ".") != 0 && strcmp(block.names[entryIndex], "..") != 0) {
                    return 0; // Directory is not empty
                }
            }
        }
    }
    return 1; // Directory is empty
}

// Remove entry from directory block
void RemoveEntryFromBlock(DirectoryEntries *block, const char *entryName) {
    for (int entryIndex = 0; entryIndex < Numentries; entryIndex++) {
        if (block->inodeNumbers[entryIndex] != -1 && strcmp(block->names[entryIndex], entryName) == 0) {
            block->inodeNumbers[entryIndex] = -1;
            strcpy(block->names[entryIndex], "\0");
            return;
        }
    }
}

// Update directory block and parent inode
void UpdateDirectoryAndParent(Inode *parentInode, int blockIndex, DirectoryEntries *block) {
    lseek(FileSystemDescriptor, LogEnd * BLOCKSIZE, SEEK_SET);
    write(FileSystemDescriptor, block, BLOCKSIZE);
    LogEnd++;

    parentInode->blockNumbers[blockIndex] = LogEnd - 1;
    lseek(FileSystemDescriptor, LogEnd * BLOCKSIZE, SEEK_SET);
    write(FileSystemDescriptor, parentInode, sizeof(Inode));
    LogEnd++;

    UpdateInodeMap(parentInode->inodeNumber, LogEnd - 1);
    UpdateCheckPointRegion(parentInode->inodeNumber);
}

int Server_Unlink(int parentInodeNum, char *entryName) {
    printf("Entering Server_Unlink...\n");

    Inode toRemove;
    Inode parentInode;

    if (GetInode(parentInodeNum, &parentInode) == -1) {
        printf("Parent Inode does not exist\n");
        return -1;
    }

    printf("Finding Inode number of file/dir name = %s to be unlinked\n", entryName);
    int inodeNum = LookupServer(parentInodeNum, entryName);

    if (inodeNum == -1) {
        printf("Name does not exist, unlinking is not a failure\n");
        return 0;
    }

    if (GetInode(inodeNum, &toRemove) == -1) {
        printf("Error getting Inode to unlink\n");
        return -1;
    }

    if (toRemove.type == MFS_DIRECTORY && !IsDirectoryEmpty(&toRemove)) {
        printf("Directory not empty\n");
        return -1;
    }

    int unlinkDone = 0;
    for (int blockIndex = 0; blockIndex < NumBlocks && !unlinkDone; blockIndex++) {
        if (parentInode.isBlockFilled[blockIndex]) {
            DirectoryEntries block;
            lseek(FileSystemDescriptor, parentInode.blockNumbers[blockIndex] * BLOCKSIZE, SEEK_SET);
            read(FileSystemDescriptor, &block, BLOCKSIZE);

            RemoveEntryFromBlock(&block, entryName);

            if (unlinkDone) {
                UpdateDirectoryAndParent(&parentInode, blockIndex, &block);
            }
        }
    }

    printf("Removing Inode from Inode map\n");
    UpdateInodeMap(inodeNum, -1);
    UpdateCheckPointRegion(inodeNum);

    return 0;
}

int Server_Shutdown() {
    fsync(FileSystemDescriptor);
    exit(0);
}

void HandleClientRequest(int DescriptorSock) {
    struct sockaddr_in clientAddress;
    Payload payload;
    int ReadBytes = UDP_Read(DescriptorSock, &clientAddress, (char *)&payload, sizeof(Payload));

    if (ReadBytes > 0) {
        Payload responseOfP;
        switch (payload.Opayload) {
            case 0:
                responseOfP.inodeNum = LookupServer(payload.inodeNum, payload.name);
                break;
            case 1:
                responseOfP.inodeNum = StatServer(payload.inodeNum, &(responseOfP.stat));
                break;
            case 2:
                responseOfP.inodeNum = WriteServer(payload.inodeNum, payload.buffer, payload.block);
                break;
            case 3:
                responseOfP.inodeNum = ReadServer(payload.inodeNum, responseOfP.buffer, payload.block);
                break;
            case 4:
                responseOfP.inodeNum = Server_Create(payload.inodeNum, payload.type, payload.name);
                break;
            case 5:
                responseOfP.inodeNum = Server_Unlink(payload.inodeNum, payload.name);
                break;
            default:
                break;
        }

        responseOfP.Opayload = 6;
        ReadBytes = UDP_Write(DescriptorSock, &clientAddress, (char *)&responseOfP, sizeof(Payload));

        if (payload.Opayload == 7) {
            Server_Shutdown();
        }
    }
}

void ShowUsage() {
    printf("Usage: ./server <port_number> <file_system_path>\n");
}

int main(int argc, char *argv[]) {
	if (argc != 3) {
        ShowUsage();
        return 1;
    }
    
    int portNumber = atoi(argv[1]);
    char *fileSysPath = argv[2];

    StartServer(portNumber, fileSysPath);

    int DescriptorSock = UDP_Open(portNumber);
    if (DescriptorSock < 0) {
        printf("Error opening socket on port %d\n", portNumber);
        exit(1);
    }

    printf("Server running..\n");
    while (1) {
        HandleClientRequest(DescriptorSock);
    }

    return 0;
}






