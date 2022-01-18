#include "Ext2.h"
#include <stdint.h>
#include "Memory/Memory.h"
#include "Serial.h"
#include "StringUtilities.h"
#include "Panic.h"

namespace Ext2
{
    struct Ext2Superblock
    {
        uint32_t inodesCount;
        uint32_t blocksCount;
        uint32_t superuserReservedBlocksCount;
        uint32_t unallocatedBlocksCount;
        uint32_t unallocatedInodesCount;
        uint32_t superblockBlock; // Block containing the superblock
        uint32_t blockSizeLog2Minus10; // 1024 << n = block size
        uint32_t fragmentSizeLog2Minus10; // 1024 << n = fragment size
        uint32_t blocksPerBlockGroup;
        uint32_t fragmentsPerBlockGroup;
        uint32_t inodesPerBlockGroup;
        uint32_t lastMountTime; // in POSIX time
        uint32_t lastWrittenTime; // in POSIX time
        uint16_t volumeMountedCount; // Number of times the volume has been mounted since its last consistency check
        uint16_t mountsAllowedBeforeConsistencyCheck; // Number of mounts allowed before a consistency check must be done
        uint16_t ext2Signature;
        uint16_t fileSystemState;
        uint16_t errorHandlingMethod;
        uint16_t minorVersion; // Version fractional part
        uint32_t lastConsistencyCheckTime; // in POSIX time
        uint32_t forcedConsistencyChecksInterval; // in POSIX time
        uint32_t originOperatingSystemId; // Operating system ID from which the filesystem on this volume was created
        uint32_t majorVersion; // Version integer part
        uint16_t reservedBlocksUserId;
        uint16_t reservedBlocksGroupId;

        // Extended superblock fields
        uint32_t firstNonReservedInode; // Inode numbers start at 1
        uint16_t inodeSize;
        uint16_t superblockBlockGroup; // Block group that this superblock is part of if this is a backup superblock
        uint32_t optionalFeatures;
        uint32_t requiredFeatures;
        uint32_t readOnlyFeatures; // Features that if not supported, the volume must be mounted read-only
        uint8_t fileSystemId[16];
        char volumeName[16]; // Null-terminated volume name
        char lastMountedPath[64]; // Null-terminated name of path the volume was last mounted to
        uint32_t compressionAlgorithm;
        uint8_t preallocFilesBlocksCount; // Number of blocks to preallocate for files
        uint8_t preallocDirectoriesBlocksCount; // Number of blocks to preallocate for directories
        uint16_t unused;
        uint8_t journalId[16];
        uint32_t journalInode;
        uint32_t journalDevice;
        uint32_t orphanInodeListHead;
    };

    struct Ext2BlockGroupDescriptor
    {
        uint32_t blockUsageBitmapBlock;
        uint32_t inodeUsageBitmapBlock;
        uint32_t inodeTableStartBlock;
        uint16_t unallocatedBlocksCount;
        uint16_t unallocatedInodesCount;
        uint16_t directoriesCount;
        uint8_t unused[14];
    } __attribute__((packed));

    struct Ext2DirectoryEntry
    {
        uint32_t inodeNum;
        uint16_t entrySize;
        uint8_t nameLength; // Name length least-significant 8 bits
        uint8_t typeIndicator; // Name length most-significant 8 bits if feature "directory entries have file type byte" is not set
    } __attribute__((packed));

    const uint32_t INODE_ROOT_DIR = 2;

    uint32_t blockGroupsCount;
    uint64_t blockSize;
    Ext2Superblock* superblock;
    Ext2BlockGroupDescriptor* blockGroupDescTable;
    Ext2Inode* rootDirInode;

    // TODO: Get rid of this when loading Ext2 from disk is implemented
    uint64_t ramDiskAddr;

    Ext2Inode* GetInode(uint32_t inodeNum)
    {
        // Inode indexes start at 1
        uint32_t blockGroupIndex = (inodeNum - 1) / superblock->inodesPerBlockGroup;
        uint32_t inodeIndex = (inodeNum - 1) % superblock->inodesPerBlockGroup;

        uint64_t diskAddr = blockGroupDescTable[blockGroupIndex].inodeTableStartBlock * blockSize + inodeIndex * superblock->inodeSize;

        // TODO: If loading Ext2 from disk, read from disk and allocate space for it on the heap
        return (Ext2Inode*)(diskAddr + ramDiskAddr);
    }

    VNode ConstructVNode(Ext2DirectoryEntry* directoryEntry)
    {
        // TODO: Add support for when "file type byte for directory entries" are not supported.
        char name[directoryEntry->nameLength + 1];
        MemCopy(name, (char*)((uint64_t)directoryEntry + sizeof(Ext2DirectoryEntry)), directoryEntry->nameLength);
        name[directoryEntry->nameLength] = 0;

        if (String::Length(name) != directoryEntry->nameLength)
        {
            Serial::Printf("Directory entry name length computed (%d) ", String::Length(name), "");
            Serial::Printf("is different from name length in directory entry (%d).", directoryEntry->nameLength);
        }

        return VNode(name, directoryEntry->inodeNum);
    }

