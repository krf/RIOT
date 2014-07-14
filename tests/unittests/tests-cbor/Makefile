MODULE = tests-cbor

ifeq (,$(filter native,$(BOARD)))
	CFLAGS += -DCBOR_NO_CTIME -DCBOR_NO_FLOAT -DCBOR_NO_STREAM_DECODE
endif

include $(RIOTBASE)/Makefile.base
