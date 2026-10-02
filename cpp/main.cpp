// main.cpp - Virtual NAND Storage Manager (user-space C++ application)
//
// Talks to the Linux driver ONLY through the device file /dev/virtualnand,
// using these system calls: open(), close(), lseek(), read(), write(), ioctl().
//
// Wear-leveling policy (menu option 7) lives HERE, not in the driver:
//   "write to the block with the lowest erase_count; on a tie, the smallest ID".
//
// Build:  make          Run:  sudo ./vnand_manager

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <limits>
#include <cctype>
#include <cerrno>
#include <cstdio>     // perror
#include <cstdlib>    // EXIT_FAILURE
#include <cstring>    // strerror

#include <fcntl.h>      // open, O_RDWR
#include <unistd.h>     // close, read, write, lseek
#include <sys/types.h>
#include <sys/ioctl.h>  // ioctl

#include "virtual_nand_ioctl.h"   // shared with the kernel driver

namespace {

const long long kMaxReadSize = 4096;   // keep terminal output reasonable

// ---------------------------------------------------------------
// Input helpers
// ---------------------------------------------------------------

// Read one number. On bad input: clear the error state, throw away the rest of
// the line, and return false. On success the rest of the line (including the
// newline) is also discarded, so a later std::getline() starts on a fresh line.
bool readNumber(const std::string &prompt, long long &value)
{
    std::cout << prompt;
    if (!(std::cin >> value)) {
        if (std::cin.eof())
            return false;                       // input closed (Ctrl+D)
        std::cin.clear();                       // reset failbit
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "Invalid input: please enter a number.\n";
        return false;
    }
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return true;
}

bool readBlockId(__u32 &id)
{
    long long v;
    if (!readNumber("Enter block ID: ", v))
        return false;
    if (v < 0 || v > 0xFFFFFFFFLL) {
        std::cout << "Block ID must be a non-negative number.\n";
        return false;
    }
    id = static_cast<__u32>(v);
    return true;
}

// ---------------------------------------------------------------
// ioctl wrappers: return 0 on success, or the errno value on failure
// ---------------------------------------------------------------

int ioctlGetDeviceInfo(int fd, vnand_device_info &info)
{
    if (ioctl(fd, VNAND_IOC_GET_DEVICE_INFO, &info) < 0)
        return errno;
    return 0;
}

int ioctlGetBlockInfo(int fd, __u32 id, vnand_block_info &info)
{
    info = vnand_block_info{};
    info.block_id = id;                         // input: which block
    if (ioctl(fd, VNAND_IOC_GET_BLOCK_INFO, &info) < 0)
        return errno;
    return 0;
}

int ioctlEraseBlock(int fd, __u32 id)
{
    if (ioctl(fd, VNAND_IOC_ERASE_BLOCK, &id) < 0)
        return errno;
    return 0;
}

// ---------------------------------------------------------------
// Wear-leveling policy: pick the least-worn block
// ---------------------------------------------------------------

// Asks the driver for every block's erase_count and chooses the smallest.
// Tie-break: smallest block ID. Returns 0 on success, or an errno value.
int findLeastWornBlock(int fd, __u32 &selectedBlock, __u32 &eraseCount)
{
    // 1. How many blocks does the device have? (never hardcoded)
    vnand_device_info dev{};
    int err = ioctlGetDeviceInfo(fd, dev);
    if (err != 0)
        return err;
    if (dev.block_count == 0)
        return ENODEV;

    // 2. Walk the blocks in order 0, 1, 2, ... and remember the best one so far
    bool found = false;
    for (__u32 b = 0; b < dev.block_count; b++) {
        vnand_block_info info{};
        err = ioctlGetBlockInfo(fd, b, info);
        if (err != 0)
            return err;

        // Strictly "<": an equal erase count never replaces the current choice.
        // Blocks are visited in ascending order, so on a tie the SMALLER ID wins.
        if (!found || info.erase_count < eraseCount) {
            selectedBlock = b;
            eraseCount = info.erase_count;
            found = true;
        }
    }

    std::cout << "Least-worn block selected: " << selectedBlock << "\n"
              << "Current erase count: " << eraseCount << "\n";
    return 0;
}

// ---------------------------------------------------------------
// Menu actions
// ---------------------------------------------------------------

void showDeviceInfo(int fd)
{
    vnand_device_info info{};
    int err = ioctlGetDeviceInfo(fd, info);
    if (err != 0) {
        std::cout << "ioctl GET_DEVICE_INFO failed: " << std::strerror(err) << "\n";
        return;
    }
    std::cout << "--- Device information ---\n"
              << "Block count     : " << info.block_count << "\n"
              << "Pages per block : " << info.pages_per_block << "\n"
              << "Page size       : " << info.page_size << " bytes\n"
              << "Total size      : " << info.total_size << " bytes\n";
}

void showBlockInfo(int fd)
{
    __u32 id;
    if (!readBlockId(id))
        return;

    vnand_block_info info{};
    int err = ioctlGetBlockInfo(fd, id, info);
    if (err == EINVAL) {
        std::cout << "Block " << id << " does not exist (invalid block ID).\n";
        return;
    }
    if (err != 0) {
        std::cout << "ioctl GET_BLOCK_INFO failed: " << std::strerror(err) << "\n";
        return;
    }
    std::cout << "--- Block information ---\n"
              << "Block ID    : " << info.block_id << "\n"
              << "Erase count : " << info.erase_count << "\n"
              << "Used pages  : " << info.used_pages << "\n";
}

void writeData(int fd)
{
    long long offset;
    if (!readNumber("Enter offset: ", offset))
        return;
    if (offset < 0) {
        std::cout << "Offset cannot be negative.\n";
        return;
    }

    std::cout << "Enter data: ";
    std::string data;
    std::getline(std::cin, data);               // whole line, spaces allowed
    if (data.empty()) {
        std::cout << "Nothing to write.\n";
        return;
    }

    if (lseek(fd, static_cast<off_t>(offset), SEEK_SET) == static_cast<off_t>(-1)) {
        perror("lseek");
        return;
    }

    ssize_t n = write(fd, data.data(), data.size());
    if (n < 0) {
        perror("write");                        // e.g. "No space left on device"
        return;
    }
    std::cout << "Written: " << n << " bytes\n";
    if (static_cast<size_t>(n) < data.size())
        std::cout << "Note: only part of the data fit before the end of the device.\n";
}

void readData(int fd)
{
    long long offset, length;
    if (!readNumber("Enter offset: ", offset))
        return;
    if (offset < 0) {
        std::cout << "Offset cannot be negative.\n";
        return;
    }
    if (!readNumber("Enter number of bytes: ", length))
        return;
    if (length <= 0 || length > kMaxReadSize) {
        std::cout << "Number of bytes must be between 1 and " << kMaxReadSize << ".\n";
        return;
    }

    if (lseek(fd, static_cast<off_t>(offset), SEEK_SET) == static_cast<off_t>(-1)) {
        perror("lseek");
        return;
    }

    std::vector<char> buffer(static_cast<size_t>(length));   // safe, auto-freed buffer
    ssize_t n = read(fd, buffer.data(), buffer.size());
    if (n < 0) {
        perror("read");
        return;
    }
    if (n == 0) {
        std::cout << "Read 0 bytes (offset is at or beyond the end of the device).\n";
        return;
    }

    std::cout << "Read " << n << " bytes\n";
    if (static_cast<size_t>(n) < buffer.size())
        std::cout << "(fewer than requested: reached the end of the device)\n";

    // The data is NOT a C string, so print exactly n bytes, byte by byte.
    // Non-printable bytes (like erased 0xFF) are shown as '.'.
    std::cout << "Text: ";
    for (ssize_t i = 0; i < n; i++) {
        unsigned char c = static_cast<unsigned char>(buffer[i]);
        std::cout << (std::isprint(c) ? static_cast<char>(c) : '.');
    }
    std::cout << "\nHex : ";
    std::cout << std::hex << std::setfill('0');
    for (ssize_t i = 0; i < n; i++) {
        std::cout << std::setw(2)
                  << static_cast<int>(static_cast<unsigned char>(buffer[i])) << ' ';
        if ((i + 1) % 16 == 0 && i + 1 < n)
            std::cout << "\n      ";
    }
    std::cout << std::dec << std::setfill(' ') << "\n";
}

void eraseBlock(int fd)
{
    __u32 id;
    if (!readBlockId(id))
        return;

    int err = ioctlEraseBlock(fd, id);
    if (err == 0)
        std::cout << "Block " << id << " erased successfully.\n";
    else if (err == EINVAL)
        std::cout << "Erase failed: block " << id << " is not a valid block.\n";
    else
        std::cout << "Erase failed: " << std::strerror(err) << "\n";
}

// Prints "Block N : erase_count" for every block. Returns false on ioctl failure.
bool printEraseCounts(int fd, const char *title)
{
    vnand_device_info dev{};
    int err = ioctlGetDeviceInfo(fd, dev);
    if (err != 0) {
        std::cout << "ioctl GET_DEVICE_INFO failed: " << std::strerror(err) << "\n";
        return false;
    }

    std::cout << title << "\n";
    for (__u32 b = 0; b < dev.block_count; b++) {
        vnand_block_info info{};
        err = ioctlGetBlockInfo(fd, b, info);
        if (err != 0) {
            std::cout << "Block " << b << " : error (" << std::strerror(err) << ")\n";
            continue;
        }
        std::cout << "Block " << b << " : " << info.erase_count << "\n";
    }
    return true;
}

void showAllEraseCounts(int fd)
{
    printEraseCounts(fd, "--- Erase counts ---");
}

// Menu option 7: write the data to the least-worn block (start of that block).
// This does NOT change any erase count; only VNAND_IOC_ERASE_BLOCK does that.
void wearLevelWrite(int fd)
{
    std::cout << "Enter data: ";
    std::string data;
    std::getline(std::cin, data);               // whole line, spaces allowed
    if (data.empty()) {
        std::cout << "Nothing to write.\n";
        return;
    }

    // 1. Device geometry -> size of one block in bytes
    vnand_device_info dev{};
    int err = ioctlGetDeviceInfo(fd, dev);
    if (err != 0) {
        std::cout << "ioctl GET_DEVICE_INFO failed: " << std::strerror(err) << "\n";
        return;
    }
    const size_t blockSize = static_cast<size_t>(dev.pages_per_block) * dev.page_size;

    // 2. The data must fit inside ONE block (never cross into the next one)
    if (data.size() > blockSize) {
        std::cout << "Data is too large for one NAND block.\n"
                  << "Maximum block size: " << blockSize << " bytes\n";
        return;
    }

    // 3. Show the current wear, then let the policy decide
    if (!printEraseCounts(fd, "Current erase counts:"))
        return;

    __u32 selectedBlock = 0;
    __u32 eraseCount = 0;
    err = findLeastWornBlock(fd, selectedBlock, eraseCount);
    if (err != 0) {
        std::cout << "Wear-level selection failed: " << std::strerror(err) << "\n";
        return;
    }

    // 4. Byte offset where the selected block starts
    const off_t blockOffset = static_cast<off_t>(selectedBlock) * static_cast<off_t>(blockSize);

    // 5. lseek() to that block, then write()
    if (lseek(fd, blockOffset, SEEK_SET) == static_cast<off_t>(-1)) {
        perror("lseek");
        return;
    }
    ssize_t n = write(fd, data.data(), data.size());
    if (n < 0) {
        perror("write");
        return;
    }

    std::cout << "Wear-level write successful.\n"
              << "Selected block : " << selectedBlock << "\n"
              << "Erase count    : " << eraseCount << "\n"
              << "Offset         : " << static_cast<long long>(blockOffset) << "\n"
              << "Bytes written  : " << n << "\n";
}

void printMenu()
{
    std::cout << "\n===== Virtual NAND Storage Manager =====\n"
              << "1. Show device information\n"
              << "2. Show block information\n"
              << "3. Write data\n"
              << "4. Read data\n"
              << "5. Erase block\n"
              << "6. Show all erase counts\n"
              << "7. Wear-level write\n"
              << "0. Exit\n";
}

} // namespace

int main()
{
    // open(): connect this program to the driver through the device file
    int fd = open(VNAND_DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("open " VNAND_DEVICE_PATH);
        std::cerr << "Is the module loaded (insmod)? Are you running with sudo?\n";
        return EXIT_FAILURE;
    }

    bool running = true;
    while (running) {
        printMenu();
        long long choice;
        if (!readNumber("Enter choice: ", choice)) {
            if (std::cin.eof())
                break;                          // input closed: leave the loop
            continue;                           // bad input: show menu again
        }

        switch (choice) {
        case 1: showDeviceInfo(fd);      break;
        case 2: showBlockInfo(fd);       break;
        case 3: writeData(fd);           break;
        case 4: readData(fd);            break;
        case 5: eraseBlock(fd);          break;
        case 6: showAllEraseCounts(fd);  break;
        case 7: wearLevelWrite(fd);      break;
        case 0: running = false;         break;
        default:
            std::cout << "Invalid menu selection. Choose 0-7.\n";
            break;
        }
    }

    // close(): release the file descriptor (driver's .release runs)
    if (close(fd) < 0) {
        perror("close");
        return EXIT_FAILURE;
    }
    std::cout << "Goodbye.\n";
    return 0;
}
