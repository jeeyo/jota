CONTIKI_PROJECT = jota-node
all: $(CONTIKI_PROJECT)
CONTIKI = ../../..

# Include optional target-specific module
include $(CONTIKI)/Makefile.identify-target
MODULES_REL += $(TARGET)

PROJECT_SOURCEFILES += jota-torrent.c
PROJECT_SOURCEFILES += cmp.c

MAKE_ROUTING = MAKE_ROUTING_RPL_CLASSIC
include $(CONTIKI)/Makefile.include
