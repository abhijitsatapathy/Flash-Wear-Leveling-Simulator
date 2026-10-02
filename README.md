# Flash Memory Wear-Leveling Simulator & Storage Hub

| | |
|---|---|
| **Student** | Abhijit Satapathy |
| **Registration No** | 2341019353 |

---

## Project Overview

This capstone project is a software-only NAND flash simulator for Linux. A Linux kernel character driver simulates a small NAND flash device in kernel memory and exposes it as `/dev/virtualnand`. A C++ user-space application manages the device through standard Linux system calls and applies a simple wear-leveling policy.

No physical hardware is used. The project runs on Ubuntu Linux (for example, in a VirtualBox virtual machine).

## Objective

The objective is to simulate NAND flash blocks and pages, track erase counts, and use a simple erase-count-based wear-leveling policy to choose the least-worn block for writes.

## Key Features

- Linux character device `/dev/virtualnand` implemented as a kernel module
- Simulated NAND memory in kernel space, organized as blocks and pages
- Block and page metadata: erase count, used pages, page state, and page program count
- File operations: `open`, `release`, `read`, `write`, `llseek`, and `ioctl`
- Block erase that restores a whole block to `0xFF` and increments its erase count
- Internal state protected by a mutex
- Shared ioctl header used by both the driver and the C++ application
- C++ command-line manager with a menu
- Simple wear-leveling policy in the C++ application: lowest erase count wins, smallest block ID breaks ties
- Small C test utility for the ioctl commands

## System Architecture

```
C++ User-Space Application
         |
         |  open / close / read / write / lseek / ioctl
         v
/dev/virtualnand
         |
         v
Linux Character Driver
         |
         v
Simulated NAND Memory + Metadata
```

- **Kernel driver:** owns the simulated NAND data, block and page metadata, erase operations, and ioctl handling.
- **C++ application:** handles user interaction and the wear-leveling decision. It communicates only through the device file and Linux system calls, and never accesses kernel memory directly.
- **Shared header:** `include/virtual_nand_ioctl.h` defines the ioctl commands and structures used by both sides.

The C++ application is written as functions in `cpp/main.cpp`; it does not use C++ classes.

## NAND Geometry

| Property | Value |
|----------|-------|
| Blocks | 8 |
| Pages per block | 16 |
| Page size | 512 bytes |
| Block size | 8192 bytes |
| Total capacity | 65536 bytes |

The erased value of a NAND byte is `0xFF`.

## Project Structure

```
flash_wear_leveling/
├── driver/
│   ├── virtual_nand_driver.c     Linux kernel module
│   └── Makefile                  Builds virtual_nand_driver.ko
├── include/
│   └── virtual_nand_ioctl.h      Shared ioctl header (driver + C++ app)
├── cpp/
│   ├── main.cpp                  C++ storage manager
│   └── Makefile                  Builds vnand_manager
├── tests/
│   └── ioctl_test.c              Small ioctl test utility (C)
├── docs/                         Project documentation
├── README.md
└── .gitignore
```

## Build Requirements

- Ubuntu Linux (kernel headers matching the running kernel)
- `gcc`, `g++`, and `make` (`build-essential`)
- `linux-headers-$(uname -r)`
- `kmod` (`insmod`, `rmmod`, `lsmod`)
- `libelf-dev`
- sudo access to load and unload the kernel module; in the current setup, the C++ application is also run with sudo to access /dev/virtualnand

Install on Ubuntu if needed:

```bash
sudo apt install build-essential linux-headers-$(uname -r) kmod libelf-dev
```

## Build Instructions

**Kernel module** (from the project root):

```bash
cd driver
make
```

This produces `virtual_nand_driver.ko`.

**C++ application:**

```bash
cd ../cpp
make
```

This produces the executable `vnand_manager`.

**Optional ioctl test utility** (from the project root):

```bash
gcc -Wall -Wextra -Iinclude tests/ioctl_test.c -o tests/ioctl_test
```

To remove build files, run `make clean` in `driver/` and in `cpp/`.

## Module Loading and Device Verification

```bash
cd driver
sudo insmod virtual_nand_driver.ko
lsmod | grep virtual_nand
ls -l /dev/virtualnand
sudo dmesg | tail
```

Expected results:

- `lsmod` lists `virtual_nand_driver`.
- `ls -l` shows a character device (`c...`) named `/dev/virtualnand`.
- `dmesg` shows messages such as the geometry and `module loaded, /dev/virtualnand created`.

To unload the module:

```bash
sudo rmmod virtual_nand_driver
```

## Running the C++ Application

With the module loaded:

```bash
cd cpp
sudo ./vnand_manager
```

In the current setup, the application is run with sudo to access /dev/virtualnand.

## Menu / Features

```
===== Virtual NAND Storage Manager =====
1. Show device information
2. Show block information
3. Write data
4. Read data
5. Erase block
6. Show all erase counts
7. Wear-level write
0. Exit
```

| Option | Description | System calls |
|--------|-------------|--------------|
| 1 | Shows block count, pages per block, page size, and total size | `ioctl()` |
| 2 | Shows block ID, erase count, and used pages for a chosen block | `ioctl()` |
| 3 | Writes text at a chosen offset | `lseek()`, `write()` |
| 4 | Reads a chosen number of bytes from a chosen offset | `lseek()`, `read()` |
| 5 | Erases a chosen block | `ioctl()` |
| 6 | Shows the erase count of every block | `ioctl()` |
| 7 | Writes text to the least-worn block (see below) | `ioctl()`, `lseek()`, `write()` |
| 0 | Exits | `close()` |

