#include "DeviceFS.h"
#include "Device.h"
#include "Pseudoterminal.h"
#include "Serial.h"
#include "Heap.h"

DeviceFS::DeviceFS(Disk* disk) : FileSystem(disk)
{
    fileSystemRoot = new (Allocator::Permanent) VFS::Vnode();
    fileSystemRoot->inodeNum = currentInodeNum++;
    fileSystemRoot->type = VFS::VnodeType::Directory;
    fileSystemRoot->fileSystem = this;
    VFS::CacheVNode(fileSystemRoot);

    Device* terminal = new Pseudoterminal(String("tty"), currentInodeNum++);
    devices.Push(terminal);

    auto terminalVnode = new (Allocator::Permanent) VFS::Vnode();
    terminalVnode->inodeNum = terminal->inodeNum;
    terminalVnode->fileSystem = this;
    terminalVnode->context = terminal;
    terminalVnode->fileSize = 0;
    terminalVnode->type = VFS::VnodeType::CharacterDevice;
    VFS::CacheVNode(terminalVnode);
}

uint64_t DeviceFS::Read(VFS::Vnode* vnode, void* buffer, uint64_t count, uint64_t readPos)
{
    auto device = (Device*)vnode->context;
    return device->Read(buffer, count);

    (void)readPos;
}

uint64_t DeviceFS::Write(VFS::Vnode* vnode, const void* buffer, uint64_t count, uint64_t writePos)
{
    auto device = (Device*)vnode->context;
    return device->Write(buffer, count);

    (void)writePos;
}

VFS::Vnode* DeviceFS::FindInDirectory(VFS::Vnode* directory, const String& name)
{
    Assert(directory == fileSystemRoot);

    for (Device* device : devices)
    {
        Serial::Log("[/dev]------------- Found: %s", device->name.ToRawString());

        if (device->name.Equals(name))
        {
            VFS::Vnode* file = VFS::SearchInCache(device->inodeNum, this);

            Assert(file != nullptr);

            return file;
        }
    }

    return nullptr;
}

void DeviceFS::Create(VFS::Vnode* vnode, VFS::Vnode* directory, const String& name)
{
    Panic();

    (void)vnode;
    (void)directory;
    (void)name;
}

void DeviceFS::Truncate(VFS::Vnode* vnode)
{
    Panic();

    (void)vnode;
}