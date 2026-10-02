# System Architecture

**Project:** Flash Memory Wear-Leveling Simulator & Storage Hub

---

## 1. Architecture Overview

This project simulates NAND flash storage entirely in software on Linux, without any physical hardware. The system has two layers:

1. **User space:** a C++ management application that interacts with the user and applies a simple wear-leveling policy.
2. **Kernel space:** a Linux character device driver that owns the simulated NAND memory, its metadata, and all erase operations.

The two layers communicate only through the device file `/dev/virtualnand` and standard Linux system calls. The C++ application never accesses kernel memory directly.

---

## 2. High-Level Architecture

```
C++ User-Space Application
         |
         |  open / close / read / write / lseek / ioctl
         v
/dev/virtualnand
         |
=========|=============== user space / kernel space boundary
         v
Linux Character Driver
         |
         v
Simulated NAND Memory + Metadata
   |
   +-- 8 blocks x 16 pages x 512 bytes = 65536 bytes (data buffer)
   +-- Per-block metadata: block ID, erase count, used pages
   +-- Per-page metadata: state (free / written), program count
```

| Property | Value |
|----------|-------|
| Erase blocks | 8 |
| Pages per block | 16 |
| Page size | 512 bytes |
| Block size | 8192 bytes (16 x 512) |
| Total capacity | 65536 bytes (8 x 16 x 512) |

---

## 3. User-Space Layer

The C++ application (`cpp/main.cpp`, executable `vnand_manager`) provides a command-line menu. It:

- Opens `/dev/virtualnand` with `open()` and closes it with `close()` on exit
- Shows device information and block information (via `ioctl()`)
- Writes data at a chosen offset (`lseek()` + `write()`)
- Reads data from a chosen offset (`lseek()` + `read()`)
- Erases a block (via `ioctl()`)
- Shows the erase count of every block
- Performs a wear-level write, using the least-worn block selection described in Section 10
- Validates user input and reports the result of each operation

The application reaches the driver only through system calls, and it uses the shared header `include/virtual_nand_ioctl.h` for ioctl command numbers and data structures.

---

## 4. Device Interface

`/dev/virtualnand` is a character device node. It is the only connection between the two layers.

- `open()` returns a file descriptor linked to the driver.
- `read()`, `write()`, `lseek()`, and `ioctl()` on that descriptor are dispatched to the matching functions in the driver's `file_operations` table (`.read`, `.write`, `.llseek` support, `.unlocked_ioctl`).
- `close()` releases the descriptor and calls the driver's `.release` function.

---

## 5. Kernel Driver Layer

The kernel module (`driver/virtual_nand_driver.c`) is responsible for:

- Registering the character device and creating `/dev/virtualnand` on module load, and removing everything cleanly on unload
- Allocating and initializing the simulated NAND memory
- Implementing `open`, `release`, `read`, `write`, and `ioctl`
- Checking bounds on reads and writes and validating block IDs
- Copying data safely between user space and kernel space with `copy_from_user()` and `copy_to_user()`
- Performing block erase and maintaining erase counters and used-page tracking
- Protecting the buffer and metadata with a mutex
- Logging important events with kernel messages

---

## 6. Simulated NAND Storage Layer

The simulated flash is a single contiguous 65536-byte buffer in kernel memory, organized logically as blocks of pages:

```
Virtual NAND
├── Block 0
│   ├── Page 0
│   ├── Page 1
│   └── ... Page 15
├── Block 1
│   └── Pages 0..15
└── ... Block 7
```

- The buffer starts in the erased state, where every byte is `0xFF`.
- A byte offset maps to a location by simple division: block = `offset / 8192`, page within block = `(offset % 8192) / 512`.
- The data exists only in kernel memory, so it is lost when the module is unloaded.

---

## 7. Metadata Management

The driver keeps metadata in a static array, separate from the data buffer:

| Level | Field | Meaning |
|-------|-------|---------|
| Block | `block_id` | Index of the block |
| Block | `erase_count` | Number of times the block has been erased |
| Block | `used_pages` | Number of pages written since the last erase |
| Page | `state` | Free or written |
| Page | `program_count` | Writes to the page since the last erase |

- A write marks the touched pages as written and updates `used_pages`.
- An erase resets the pages and `used_pages`, and increases `erase_count`.

---

## 8. IOCTL Communication

