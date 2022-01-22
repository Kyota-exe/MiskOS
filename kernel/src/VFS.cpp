#include "VFS.h"
#include "Memory/Memory.h"
#include "Ext2.h"
#include "StringUtilities.h"
#include "Panic.h"
#include "Vector.h"
#include "Serial.h"
#include "Ext2Inode.h"

VNode TraversePath(const char* absolutePath, bool& exists, char*& finalPathToken)
{
    uint64_t pathStringLength = String::Length(absolutePath);
    char* path = new char[pathStringLength + 1];
    MemCopy(path, absolutePath, pathStringLength + 1);
    path[pathStringLength] = 0;
    char* remainingPath = path;

    Ext2::Ext2Inode* currentInode = Ext2::rootDirInode;
    VNode lastVNode = VNode("", 2);

    char* pathToken = String::Split(remainingPath, '/', &remainingPath);
    KAssert(*pathToken == 0, "Absolute path does not begin at root.");

    uint64_t remainingPathDepth = String::Count(remainingPath, '/') + 1;
    bool foundNextInode = false;
    while (remainingPathDepth > 0)
    {
        foundNextInode = false;
        pathToken = String::Split(remainingPath, '/', &remainingPath);
        for (const VNode& nodeInDirectory : currentInode->GetDirectoryListing())
        {
            if (String::Equals(nodeInDirectory.name, pathToken))
            {
                foundNextInode = true;
                lastVNode = nodeInDirectory;
                currentInode = Ext2::GetInode(nodeInDirectory.inodeNum);
                break;
            }
        }
        remainingPathDepth--;

        // If the entry in the directory could not be found
        KAssert(foundNextInode || remainingPathDepth == 0, "Directory entry could not be found while traversing path.");
    }

    exists = foundNextInode;

    uint64_t finalPathTokenStringSize = String::Length(pathToken);
    finalPathToken = new char[finalPathTokenStringSize + 1];
    MemCopy(finalPathToken, pathToken, finalPathTokenStringSize);
    finalPathToken[finalPathTokenStringSize] = 0;

    delete[] path;
    return VNode(lastVNode.name, lastVNode.inodeNum);
}

uint64_t RepositionOffset(FileDescriptor* fileDescriptor, Ext2::Ext2Inode* inode, uint64_t offset, VFSSeekType seekType)
{
    switch (seekType)
    {
        case SeekSet:
            fileDescriptor->offset = offset;
            break;
        case SeekCursor:
            fileDescriptor->offset += offset;
            break;
        case SeekEnd:
            fileDescriptor->offset = inode->size0 + offset;
            break;
    }

    if (fileDescriptor->offset > inode->size0)
    {
        inode->Write(0, fileDescriptor->offset - inode->size0, inode->size0, false);
    }

    return fileDescriptor->offset;
}

uint64_t RepositionOffset(int descriptor, uint64_t offset, VFSSeekType seekType, Process* process)
{
    // TODO: Support files larger than 2^32 bytes
    FileDescriptor* fileDescriptor = &process->fileDescriptors[descriptor];
    Ext2::Ext2Inode* inode = Ext2::GetInode(fileDescriptor->vNode->inodeNum);
    return RepositionOffset(fileDescriptor, inode, offset, seekType);
}

VNode Create(const char* name, Ext2::Ext2Inode* directoryInode)
{
    uint32_t inodeNum = directoryInode->Create(name);
    return VNode(name, inodeNum);
}

void Close(int descriptor, Process* process)
{
    FileDescriptor* fileDescriptor = &process->fileDescriptors[descriptor];
    fileDescriptor->present = false;
    fileDescriptor->offset = 0;
}

int Open(const char* path, int flags, Process* process)
{
    // The VNode* in the descriptor will never be a dangling pointer since VNodes
    // stay in memory for the duration their file descriptor is alive.
    // TODO: Can't this memory stuff just be managed in the FileDescriptor constructor/destructor?

    FileDescriptor* descriptor;

    // Find descriptor int
    int descriptorIndex = -1;
    for (int i = 0; (uint64_t)i < process->fileDescriptors.GetLength(); ++i)
    {
        if (!process->fileDescriptors[i].present)
        {
            process->fileDescriptors[i].present = true;
            descriptorIndex = i;
        }
    }
    if (descriptorIndex == -1)
    {
        descriptorIndex = (int)process->fileDescriptors.GetLength();
        process->fileDescriptors.Push({true, 0, nullptr});
    }

    descriptor = &process->fileDescriptors[descriptorIndex];

    // Find file in ext2
    bool exists = false;
    char* filename = nullptr;
    descriptor->vNode = new VNode(TraversePath(path, exists, filename));

    Ext2::Ext2Inode* inode = Ext2::GetInode(descriptor->vNode->inodeNum);

    if ((flags & VFSOpenFlag::OCreate) && !exists)
    {
        *descriptor->vNode = Create(filename, inode);
    }

    if ((flags & VFSOpenFlag::OTruncate) && (inode->typePermissions & Ext2::InodeTypePermissions::RegularFile))
    {
        inode->size0 = 0;
    }

    if ((flags & VFSOpenFlag::OAppend))
    {
        RepositionOffset(descriptor, inode, 0, VFSSeekType::SeekEnd);
    }

    delete[] filename;
    return descriptorIndex;
}

uint64_t Read(int descriptor, void* buffer, uint64_t count, Process* process)
{
    FileDescriptor* fileDescriptor = &process->fileDescriptors[descriptor];
    Ext2::Ext2Inode* inode = Ext2::GetInode(fileDescriptor->vNode->inodeNum);

    uint64_t readCount = inode->Read(buffer, count, fileDescriptor->offset);
    RepositionOffset(fileDescriptor, inode, readCount, VFSSeekType::SeekCursor);

    return readCount;
}

uint64_t Write(int descriptor, const void* buffer, uint64_t count, Process* process)
{
    FileDescriptor* fileDescriptor = &process->fileDescriptors[descriptor];
    Ext2::Ext2Inode* inode = Ext2::GetInode(fileDescriptor->vNode->inodeNum);

    uint64_t wroteCount = inode->Write(buffer, count, fileDescriptor->offset);
    RepositionOffset(fileDescriptor, inode, wroteCount, VFSSeekType::SeekCursor);

    return wroteCount;
}

VNode::VNode(const char* _name, uint32_t _inodeNum) : inodeNum(_inodeNum)
{
    uint64_t nameLength = String::Length(_name);
    name = new char[nameLength + 1];
    MemCopy(name, (void*)_name, nameLength);
    name[nameLength] = 0;
}

VNode::VNode(const VNode &original) : inodeNum(original.inodeNum)
{
    uint64_t nameLength = String::Length(original.name);
    name = new char[nameLength + 1];
    MemCopy(name, original.name, nameLength);
    name[nameLength] = 0;
}

VNode::~VNode()
{
    delete[] name;
}

VNode& VNode::operator=(const VNode& newValue)
{
    if (&newValue != this)
    {
        delete[] name;
        uint64_t nameLength = String::Length(newValue.name);
        name = new char[nameLength + 1];
        MemCopy(name, newValue.name, nameLength);
        name[nameLength] = 0;

        inodeNum = newValue.inodeNum;
    }

    return *this;
}