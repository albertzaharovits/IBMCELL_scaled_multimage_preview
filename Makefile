DIRS		:= 	spu
PROGRAM_ppu	:= 	program

IMPORTS         = spu/lib_spu.a -lspe2 -lpthread -lmisc

ifdef CELL_TOP
	include $(CELL_TOP)/buildutils/make.footer
else
	include /opt/cell/sdk/buildutils/make.footer
endif
