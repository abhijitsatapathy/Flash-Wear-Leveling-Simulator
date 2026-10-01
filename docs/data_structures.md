# Data Structures

## Virtual NAND Layout

The virtual NAND storage will be divided into blocks.

Each block will contain multiple pages.

## Block Metadata

Each block will maintain:

- Block ID
- Erase Count
- Used Page Count
- Block Status

## Page Metadata

Each page will maintain:

- Page ID
- Data Buffer
- Valid/Empty Status

## Wear-Leveling Information

The wear-leveling engine will compare erase counts and select a suitable block with lower wear for future operations.
