CONTIKI_PROJECT = border-router
all: $(CONTIKI_PROJECT)
CONTIKI = ../../..

# The BR is either native or embedded, and in the latter case must support SLIP
# PLATFORMS_EXCLUDE = nrf52dk z1

# Include RPL BR module
# MODULES += os/services/rpl-border-router
# Include optional target-specific module
include $(CONTIKI)/Makefile.identify-target
MODULES_REL += $(TARGET)

PROJECTDIRS += ../node
PROJECT_SOURCEFILES += jota-torrent.c
PROJECT_SOURCEFILES += jota-node.c cmp.c

include $(CONTIKI)/Makefile.include
