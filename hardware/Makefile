#Feel free to edit these!
PROJECT = ledpanel
BUILD_DIR = bin

SHARED_DIR = ../src
CFILES = game.c panel_hw.c

# You shouldn't have to edit anything below here.
DEVICE=stm32f303ret6
OOCD_FILE = board/st_nucleo_f3.cfg

VPATH += $(SHARED_DIR)
INCLUDES += $(patsubst %,-I%, . $(SHARED_DIR))
# OPENCM3_DIR=../../libopencm3

ifndef OPENCM3_DIR
$(error Set OPENCM3_DIR to a built libopencm3 checkout; see hardware/README.md)
endif
include $(OPENCM3_DIR)/mk/genlink-config.mk
ifeq ($(wildcard ./rules.mk),)
$(error Missing coursework rules.mk; see hardware/README.md)
endif
include ./rules.mk
include $(OPENCM3_DIR)/mk/genlink-rules.mk
