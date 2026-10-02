# Development Plan

**Project:** Flash Memory Wear-Leveling Simulator & Storage Hub

---

## 1. Development Approach

The project follows six training stages and is built incrementally. Each step is compiled, loaded, and tested on Ubuntu Linux before the next step begins, so the project always has a working version.

| Stage | Name |
|-------|------|
| 1 | Project Introduction |
| 2 | Requirements & Development Plan |
| 3 | System Design & Architecture |
| 4 | Initial Implementation & Prototype |
| 5 | Testing, Integration & Improvement |
| 6 | Final Implementation & Presentation |

The implementation is Linux-only and uses only C and C++. NAND flash hardware is simulated in software, so no physical hardware is needed.

---

## 2. Stage 1 - Project Introduction

**Tasks**

- Set up the Linux development environment
- Configure Git and the GitHub repository
- Prepare `README.md`
- Define the project idea, scope, and objectives

**Deliverables**

- Working Linux development environment
- GitHub repository with the initial project structure
- Initial `README.md`

---

## 3. Stage 2 - Requirements & Development Plan

**Tasks**

- Prepare the project requirements
- Define scope and objectives
- Plan the development stages

**Deliverables**

- `docs/requirements.md`
- `docs/development_plan.md`

---

## 4. Stage 3 - System Design & Architecture

**Tasks**

- Prepare the high-level system architecture
- Define kernel-space and user-space components
- Define the communication interface
- Define the project modules
- Prepare UML diagrams and data structures

**Deliverables**

- `docs/architecture.md`
- UML diagrams
- Interface and data-structure design

---

## 5. Stage 4 - Initial Implementation & Prototype

The prototype was built in small steps, and each step was tested before the next one.

| Step | Work | Main files |
|------|------|------------|
| 1 | Minimal Linux kernel module: register a character device, create `/dev/virtualnand`, implement `open()` and `release()`, log load/open/close/unload, unload cleanly | `driver/virtual_nand_driver.c`, `driver/Makefile` |
| 2 | Simulated NAND memory in kernel space (8 blocks, 16 pages per block, 512-byte pages, 65536 bytes total); `read()`, `write()`, file-offset handling, bounds checking | `driver/virtual_nand_driver.c` |
| 3 | Block and page metadata, block erase, erase counters, used-page tracking, ioctl interface, shared ioctl header, small C test utility | `driver/virtual_nand_driver.c`, `include/virtual_nand_ioctl.h`, `tests/ioctl_test.c` |
| 4 | C++ user-space manager: open the device, command-line menu, read/write with `lseek()`, ioctl operations, display of block statistics | `cpp/main.cpp`, `cpp/Makefile` |
| 5 | Simple wear leveling in the C++ application: choose the block with the lowest erase count, use the smallest block ID as the tie-breaker, write to the selected block | `cpp/main.cpp` |

**Deliverables**

- Linux kernel module providing `/dev/virtualnand`
- Shared ioctl header used by the driver and the C++ application
- C++ storage manager with a command-line menu
- Simple erase-count-based wear-leveling policy

---

## 6. Stage 5 - Testing, Integration & Improvement

**Tasks**

- Integrate the C++ application with the Linux driver
- Test read, write, and erase operations
- Test wear-leveling behavior
- Test invalid operations and error handling
- Test repeated opening and closing of the application
- Verify module reload behavior
- Debug and check kernel logs using Linux tools such as `dmesg`
- Document the test results

**Deliverables**

- `docs/testing.md`
- Verified integration of the C++ application, `/dev/virtualnand`, the driver, and the simulated NAND memory

---

## 7. Stage 6 - Final Implementation & Presentation (Remaining)

Stage 6 is the remaining stage. The items below are not yet complete.

- [ ] Update `README.md` with build and execution steps
- [ ] Prepare the final architecture and UML documentation
- [ ] Document limitations and future improvements
- [ ] Verify the GitHub repository structure
- [ ] Perform final demonstration testing
- [ ] Push the final project to GitHub

---

## 8. Git / Version-Control Practice

- The project is kept in a Git repository hosted on GitHub.
- Working, tested code is committed in small, meaningful steps.
- Source code and documentation are committed: the driver, the shared header, the C++ source, the Makefiles, the test utility, and the `docs/` files.
- Generated build outputs, such as the compiled `vnand_manager` executable, are not committed.
- The final project is pushed to GitHub as part of Stage 6.

---

## 9. Documentation and Progress Tracking

- The `docs/` folder holds the requirements, architecture, development plan, and testing documents.
- `README.md` is the entry point and will carry the build and run instructions.
- Progress is tracked stage by stage, and each implementation step is tested before moving on.
- Test results are recorded in `docs/testing.md`.

---

## 10. Current Project Status

| Stage | Status |
|-------|--------|
| 1. Project Introduction | Completed |
| 2. Requirements & Development Plan | Completed |
| 3. System Design & Architecture | Completed |
| 4. Initial Implementation & Prototype | Completed |
| 5. Testing, Integration & Improvement | Completed |
| 6. Final Implementation & Presentation | Remaining |

**Implemented so far:**

- `/dev/virtualnand` character device
- Simulated NAND memory (8 blocks x 16 pages x 512 bytes = 65536 bytes)
- Read and write support with `llseek()`
- Block erase, erase counters, and used-page tracking
- ioctl interface
- C++ user-space manager
- Simple least-worn-block selection, with the smallest block ID as the tie-breaker
- Functional, edge-case, integration, reliability, and module-reload testing

**Not part of this project:** a full Flash Translation Layer, logical-to-physical mapping, garbage collection, page migration, persistent state across module reload, bad-block management, a background daemon, multithreading, a GUI, networking, and a database.
