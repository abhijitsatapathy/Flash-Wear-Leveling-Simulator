# Interface Design

**Project:** Flash Memory Wear-Leveling Simulator & Storage Hub

---

## 1. Interface Overview

The project is a Linux-only educational simulator written in C and C++. A Linux kernel character driver provides a simulated NAND flash device, and a C++ user-space application manages it.

The only interface between the two sides is the device file `/dev/virtualnand` together with standard Linux system calls. The C++ application never accesses kernel memory directly.

```
C++ User-Space Application
         |
         |  open / read / write / lseek / ioctl / close
         v
/dev/virtualnand
         |
         v
Linux Character Driver
         |
         v
Simulated NAND Memory + Metadata
```

**Device geometry**

| Property | Value |
|----------|-------|
| Blocks | 8 |
| Pages per block | 16 |
| Page size | 512 bytes |
| Block size | 8192 bytes (16 x 512) |
| Total capacity | 65536 bytes (8 x 16 x 512) |

---

## 2. Device File Interface

| Item | Value |
|------|-------|
| Device file | `/dev/virtualnand` |
| Device type | Character device |
| Created by | The kernel driver, on module load |
| Removed by | The kernel driver, on module unload |
| Access | Opened by the C++ application with `open()` and `O_RDWR` |

The device name and path are defined in the shared header as `VNAND_DEVICE_NAME` and `VNAND_DEVICE_PATH`. Loading and unloading the kernel module require root privileges. In the current setup, the C++ application is run with sudo to access /dev/virtualnand.

---

## 3. File Operations

Each system call used by the application is handled by a matching function in the driver's file operations.

| User-space call | Driver file operation | Purpose |
|-----------------|-----------------------|---------|
| `open()` | `open` | Connects the application to the device |
| `close()` | `release` | Releases the device |
| `read()` | `read` | Reads data from the simulated NAND |
| `write()` | `write` | Writes data to the simulated NAND |
| `lseek()` | `llseek` | Sets the file position for the next read or write |
| `ioctl()` | `unlocked_ioctl` | Control and statistics commands |

---

## 4. Shared IOCTL Header

The header `include/virtual_nand_ioctl.h` is included by both the kernel driver and the C++ application. It defines:

- The device name and device path
- The ioctl magic number (`'N'`)
- The three ioctl command macros
- The data structures exchanged through ioctl

Using one shared header keeps the command numbers and structure layouts identical on both sides. The structures use fixed-width integer types (`__u32`) only. The application does not redefine any of these items.

---

## 5. IOCTL Commands

| Command | Macro type | Direction | Argument | Purpose |
|---------|------------|-----------|----------|---------|
| `VNAND_IOC_GET_DEVICE_INFO` | `_IOR` | Driver to user | `struct vnand_device_info` | Returns the device geometry |
| `VNAND_IOC_GET_BLOCK_INFO` | `_IOWR` | User to driver, then back | `struct vnand_block_info` | User supplies `block_id`; driver fills in the rest |
| `VNAND_IOC_ERASE_BLOCK` | `_IOW` | User to driver | `__u32` block ID | Erases the complete block |

These are the only ioctl commands in the interface.

**Erase behavior:** `VNAND_IOC_ERASE_BLOCK` erases one complete block. All bytes in the block return to the erased value `0xFF`, the block's used-page information is reset, and its erase count increases by one. Erase counts change only through this command.

---

## 6. IOCTL Data Structures

**`struct vnand_device_info`** (used by `VNAND_IOC_GET_DEVICE_INFO`)

| Field | Type | Meaning |
|-------|------|---------|
| `block_count` | `__u32` | Number of blocks |
| `pages_per_block` | `__u32` | Pages in each block |
| `page_size` | `__u32` | Bytes per page |
| `total_size` | `__u32` | Total capacity in bytes |

**`struct vnand_block_info`** (used by `VNAND_IOC_GET_BLOCK_INFO`)

| Field | Type | Filled by | Meaning |
|-------|------|-----------|---------|
| `block_id` | `__u32` | User (input) | Block to query |
| `erase_count` | `__u32` | Driver (output) | Number of times the block has been erased |
| `used_pages` | `__u32` | Driver (output) | Pages written since the last erase |

