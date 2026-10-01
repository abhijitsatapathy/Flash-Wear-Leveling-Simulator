# UML Diagrams

## 1. Class Diagram

VirtualNAND
- openDevice()
- readData()
- writeData()
- eraseBlock()
- getStatistics()

WearLevelManager
- selectBlock()
- compareEraseCounts()
- displayWearStatistics()

Relationship:

WearLevelManager --> VirtualNAND


## 2. Sequence Diagram

User
  |
  v
C++ Storage Manager
  |
  | open("/dev/virtualnand")
  v
Linux Device Driver
  |
  | access virtual NAND storage
  v
Blocks / Pages
  |
  v
Return Result
  |
  v
C++ Storage Manager
  |
  v
User


## 3. State Machine Diagram

UNLOADED
   |
   v
LOADED
   |
   v
READY
   |
   +--> OPEN
   |      |
   |      v
   |   READ / WRITE / ERASE
   |      |
   |      v
   |    CLOSED
   |
   v
UNLOADED