Control and statistics operations use `ioctl()` instead of `read()` and `write()`. The commands and structures are defined once in `include/virtual_nand_ioctl.h`, which is included by both the driver and the C++ application.

| Command | Direction | Purpose |
|---------|-----------|---------|
| `VNAND_IOC_GET_DEVICE_INFO` | driver to user | Returns block count, pages per block, page size, total size |
| `VNAND_IOC_GET_BLOCK_INFO` | user to driver and back | User supplies a block ID; driver returns erase count and used pages |
| `VNAND_IOC_ERASE_BLOCK` | user to driver | Erases the given block |

The driver validates every request and returns standard error codes: `-EINVAL` for an invalid block ID, `-EFAULT` for a bad user-space pointer, and `-ENOTTY` for an unknown command.

---

## 9. Read/Write Data Flow

**Write**

```
User enters offset and data
  -> lseek(fd, offset, SEEK_SET)        sets the file position
  -> write(fd, data, size)
  -> driver .write: check bounds (data is shortened if it would pass the end)
  -> copy_from_user() into the simulated NAND buffer
  -> mark touched pages as written, update used_pages
  -> advance the file position, return bytes written
```

**Read**

```
User enters offset and length
  -> lseek(fd, offset, SEEK_SET)
  -> read(fd, buffer, length)
  -> driver .read: check bounds (returns 0 at or beyond the end)
  -> copy_to_user() from the simulated NAND buffer
  -> advance the file position, return bytes read
```

A write at or beyond the end of the device fails with "no space left". All data flows through `read()` and `write()` with user-space copies; the application never touches the kernel buffer directly.

---

## 10. Block Erase Flow

```
User selects "Erase block"
  -> C++ calls ioctl(fd, VNAND_IOC_ERASE_BLOCK, &block_id)
  -> kernel enters the driver's ioctl handler
  -> copy_from_user() copies the block ID into the kernel
  -> driver validates the block ID (invalid: return -EINVAL)
  -> driver sets all 8192 bytes of the block to 0xFF
  -> pages become free, used_pages = 0, erase_count + 1
  -> result returns to the C++ application, which prints the outcome
```

Erase counters change only through this operation.

---

## 11. Wear-Leveling Decision Flow

The wear-leveling policy lives in the C++ application, not in the driver. The driver supplies the erase counts, and the application makes the decision.

```
User chooses "Wear-level write" and enters data
  -> C++ gets device information (block count, block size)
  -> data larger than one block is rejected
  -> C++ queries every block with VNAND_IOC_GET_BLOCK_INFO
  -> selects the block with the lowest erase_count
     (if erase counts are equal, the smallest block ID wins)
  -> block_offset = selected_block x block_size
  -> lseek(fd, block_offset, SEEK_SET)
  -> write(fd, data, size)
  -> driver stores the data in the selected block
```

Example: with erase counts 3, 1, 0, 0, 0, 0, 0, 0 for blocks 0 to 7, block 2 is selected and the offset is 2 x 8192 = 16384. A wear-level write does not change any erase count.

---

## 12. Component Responsibilities

| Component | Responsibility |
|-----------|----------------|
| `driver/virtual_nand_driver.c` | Character device, simulated NAND data, block and page metadata, read/write, erase, ioctl handling |
| `include/virtual_nand_ioctl.h` | Shared ioctl commands, device name, and data structures used by both sides |
| `cpp/main.cpp` | User menu, input validation, system calls, and the least-worn block selection policy |
| `tests/ioctl_test.c` | Small C utility for testing the ioctl commands |
| `docs/` | Project documentation |

| Responsibility | Owner |
|----------------|-------|
| NAND data, metadata, erase operation, erase counters | Kernel driver |
| User interaction and wear-leveling decision | C++ application |
| Communication | Device file and Linux system calls |

---

## 13. Architectural Limitations

This project is a simplified learning simulator. It does not include:

- A full Flash Translation Layer (FTL)
- Logical-to-physical address mapping
- Garbage collection
- Page migration
- Persistent NAND state after module reload (the simulated NAND is in kernel memory, so its state resets after `rmmod` and `insmod`)
- Bad-block management
- A background daemon
- Multithreading
- A graphical user interface
- Networking
- A database

The wear-leveling policy only chooses a destination block by erase count. It does not move existing data or manage stale data.