**Block ID argument** (used by `VNAND_IOC_ERASE_BLOCK`): a single `__u32` holding the block ID to erase.

---

## 7. Read Interface

The `read()` operation transfers data from the simulated NAND to the application.

- It starts at the current file position.
- If the position is at or beyond the total NAND size, it returns `0` (end of data).
- If the requested length extends past the end of the device, the length is reduced so the read stays inside the device.
- It copies data to the application with `copy_to_user()`.
- After a successful read, the file position advances by the number of bytes read.
- It returns the number of bytes read.

The C++ application also limits a single read request to a maximum of 4096 bytes and rejects zero or negative sizes before calling `read()`.

---

## 8. Write Interface

The `write()` operation transfers data from the application to the simulated NAND.

- It starts at the current file position.
- If the position is at or beyond the end of the device, it fails with `-ENOSPC` (no space left).
- If the data would extend past the end of the device, the length is reduced so the write stays inside the device.
- It copies data from the application with `copy_from_user()`.
- It updates the page and block metadata for the pages that were written.
- After a successful write, the file position advances by the number of bytes written.
- It returns the number of bytes written.

---

## 9. Seek Interface

The driver supports `llseek`. The application calls:

```c
lseek(fd, offset, SEEK_SET);
```

before each read or write to move to the required byte offset. The driver's `read` and `write` then start from that position.

The C++ application rejects negative offsets before calling `lseek()` and reports any `lseek()` failure.

---

## 10. Error Handling

The driver validates requests and returns standard Linux error codes. In user space, a failed system call returns `-1` and sets `errno` to the corresponding value.

| Condition | Driver return | Applies to |
|-----------|---------------|------------|
| Invalid block ID | `-EINVAL` | `VNAND_IOC_GET_BLOCK_INFO`, `VNAND_IOC_ERASE_BLOCK` |
| Invalid user-space pointer | `-EFAULT` | ioctl, `read()`, `write()` |
| Unknown ioctl command | `-ENOTTY` | `ioctl()` |
| Write at or beyond device capacity | `-ENOSPC` | `write()` |

The C++ application checks the result of every system call and prints a clear message on failure, so invalid operations do not crash the program.

---

## 11. User-Space to Kernel-Space Communication Flow

**Data transfer (write and read)**

```
Application                          Driver
-----------                          ------
lseek(fd, offset, SEEK_SET)   --->   llseek: set file position
write(fd, data, size)         --->   write: check bounds, copy_from_user(),
                                           update metadata, advance position
lseek(fd, offset, SEEK_SET)   --->   llseek: set file position
read(fd, buffer, size)        --->   read: check bounds, copy_to_user(),
                                           advance position
```

**Control and statistics (ioctl)**

```
Application                          Driver
-----------                          ------
ioctl(fd, COMMAND, &data)     --->   unlocked_ioctl: copy_from_user() if input is needed,
                                           validate, perform the operation,
                                           copy_to_user() if output is needed
                              <---   return 0 on success, or a negative error code
```

**Session**

```
open("/dev/virtualnand", O_RDWR)  -->  driver open
 ... lseek / read / write / ioctl ...
close(fd)                         -->  driver release
```

---

## 12. Interface Responsibilities

| Component | Responsibility |
|-----------|----------------|
| C++ application | Opens and closes the device, validates user input, calls `lseek()`, `read()`, `write()`, and `ioctl()`, and displays results and errors |
| Shared header | Defines the ioctl commands, structures, and device name used by both sides |
| Kernel driver | Implements the file operations, validates requests, and transfers data safely with `copy_to_user()` and `copy_from_user()` |
| Simulated NAND memory | Holds the data, block and page metadata, and erase counters inside the driver |
| Linux system calls | The only communication path between the application and the driver |

---

## 13. Interface Limitations

The interface does not include:

- Direct user-space access to kernel memory
- Sockets or networking
- Daemon-based inter-process communication
- Shared-memory inter-process communication
- A graphical user interface
- A database interface
- Multiple NAND devices
- Additional custom ioctl commands beyond the three listed in Section 5
