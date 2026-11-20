#---------------------------------------------------------------------------------
# HK3DS - Hollow Knight demake for 3DS (personal project)
# Build with devkitPro:  make
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET      := hk3ds
BUILD       := build
SOURCES     := source
DATA        := data
INCLUDES    := source
ROMFS       := romfs

APP_TITLE       := Hollow Knight 3DS
APP_DESCRIPTION := Fan demake (personal use)
APP_AUTHOR      := HK3DS
ICON            := icon.png
RSF             := cia/hk3ds.rsf
BANNER          := cia/banner.png

# make HD=1 [cia]: the HD build for the New 3DS family (top screen at 800x240, art from romfs_hd), its own title
# so it installs next to the standard one. Its .3dsx carries no romfs (the CIA is the HD deliverable).
ifeq ($(HD),1)
TARGET          := hk3ds_hd
BUILD           := build_hd
ROMFS           := romfs_hd
APP_TITLE       := Hollow Knight 3DS HD
APP_DESCRIPTION := Fan demake, HD (New 3DS)
ICON            := icon_hd.png
RSF             := cia/hk3ds_hd.rsf
BANNER          := cia/banner_hd.png
endif

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  := -g -Wall -O2 -mword-relocations -ffunction-sections -fno-strict-aliasing \
           $(ARCH) $(INCLUDE) -D__3DS__
ifeq ($(HD),1)
CFLAGS  += -DHK_HD
endif
CFLAGS  += -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable
# every fopen of a romfs:/ file checks for a data patch on the SD card first (source/patch.c)
CFLAGS  += -include hkfile.h
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11
ASFLAGS := -g $(ARCH)
LDFLAGS  = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS    := -lcitro2d -lcitro3d -lctru -lm
LIBDIRS := $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------
export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD   := $(CC)
export OFILES_SOURCES := $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN     := $(addsuffix .o,$(BINFILES))
export OFILES         := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES         := $(addsuffix .h,$(subst .,_,$(BINFILES)))
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export APP_ICON := $(TOPDIR)/$(ICON)
export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
ifneq ($(HD),1)
export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx : $(OUTPUT).elf $(OUTPUT).smdh
$(OUTPUT).elf  : $(OFILES)
$(OFILES_SOURCES) : $(HFILES)

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
# CIA (needs bannertool + makerom on PATH):  make cia
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
.PHONY: cia
cia: all
	@bannertool makebanner -i $(BANNER) -a cia/banner.wav -o $(BUILD)/banner.bnr
	@bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i $(ICON) -o $(BUILD)/icon.icn
	@# the data's version: a later code patch over this install can tell it's older (source/patch.c)
	@V=$$(sed -n 's/.*HK3DS_VERSION "\(.*\)"/\1/p' source/version.h); R=$$(sed -n 's/^ *RootPath *: *//p' $(RSF)); rm -f $$R/text/version.txt; echo $$V > $$R/text/version.txt
	@makerom -f cia -o $(TARGET).cia -elf $(TARGET).elf -rsf $(RSF) -icon $(BUILD)/icon.icn -banner $(BUILD)/banner.bnr -exefslogo -target t
	@echo built ... $(TARGET).cia
endif
