CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -I../include
TARGET   := vnand_manager

all: $(TARGET)

$(TARGET): main.cpp ../include/virtual_nand_ioctl.h
	$(CXX) $(CXXFLAGS) -o $@ main.cpp

clean:
	rm -f $(TARGET)

.PHONY: all clean
