# Testing Report

**Project:** Flash Memory Wear-Leveling Simulator & Storage Hub
**Platform:** Ubuntu Linux (tested over SSH)
**Components under test:** C++ management application (`vnand_manager`), `/dev/virtualnand`, Linux character device driver, simulated NAND memory

This document records the functional, edge-case, integration, and reliability tests performed on the project, and their results.

---

## 1. Functional Testing

These tests confirm that each core feature works as designed.

| # | Test | Input / Action | Expected Result | Result |
|---|------|----------------|-----------------|--------|
| 1.1 | Device information | Menu option 1 (`VNAND_IOC_GET_DEVICE_INFO`) | 8 blocks, 16 pages per block, 512 bytes per page, 65536 bytes total | Pass |
| 1.2 | Write and read | Write `FirstRun` at offset `0`, then read back from offset `0` | 8 bytes read, content matches `FirstRun` | Pass |
| 1.3 | Block information | Query a valid block (`VNAND_IOC_GET_BLOCK_INFO`) | Block ID, erase count, and used pages are displayed | Pass |
| 1.4 | Block erase | Erase a block (`VNAND_IOC_ERASE_BLOCK`) | `erase_count` increases and `used_pages` resets to `0` | Pass |

**Capacity check:**

```
TOTAL_SIZE = BLOCK_COUNT x PAGES_PER_BLOCK x PAGE_SIZE
           = 8 x 16 x 512
           = 65536 bytes
```

---

## 2. Invalid Input Testing

These tests confirm that bad input is rejected safely, without crashing the application or the driver.

| # | Test | Input | Expected Result | Result |
|---|------|-------|-----------------|--------|
| 2.1 | Invalid block ID (info) | Block ID `99` | Request rejected safely | Pass |
| 2.2 | Negative offset | Offset `-1` | Request rejected safely | Pass |
| 2.3 | Invalid read size (zero) | Read size `0` | Request rejected | Pass |
| 2.4 | Invalid read size (too large) | Read size `5000` | Request rejected | Pass |
| 2.5 | Invalid erase | Erase block `99` | Request rejected safely | Pass |

Validation happens in two places:

- **C++ application:** checks offsets and read sizes before making any system call.
- **Kernel driver:** independently checks block IDs and byte ranges and returns standard error codes (for example `-EINVAL`), so it stays safe even if a user program sends bad values.

---

## 3. Wear-Leveling Testing

This test checks the least-worn block selection policy implemented in the C++ application (menu option 7).

**Setup (erase counts):**

| Block | Erase count |
|-------|-------------|
| 0 | 3 |
| 1 | 1 |
| 2 | 0 |
| 3 | 0 |
| 4 | 0 |
| 5 | 0 |
| 6 | 0 |
| 7 | 0 |

**Test:** Wear-level write with data `WearLevelTest`.

| Check | Expected | Observed | Result |
|-------|----------|----------|--------|
| Selected block | Block 2 (lowest erase count; smallest ID among the tied blocks 2-7) | Block 2 | Pass |
| Block size | 16 x 512 = 8192 bytes | 8192 bytes | Pass |
| Block offset | 2 x 8192 = 16384 | 16384 | Pass |
| Bytes written | 13 (`WearLevelTest`) | 13 | Pass |

The test confirms both parts of the selection rule: the block with the lowest erase count is chosen, and ties are broken in favor of the smallest block ID.

---

## 4. Integration Testing

This test confirms that all layers of the system work together end to end.

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

**Result: Pass.** The C++ application communicated correctly with `/dev/virtualnand`, the Linux character driver, and the simulated NAND memory.

**System calls demonstrated:**

| System call | Purpose in this project |
|-------------|-------------------------|
| `open()` | Opens `/dev/virtualnand` |
| `close()` | Releases the file descriptor on exit |
| `read()` | Reads data from the simulated NAND |
| `write()` | Writes data to the simulated NAND |
| `lseek()` | Sets the file offset before reads and writes |
| `ioctl()` | Device info, block info, and block erase |

---

## 5. Reliability Testing

| Test | Observation | Result |
|------|-------------|--------|
| Repeatedly open and close the C++ manager | The device continued to work after every cycle | Pass |
| ioctl calls after repeated sessions | ioctl calls remained functional | Pass |
| Stability | No crash or hang observed | Pass |

---

## 6. Module Reload Testing

| Scenario | Observed Behavior |
|----------|-------------------|
| Kernel module stays loaded | Erase counters persist across application sessions |
| `rmmod` followed by `insmod` | Simulated NAND state resets |

**Why this is expected:** the simulated NAND storage and its metadata live in kernel memory. They exist for as long as the module is loaded and are released when the module is unloaded. This design does not provide persistent storage across module reloads, which is acceptable for this simulator.

---

## 7. Kernel Log Verification

Kernel messages were checked with:

```bash
sudo dmesg | tail -n 60
```

**Result:** No kernel panic, Oops, segfault, or fatal error was observed.

---

## 8. Overall Result

| Area | Status |
|------|--------|
| Functional testing | Pass |
| Invalid input testing | Pass |
| Wear-leveling testing | Pass |
| Integration testing | Pass |
| Reliability testing | Pass |
| Module reload behavior | Verified (as expected) |
| Kernel log verification | Pass |

**Conclusion:** All planned tests passed. The Linux character driver, the simulated NAND storage, the erase and metadata handling, and the C++ wear-leveling application work correctly together and handle invalid input safely. The system was stable throughout testing.