    // TODO: Support directories other than root
    Vector<VNode> GetDirectoryListing(Ext2Inode* directoryInode)
    {
        Vector<VNode> vNodes;

        uint64_t parsedLength = 0;
        while (parsedLength < directoryInode->size0)
        {
            // TODO: Add support for when the byte at index 7 of the directory entry is the most-significant 8 bits of the name length
            uint8_t nameLengthLowByte;
            directoryInode->Read(&nameLengthLowByte, 1, parsedLength + 6);
            uint32_t trueEntrySize = 8 + nameLengthLowByte;

            auto directoryEntry = (Ext2DirectoryEntry*)KMalloc(trueEntrySize);
            directoryInode->Read(directoryEntry, trueEntrySize, parsedLength);

            if (directoryEntry->inodeNum != 0)
            {
                VNode newVnode = ConstructVNode(directoryEntry);
                vNodes.Push(newVnode);
            }

            parsedLength += directoryEntry->entrySize;

            delete directoryEntry;
        }

        return vNodes;
    }

    void Initialize(uint64_t ramDiskBegin, uint64_t ramDiskEnd)
    {
        Serial::Print("Initializing Ext2 file system...");

        // TODO: Eventually switch from RAM disk to real disk
        Serial::Printf("Ext2 RAM disk size: %x", ramDiskEnd - ramDiskBegin);
        ramDiskAddr = ramDiskBegin;

        // TODO: If loading Ext2 from disk, read from disk and allocate 1024B for it on the heap
        superblock = (Ext2Superblock*)(ramDiskAddr + 1024);

        KAssert(superblock->ext2Signature == 0xef53, "Invalid ext2 signature!");
        KAssert(superblock->fileSystemState == 1, "File system state is not clean.");

        // Parse from superblock
        blockSize = 1024 << superblock->blockSizeLog2Minus10;

        Serial::Printf("Inodes count: %d", superblock->inodesCount);
        Serial::Printf("Blocks count: %d", superblock->blocksCount);
        Serial::Printf("Block size: %d", blockSize);
        Serial::Printf("Blocks per block group: %d", superblock->blocksPerBlockGroup);
        Serial::Printf("Inodes per block group: %d", superblock->inodesPerBlockGroup);

        Serial::Printf("Version: %d.", superblock->majorVersion, "");
        Serial::Printf("%d", superblock->minorVersion);

        // TODO: If loading Ext2 from disk, free superblock allocate on the heap

        blockGroupsCount = superblock->blocksCount / superblock->blocksPerBlockGroup +
                           (superblock->blocksCount % superblock->blocksPerBlockGroup > 0 ? 1 : 0);

        uint32_t blockGroupsCountCheck = superblock->inodesCount / superblock->inodesPerBlockGroup +
                                         (superblock->inodesCount % superblock->inodesPerBlockGroup > 0 ? 1 : 0);

        KAssert(blockGroupsCount == blockGroupsCountCheck, "Block group count could not be calculated.");

        Serial::Printf("Block group count: %d", blockGroupsCount);
        Serial::Printf("Required features: %x", superblock->requiredFeatures);
        Serial::Printf("Read-only features: %x", superblock->readOnlyFeatures);

        // TODO: If loading Ext2 from disk, allocate space for it on the heap
        uint32_t blockGroupDescTableDiskAddr = blockSize * (blockSize == 1024 ? 2 : 1);
        blockGroupDescTable = (Ext2BlockGroupDescriptor*)(ramDiskBegin + blockGroupDescTableDiskAddr);

        rootDirInode = GetInode(INODE_ROOT_DIR);

        Vector<VNode> rootDirContents = GetDirectoryListing(rootDirInode);
        Serial::Print("\n/ contents:");
        uint32_t subDirInodeNum = 0;
        for (uint64_t i = 0; i < rootDirContents.GetLength(); ++i)
        {
            Serial::Printf("%d - ", rootDirContents[i].inodeNum, "");
            Serial::Print(rootDirContents[i].name);
            if (String::Equals(rootDirContents[i].name, "subdirectory-bravo"))
            {
                subDirInodeNum = rootDirContents[i].inodeNum;
            }
        }

        Vector<VNode> subDirContents = GetDirectoryListing(GetInode(subDirInodeNum));
        Serial::Print("\n/subdirectory-bravo/ contents:");
        uint64_t barInodeNum = 0;
        for (uint64_t i = 0; i < subDirContents.GetLength(); ++i)
        {
            Serial::Printf("%d - ", subDirContents[i].inodeNum, "");
            Serial::Print(subDirContents[i].name);
            if (String::Equals(subDirContents[i].name, "bar.txt"))
            {
                barInodeNum = subDirContents[i].inodeNum;
            }
        }

        Ext2Inode* barInode = GetInode(barInodeNum);
        char* textContents = new char[barInode->size0];
        barInode->Read(textContents, barInode->size0);
        Serial::Print("\n/subdirectory-bravo/bar.txt contents:");
        Serial::Print(textContents);
    }
}