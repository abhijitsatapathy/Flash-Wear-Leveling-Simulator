# UML Diagrams

**Project:** Flash Memory Wear-Leveling Simulator & Storage Hub

---

## 1. UML Overview

The diagrams in this document are simplified, UML-style views of the implemented system, drawn in ASCII so they display correctly on GitHub. They are meant to explain the architecture, not to describe a full SSD design.

The implementation does not use large C++ classes. The C++ application is a set of functions in `cpp/main.cpp`, and the driver is a set of functions in `driver/virtual_nand_driver.c`. For that reason, the "class-style" diagram in Section 3 shows **responsibility groups** (the functions that actually exist), not classes.

| Item | Status |
|------|--------|
| C++ functions in `cpp/main.cpp` (menu actions, `findLeastWornBlock()`, `wearLevelWrite()`) | Implemented |
| Driver functions in `driver/virtual_nand_driver.c` (`vnand_read()`, `vnand_write()`, `vnand_ioctl()`, `nand_erase_block()`, ...) | Implemented |
| Device file `/dev/virtualnand` and the three ioctl commands | Implemented |
| "C++ Storage Manager" and "Linux Virtual NAND Driver" as diagram boxes | Conceptual grouping only, not C++ classes |
| `VirtualNAND` and `WearLevelManager` classes | Not implemented as classes. Their roles are covered by the driver functions and by `findLeastWornBlock()` |

---

## 2. Component Diagram

```
User
  |
  v
C++ User-Space Manager
  |
  |  open / read / write / lseek / ioctl / close
  v
/dev/virtualnand
  |
  v
Linux Character Driver
  |
  +--> NAND Data Buffer       (65536 bytes)
  |
  +--> Block/Page Metadata    (8 blocks x 16 pages)
```

The C++ manager talks to the driver only through `/dev/virtualnand` and Linux system calls. It never accesses kernel memory directly.

| Property | Value |
|----------|-------|
| Blocks | 8 |
| Pages per block | 16 |
| Page size | 512 bytes |
| Block size | 8192 bytes |
| Total capacity | 65536 bytes |

---

## 3. Structural / Class-Style Diagram

Each box lists the functions that exist in that part of the project. The boxes are groupings of responsibilities, not C++ classes.

```
+----------------------------------------------
| C++ Storage Manager       <<user-space application>>
| (cpp/main.cpp)
+----------------------------------------------
| - showDeviceInfo()
| - showBlockInfo()
| - writeData()
| - readData()
| - eraseBlock()
| - showAllEraseCounts()
| - findLeastWornBlock()
| - wearLevelWrite()
+----------------------------------------------
        |
        |  depends on, through the device file /dev/virtualnand
        |  (open / close / read / write / lseek / ioctl)
        v
+----------------------------------------------
| Linux Virtual NAND Driver       <<kernel module>>
| (driver/virtual_nand_driver.c)
+----------------------------------------------
| - vnand_open()
| - vnand_release()
| - vnand_read()
| - vnand_write()
| - - vnand_llseek()
| - vnand_ioctl()
| - nand_erase_block()
| - nand_mark_pages_written()
+----------------------------------------------
        |
        |  owns
        v
+----------------------------------------------
| Driver data                    (kernel memory)
+----------------------------------------------
| - nand_buffer      (65536 bytes)
| - nand_meta[8]     (struct nand_block_meta)
|     block_id, erase_count, used_pages
|     pages[16]      (struct nand_page_meta)
|         state, program_count
+----------------------------------------------
```

Not shown: small helper functions in `cpp/main.cpp` (reading input, thin wrappers around `ioctl()`, printing the menu) and the module init and exit functions in the driver.

The shared header `include/virtual_nand_ioctl.h` is included by both sides. It defines the device name, the three ioctl commands (`VNAND_IOC_GET_DEVICE_INFO`, `VNAND_IOC_GET_BLOCK_INFO`, `VNAND_IOC_ERASE_BLOCK`), and the structures exchanged through them.

---

## 4. Sequence Diagram - Normal Write

```
  User         C++ Manager       /dev/virtualnand       Linux driver        NAND buffer/meta
   |                |                    |                    |                     |
   |-offset,data--->|                    |                    |                     |
   |                |-lseek(offset)----->|                    |                     |
   |                |                    |-.llseek----------->|                     |
   |                |-write(data)------->|                    |                     |
   |                |                    |-.write------------>|                     |
   |                |                    |                    | check bounds        |
   |                |                    |                    |-copy_from_user()--->|
   |                |                    |                    |-update metadata---->|
   |                |                    |<-bytes written-----|                     |
   |                |<-bytes written-----|                    |                     |
   |<-Written: N----|                    |                    |                     |
```

- `lseek()` sets the file position that the following `write()` starts from.
- The driver clips the write if it would pass the end of the device, and fails with "no space left" if the position is already at the end.
- "Update metadata" means the written pages are marked as written, their program counts are increased, and the block's `used_pages` is updated.

