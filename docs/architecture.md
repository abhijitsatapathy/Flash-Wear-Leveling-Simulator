# System Architecture

## 1. Overview

The Flash Memory Wear-Leveling Simulator & Storage Hub is a Linux-based software project that simulates NAND flash storage behavior without requiring physical hardware.

The system is divided into two main layers:

1. Kernel Space
2. User Space

## 2. Kernel Space Components

The Linux kernel module will provide a virtual NAND storage device.

Main responsibilities:

- Create the virtual device `/dev/virtualnand`
- Simulate flash blocks and pages
- Maintain block metadata
- Maintain erase counters
- Support read operations
- Support write operations
- Support erase operations
- Provide ioctl-based control and statistics

## 3. User Space Components

The user-space application will be written in C++.

Main responsibilities:

- Open and communicate with `/dev/virtualnand`
- Send read, write, and erase requests
- Track and display block statistics
- Apply wear-leveling logic
- Select suitable blocks based on erase count
- Display operation results through a command-line interface

## 4. Communication Interface

Communication between the C++ application and the Linux device driver will use Linux system programming interfaces such as:

- open()
- read()
- write()
- ioctl()
- close()

## 5. High-Level Architecture

C++ User-Space Application
        |
        | open / read / write / ioctl
        v
/dev/virtualnand
        |
-------------------------------
User Space / Kernel Space
-------------------------------
        |
        v
Linux Virtual NAND Device Driver
        |
        v
Virtual NAND Storage
        |
        +-- Flash Blocks
        |
        +-- Pages
        |
        +-- Erase Counters
        |
        +-- Block Metadata
        |
        v
Wear-Leveling Statistics

## 6. Major Modules

- Virtual NAND Linux Device Driver
- Shared Driver/User Interface
- C++ Storage Manager
- Wear-Leveling Engine
- Testing Module
- Documentation Module
