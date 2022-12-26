ifeq ($(filter UART16550, $(HW_MODULES)),)

include $(UART16550_DIR)/config.mk

#add itself to HW_MODULES list
HW_MODULES+=UART16550


UART16550_INC_DIR:=$(UART16550_HW_DIR)/include
UART16550_SRC_DIR:=$(UART16550_HW_DIR)/src

USE_NETLIST ?=0

#include files
VHDR+=$(wildcard $(UART16550_INC_DIR)/*.vh)

#hardware include dirs
INCLUDE+=$(incdir). $(incdir)$(UART16550_INC_DIR)

#sources
VSRC+=$(wildcard $(UART16550_SRC_DIR)/*.v)

uart16550-hw-clean: uart16550-gen-clean
	@rm -f *.v *.vh

.PHONY: uart16550-hw-clean

endif
