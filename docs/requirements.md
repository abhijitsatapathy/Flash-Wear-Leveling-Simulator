# Project Requirements

**Project:** Flash Memory Wear-Leveling Simulator & Storage Hub

---

## 1. Project Overview

This project simulates a small NAND flash storage device entirely in software on Linux. A Linux kernel module implements a character device, `/dev/virtualnand`, that keeps the simulated flash memory and its metadata in kernel memory. A C++ user-space program communicates with the driver through standard system calls and applies a simple wear-leveling policy.

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

The simulated device uses the following fixed layout:

| Property | Value |
|----------|-------|
| Erase blocks | 8 |
| Pages per block | 16 |
| Page size | 512 bytes |
| Total capacity | 65536 bytes (8 x 16 x 512) |

---

## 2. Objective

The objective is to build a Linux-only, C/C++ project that demonstrates:

- Linux device driver concepts (character device, `file_operations`, `ioctl`)
- Linux system programming (`open`, `close`, `read`, `write`, `lseek`, `ioctl`)
- C++ user-space programming
- How NAND flash is organized (blocks and pages) and why wear leveling matters
- Software/hardware interaction, using a software-simulated device

---

## 3. Functional Requirements

| ID | Requirement |
|----|-------------|
| FR-1 | Provide a virtual NAND storage device, `/dev/virtualnand`, implemented as a Linux kernel module. |
| FR-2 | Represent the storage as blocks and pages with fixed geometry (8 blocks, 16 pages per block, 512-byte pages). |
| FR-3 | Support read operations at a chosen offset, with bounds checking. |
| FR-4 | Support write operations at a chosen offset, with bounds checking. |
| FR-5 | Support `llseek()` so the file offset can be set before reading or writing. |
| FR-6 | Support erasing one complete block, returning its bytes to the erased value `0xFF`. |
| FR-7 | Maintain an erase counter for each block, increased on every successful erase. |
| FR-8 | Track the number of used pages in each block; the count resets when the block is erased. |
| FR-9 | Provide ioctl commands, defined in a shared header, to get device information, get block information (block ID, erase count, used pages), and erase a block. |
| FR-10 | Provide a C++ user-space manager with a menu to show device and block information, write data, read data, erase a block, and show all erase counts. |
| FR-11 | Implement a simple wear-leveling policy in the C++ program: choose the block with the lowest erase count, and use the smallest block ID when erase counts are equal. |
| FR-12 | Provide a wear-level write that stores data at the start of the selected block and rejects data larger than one block. |
| FR-13 | Display wear statistics (erase counts) and the result of each operation. |

---

## 4. Non-Functional Requirements

| ID | Requirement |
|----|-------------|
| NFR-1 | The project must run only on Linux. |
| NFR-2 | The implementation must use only C and C++. |
| NFR-3 | No physical hardware is required. |
| NFR-4 | The design should be modular (driver, shared header, user-space application) and easy to test. |
| NFR-5 | Invalid operations (for example an invalid block ID, a negative offset, or an invalid read size) must be rejected safely without crashing the program or the driver. |
| NFR-6 | The project should be easy to build and demonstrate from the command line. |
| NFR-7 | The code should be simple and readable so it can be explained in a short evaluation. |

---

## 5. System / Software Requirements

| Item | Requirement |
|------|-------------|
| Operating system | Ubuntu Linux |
| Compilers | `gcc` (kernel module, C), `g++` (C++ application) |
| Build tools | `make`, `build-essential`, Linux kbuild |
| Kernel support | Kernel headers matching the running kernel (`linux-headers-$(uname -r)`), `kmod` (`insmod`, `rmmod`, `lsmod`), `libelf-dev` |
| Privileges | `sudo` to load or unload the module and to access `/dev/virtualnand` |
| Optional tools | `gdb`, `valgrind`, `strace`, `ltrace` for debugging and inspection |

---

## 6. Hardware Requirements

- **No physical NAND flash device is required.**
- **No embedded board is required.**
- NAND flash behavior is simulated entirely in software.
- The project runs on Ubuntu Linux in a VirtualBox virtual machine, on an ordinary computer.

---

## 7. Scope

The project includes:

- A Linux kernel module providing the `/dev/virtualnand` character device
- Simulated NAND memory and per-block metadata held in kernel memory
- Read, write, `llseek`, and block erase operations
- Erase counters and used-page tracking for each block
- A shared ioctl header used by both the driver and the C++ application
- A C++ management application using `open()`, `close()`, `read()`, `write()`, `lseek()`, and `ioctl()`
- A simple erase-count-based wear-leveling policy with least-worn block selection
- Testing and documentation of the above

---

## 8. Out of Scope

The following are not implemented:

- A full Flash Translation Layer (FTL)
- Logical-to-physical address mapping
- Garbage collection
- Page migration
- Persistent NAND state after the module is reloaded
- Bad-block management
- A background daemon
- Multithreading
- A graphical user interface
- Networking
- A database

---

## 9. Expected Outcomes

- The kernel module builds, loads, creates `/dev/virtualnand`, and unloads cleanly.
- Data can be written to and read back from the simulated NAND at chosen offsets.
- A block can be erased, its data returns to `0xFF`, and its erase count increases.
- The C++ application can display device information, block information, and all erase counts.
- The wear-level write selects the least-worn block, using the smallest block ID when counts are equal.
- Invalid input is rejected without crashing the application or the driver.
- The project demonstrates Linux device drivers, system programming, and C++ working together.

---

## 10. Constraints and Assumptions

**Constraints**

- Only Linux, and only C/C++, may be used.
- The NAND geometry is fixed at compile time (8 blocks, 16 pages per block, 512-byte pages).
- The simulated NAND and its metadata are held in kernel memory, so their state is lost when the module is unloaded. Erase counters persist only while the module stays loaded.
- The wear-leveling policy is a simplified simulation. It uses erase counts only and does not model a complete flash controller.
- Loading and unloading the kernel module require root privileges. In the current setup, the C++ manager is run with sudo to access /dev/virtualnand>

**Assumptions**

- The system runs Ubuntu Linux with kernel headers installed for the running kernel.
- Only one simulated NAND device is provided, and it is used for learning and demonstration, not as a real storage product.
