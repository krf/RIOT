MODULE = cbor

ifeq (,$(filter native,$(BOARD)))
	CFLAGS += -DCBOR_NO_CTIME -DCBOR_NO_FLOAT
endif

include $(RIOTBASE)/Makefile.base
