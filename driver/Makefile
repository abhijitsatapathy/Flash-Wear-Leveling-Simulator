# Kbuild: build virtual_nand_driver.c into virtual_nand_driver.ko
obj-m += virtual_nand_driver.o

ccflags-y += -I$(src)/../include
# Kernel build tree for the currently running kernel
KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

.PHONY: all clean
