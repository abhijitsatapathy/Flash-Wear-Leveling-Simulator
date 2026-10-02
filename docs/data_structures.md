# Data Structures

**Project:** Flash Memory Wear-Leveling Simulator & Storage Hub

---

## 1. Data Structure Overview

The simulated NAND device is held entirely in kernel memory by the Linux driver. It uses two separate parts:

| Part | Purpose |
|------|---------|
| Simulated NAND data buffer | One contiguous memory area that holds the stored bytes |
| Metadata | Small bookkeeping structures that describe each block and each page |

The metadata does not store any data. It only records state and counters. Both parts are protected by a single mutex in the driver.

```
Driver memory
├── NAND data buffer   (65536 bytes, one contiguous area)
└── Metadata           (8 block records, each with 16 page records)
```

---

## 2. Virtual NAND Geometry

| Property | Value |
|----------|-------|
| Blocks | 8 |
| Pages per block | 16 |
| Page size | 512 bytes |
| Block size | 8192 bytes (16 x 512) |
| Total pages | 128 (8 x 16) |
| Total capacity | 65536 bytes (8 x 16 x 512) |

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

---

## 3. Simulated NAND Data Buffer

- The NAND data is stored in one contiguous kernel-memory buffer of 65536 bytes.
- The buffer is allocated when the module is loaded and freed when the module is unloaded.
- At load time every byte is set to the erased value `0xFF`.
- There is no separate data buffer for individual pages or blocks. A block or page is simply a fixed range inside this one buffer.

| Item | Starts at byte | Size |
|------|----------------|------|
| Block `b` | `b x 8192` | 8192 bytes |
| Page `p` of block `b` | `b x 8192 + p x 512` | 512 bytes |

---

## 4. Page Metadata

Each page has one metadata record:

| Field | Meaning |
|-------|---------|
| `state` | Whether the page is free (`PAGE_FREE`) or has been written (`PAGE_WRITTEN`) |
| `program_count` | Number of write operations that have affected the page since the last erase of its block |

A page is identified by its position in the block's `pages[]` array. There is no separate page ID field.

---

## 5. Block Metadata

Each block has one metadata record:

| Field | Meaning |
|-------|---------|
| `block_id` | Index of the block (0 to 7) |
| `erase_count` | Number of times the block has been erased |
| `used_pages` | Number of pages in the block whose state is written |
| `pages[]` | Array of 16 page metadata records |

The metadata for all blocks is kept in a fixed array of 8 block records.

At module load, every block starts with `erase_count = 0` and `used_pages = 0`, and every page starts as free with `program_count = 0`.

---

## 6. Block/Page Relationship

```
Block record (one per block)
├── block_id
├── erase_count
├── used_pages
└── pages[0..15]
      ├── state
      └── program_count
```

A byte offset in the device maps to a block and a page by simple division:

```
block           = offset / 8192
page in block   = (offset % 8192) / 512
```

Example: offset 16384 is in block 2 (16384 / 8192), page 0 ((16384 % 8192) / 512).

---

## 7. Metadata Update During Write

When data is written:

1. The data is copied into the main NAND buffer at the file offset.
2. Each page touched by the write is marked as written.
3. The `program_count` of each touched page is increased by one.
4. The block's `used_pages` value is increased the first time one of its pages changes from free to written.

A write that crosses a page boundary updates every page it touches. A page that is written again keeps its written state, and its `program_count` increases again.

---

## 8. Metadata Update During Block Erase

When a block is erased:

1. All 8192 bytes of the block in the NAND buffer are restored to `0xFF`.
2. The state of every page in the block is reset to free.
3. The `program_count` of every page in the block is reset to `0`.
4. The block's `used_pages` is reset to `0`.
5. The block's `erase_count` is increased by one.

`erase_count` is the only value that is not reset by an erase. It is the wear counter for the block, and it changes only when a block is erased.

---

## 9. Wear-Leveling Information

Wear leveling is implemented in the C++ user-space application, not in the driver.

- The application asks the driver for each block's `erase_count` through the ioctl interface.
- It then applies this selection rule:

```
Select the block with the lowest erase_count.
If multiple blocks have the same erase_count,
select the block with the smallest block ID.
```

- The application keeps no mapping table and no additional wear-leveling data structure. The driver's `erase_count` values are the only wear information used.

Example: with erase counts 3, 1, 0, 0, 0, 0, 0, 0 for blocks 0 to 7, block 2 is selected.

---

## 10. Data Structure Limitations

- The data buffer and metadata live in kernel memory only, so their state is lost when the module is unloaded.
- The geometry (8 blocks, 16 pages per block, 512-byte pages) is fixed in the code.
- Page state and `program_count` are bookkeeping information. The driver does not block a write to a page that is already written.
- There is no logical-to-physical mapping table, no Flash Translation Layer structure, no garbage-collection metadata, no bad-block table, no page-migration structure, and no persistent storage metadata.