---

## 5. Sequence Diagram - Block Erase

```
  User         C++ Manager       /dev/virtualnand       Linux driver        NAND buffer/meta
   |                |                    |                    |                     |
   |-block ID------>|                    |                    |                     |
   |                |-ioctl(ERASE_BLOCK)>|                    |                     |
   |                |                    |-.unlocked_ioctl--->|                     |
   |                |                    |                    | copy_from_user(id)  |
   |                |                    |                    | validate block ID   |
   |                |                    |                    |-memset block 0xFF-->|
   |                |                    |                    |-reset page meta---->|
   |                |                    |                    |-erase_count + 1---->|
   |                |                    |<-return 0----------|                     |
   |                |<-ioctl result------|                    |                     |
   |<-Block X erased|                    |                    |                     |
```

- `ioctl(ERASE_BLOCK)` stands for `ioctl(fd, VNAND_IOC_ERASE_BLOCK, &block_id)`.
- If the block ID is invalid, the driver stops after validation and returns `-EINVAL`. The application then prints an error instead of "erased".
- The erase restores all 8192 bytes of the block to `0xFF`, resets the page states, page program counts, and `used_pages`, and increases `erase_count`.

---

## 6. Sequence Diagram - Wear-Level Write

All calls from the manager to the driver go through `/dev/virtualnand`.

```
  User         C++ Manager         Linux driver         NAND buffer/meta
   |                |                         |                       |
   |-data----------->|                         |                       |
   |                |-ioctl(GET_DEVICE_INFO)->|                       |
   |                |<-device info------------|                       |
   |                | block_size = 16 x 512   |                       |
   |                | for each block:         |                       |
   |                |-ioctl(GET_BLOCK_INFO)-->|                       |
   |                |<-block info-------------|                       |
   |                | end for                 |                       |
   |                | compare erase_count     |                       |
   |                | choose least-worn       |                       |
   |                | offset = block x 8192   |                       |
   |                |-lseek(offset)---------->|                       |
   |                |-write(data)------------>|                       |
   |                |                         |-store data+metadata-->|
   |                |<-bytes written----------|                       |
   |<-result--------|                         |                       |
```

- The block selection happens in the C++ manager. The driver only supplies the erase counts and stores the data.
- Data larger than one block (8192 bytes) is rejected by the manager before any write.
- A wear-level write does not change any erase count. Only a block erase does.

---

## 7. State Machine Diagram

```
MODULE_UNLOADED
      |
      | insmod
      v
MODULE_LOADED
      |
      | module init finishes, /dev/virtualnand exists
      v
DEVICE_READY
      |
      | open()
      v
DEVICE_OPEN <------------------------+
      |                              |
      +--> READ  --- done -----------+
      +--> WRITE --- done -----------+
      +--> IOCTL --- done -----------+
      |       |
      |       +--> GET_INFO   (device info, block info)
      |       +--> ERASE      (erase one block)
      |
      | close()
      v
DEVICE_CLOSED
      |
      | device is ready again and can be reopened
      v
DEVICE_READY
      |
      | rmmod (only while the device is not open)
      v
MODULE_UNLOADED
```

Multiple operations can occur while the device stays open. After each read, write, or ioctl, the device is still in the `DEVICE_OPEN` state until `close()` is called. For example, the C++ manager keeps one descriptor open for its whole menu session.

---

## 8. Wear-Level Selection Flow

```
Start
  |
  v
Get block count            (ioctl GET_DEVICE_INFO)
  |
  v
block = 0
  |
  v
Read erase_count of block  (ioctl GET_BLOCK_INFO)
  |
  v
First block, or erase_count < smallest so far?
  |
  +-- yes --> remember this block and its erase_count
  |
  +-- no  --> keep the current choice
              (equal erase_count = tie: the smaller block ID,
               which was visited first, is kept)
  |
  v
More blocks?
  |
  +-- yes --> block = block + 1, read its erase_count
  |
  +-- no
  |
  v
Selected block = remembered block
  |
  v
offset = selected block x block size
  |
  v
lseek(offset), then write(data)
  |
  v
End
```

Example: with erase counts 3, 1, 0, 0, 0, 0, 0, 0 for blocks 0 to 7, block 2 is selected. The offset is 2 x 8192 = 16384.

---

## 9. Diagram Notes / Limitations

- These are simplified UML-style diagrams, drawn in ASCII.
- They describe the implemented architecture: a C++ user-space manager, the `/dev/virtualnand` device file, a Linux character driver, and simulated NAND memory with block and page metadata.
- The boxes in the structural diagram are groupings of existing functions, not C++ classes.
- They do not represent a full SSD or Flash Translation Layer design. There are no mapping tables, garbage collection, page migration, bad-block management, background daemon, or persistent storage in this project.
