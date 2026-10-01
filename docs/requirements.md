# Project Requirements

## Functional Requirements

1. Create a virtual NAND storage device.
2. Represent storage using blocks and pages.
3. Support simulated read operations.
4. Support simulated write operations.
5. Support erase operations.
6. Maintain erase count for each block.
7. Apply a basic wear-leveling algorithm.
8. Provide Linux device-driver interaction.
9. Provide a C++ user-space management program.
10. Display wear statistics and operation results.

## Non-Functional Requirements

1. The project must run only on Linux.
2. The implementation must use only C/C++.
3. No physical hardware is required.
4. The system should be modular and easy to test.
5. Invalid operations should not crash the program.
6. The project should be easy to demonstrate from the command line.
