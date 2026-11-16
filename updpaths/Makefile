XBE_TITLE ?= updpaths
GEN_XISO = $(XBE_TITLE).iso
SRCS = $(CURDIR)/main.c
NXDK_DIR ?= $(CURDIR)/../nxdk

ifdef ONLY_PHASE
CFLAGS += -DONLY_PHASE=$(ONLY_PHASE)
endif

include $(NXDK_DIR)/Makefile