The application opens the device with `open()` at startup. Invalid input (for example a negative offset or an invalid block ID) is rejected with a message and does not crash the program.

## Wear-Leveling Algorithm

The wear-leveling policy is implemented in the C++ application, not in the driver. For option 7:

1. Get the device information (block count, pages per block, page size).
2. Reject the data if it is larger than one block (8192 bytes).
3. Query the erase count of every block using `VNAND_IOC_GET_BLOCK_INFO`.
4. Select the block with the lowest `erase_count`. If several blocks have the same count, select the smallest block ID.
5. Calculate the block offset: `block_offset = selected_block x block_size`.
6. Call `lseek()` to that offset and `write()` the data at the beginning of the selected block.

Example: with erase counts `3, 1, 0, 0, 0, 0, 0, 0` for blocks 0 to 7, block 2 is selected, and the offset is 2 x 8192 = 16384.

The wear-level write does not change any erase count. Erase counts change only when a block is erased with option 5. It also does not perform automatic erase, page migration, garbage collection, or logical-to-physical mapping.

## Example Workflow

With the module loaded and `sudo ./vnand_manager` running:

| Step | Menu input | Result |
|------|------------|--------|
| 1 | `1` | Shows 8 blocks, 16 pages per block, 512-byte pages, 65536 bytes total |
| 2 | `3`, offset `0`, data `Hello NAND` | `Written: 10 bytes` |
| 3 | `4`, offset `0`, `10` bytes | Reads back `Hello NAND` |
| 4 | `5`, block `0` (three times) | Block 0 erase count becomes 3 |
| 5 | `5`, block `1` (once) | Block 1 erase count becomes 1 |
| 6 | `6` | `Block 0 : 3`, `Block 1 : 1`, blocks 2 to 7 show `0` |
| 7 | `7`, data `WearLevelTest` | Selects block 2; offset 16384; 13 bytes written |
| 8 | `2`, block `2` | Erase count 0, used pages 1 |
| 9 | `0` | Exits |

The ioctl test utility offers a quick check of the same commands. Run `sudo ./tests/ioctl_test info`, `block <id>`, `erase <id>`, or `demo <id>` from the project root.

## Testing Summary

The following tests were performed and passed (details in [`docs/testing.md`](docs/testing.md)):

| Area | Result |
|------|--------|
| Device information (8 blocks, 16 pages, 512 bytes, 65536 bytes) | Pass |
| Write and read back (`FirstRun`, offset 0, 8 bytes) | Pass |
| Block erase (erase count increases, used pages reset to 0) | Pass |
| Block information | Pass |
| Invalid inputs (block ID 99, offset -1, read sizes 0 and 5000, erase of block 99) rejected safely | Pass |
| Wear-leveling (block 2 selected, offset 16384, 13 bytes written) | Pass |
| Integration of the C++ application, `/dev/virtualnand`, the driver, and the simulated NAND | Pass |
| Reliability (repeated open/close, no crash or hang) | Pass |
| Module reload (counters persist while loaded; state resets after `rmmod` and `insmod`) | Verified |
| Kernel log check (no panic, Oops, segfault, or fatal error) | Pass |

## Limitations

This project is a simplified educational simulator. It does not include:

- A full Flash Translation Layer (FTL)
- Logical-to-physical mapping
- Garbage collection
- Page migration
- Bad-block management
- Persistent NAND contents after module unload
- A background daemon
- A GUI
- Networking
- A database

Other points to note:

- NAND data and metadata are held in kernel memory, so they are reset after the module is unloaded and loaded again.
- The geometry (8 blocks, 16 pages per block, 512-byte pages) is fixed in the code.
- The wear-leveling policy uses erase counts only. It does not check whether the selected block already contains data, and the driver does not block a write to a page that has already been written.

## Technologies / Concepts Demonstrated

- Linux kernel module and character device driver (`file_operations`, `alloc_chrdev_region`, `cdev`, device class and node)
- `copy_to_user()` and `copy_from_user()` for safe data transfer between user space and kernel space
- `ioctl` interface design with a shared header (`_IOR`, `_IOW`, `_IOWR`)
- Linux system programming: `open()`, `close()`, `read()`, `write()`, `lseek()`, `ioctl()`
- C++ user-space programming (`std::string`, `std::vector`, input validation)
- NAND flash organization: blocks, pages, block erase, and `0xFF` erased state
- Wear leveling based on erase counts
- Mutex-based protection of shared driver state
- Linux kbuild, Makefiles, `dmesg` debugging
- Git and GitHub version control

## Documentation References

| Document | Description |
|----------|-------------|
| [`docs/requirements.md`](docs/requirements.md) | Functional and non-functional requirements, scope, constraints |
| [`docs/architecture.md`](docs/architecture.md) | System architecture and data flows |
| [`docs/development_plan.md`](docs/development_plan.md) | Development stages and status |
| [`docs/interface_design.md`](docs/interface_design.md) | Device file, system calls, and ioctl interface |
| [`docs/data_structures.md`](docs/data_structures.md) | NAND buffer, block metadata, and page metadata |
| [`docs/uml.md`](docs/uml.md) | UML-style diagrams |
| [`docs/testing.md`](docs/testing.md) | Test cases and results |
