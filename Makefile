PROJECT := sse_mmx_archtest
.DEFAULT_GOAL := all
CC ?= gcc
LD ?= ld
OBJCOPY ?= objcopy
ifeq ($(OS),Windows_NT)
MSYS2 ?= C:/msys64
PYTHON ?= $(MSYS2)/clang64/bin/python.exe
HOSTCC ?= $(MSYS2)/clang64/bin/clang.exe
else
PYTHON ?= python3
HOSTCC ?= cc
endif
OUTPUT ?= dist/$(PROJECT).img

# The harness has no OS/libc. Disable compiler-generated floating-point/SIMD
# so only the explicit probes touch the registers and MXCSR being tested.
CFLAGS := -m32 -std=gnu11 -O2 -ffreestanding -fno-pic -fno-pie \
          -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables \
          -mno-sse -mno-mmx -mno-80387 -Wall -Wextra -Werror -MMD -MP -Iinclude -Iinclude/libc -Ithird_party/fatfs
ASFLAGS := -m32 -ffreestanding -fno-pic -fno-pie -Iinclude
LDFLAGS := -m elf_i386 -nostdlib --emit-relocs

C_SRCS := $(wildcard src/*.c) $(wildcard src/tests/*.c)
S_SRCS := src/entry.S src/faults.S src/tests/baseline.S src/tests/generated_ops.S src/tests/operand_forms.S src/tests/pavgw_probe.S
C_OBJS := $(patsubst src/%.c,build/%.c.o,$(C_SRCS))
S_OBJS := $(patsubst src/%.S,build/%.S.o,$(S_SRCS))
KERNEL_OBJS := $(C_OBJS) $(S_OBJS) build/fatfs.o
# Native Windows builds use the checked driver below. Do not import ELF
# dependency files or let MinGW compile individual PE objects into this tree.
ifneq ($(OS),Windows_NT)
-include $(KERNEL_OBJS:.o=.d)
endif

.PHONY: all image verify verify-hosted verify-framework verify-storage clean distclean regen extract info windows-build
all: verify

# Edit generators, not generated .S files; keep both in sync for source review.
regen: src/tests/generated_ops.S src/tests/operand_forms.S

src/tests/generated_ops.S: tools/gen_cases.py
	"$(PYTHON)" $<

src/tests/operand_forms.S: tools/gen_operand_forms.py
	"$(PYTHON)" $<

ifeq ($(OS),Windows_NT)
# All aliases share one prerequisite, so make -j cannot start competing builds.
all image verify $(OUTPUT): windows-build
windows-build: regen
	"$(PYTHON)" tools/build_windows.py --msys2 "$(MSYS2)" --output "$(OUTPUT)"
else
build/fatfs.o: third_party/fatfs/ff.c third_party/fatfs/ff.h third_party/fatfs/ffconf.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.c.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.S.o: src/%.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

build/kernel.elf: $(KERNEL_OBJS) linker/kernel.ld
	$(LD) $(LDFLAGS) -T linker/kernel.ld -o $@ $(KERNEL_OBJS)

build/kernel.bin: build/kernel.elf
	$(OBJCOPY) -O binary $< $@

build/boot.o: src/boot.S
	@mkdir -p build
	$(CC) $(ASFLAGS) -c $< -o $@

build/boot.elf: build/boot.o linker/boot.ld
	$(LD) $(LDFLAGS) -T linker/boot.ld -o $@ build/boot.o

build/boot.bin: build/boot.elf
	$(OBJCOPY) -O binary $< $@

image: dist/$(PROJECT).img

dist/$(PROJECT).img: build/boot.bin build/kernel.bin tools/build_image.py
	@mkdir -p dist
	"$(PYTHON)" tools/build_image.py --boot build/boot.bin --kernel build/kernel.bin --output $@

verify: image
	"$(PYTHON)" tools/verify_image.py dist/$(PROJECT).img --kernel build/kernel.bin --source src/entry.S --elf build/kernel.elf
	"$(PYTHON)" tools/test_tools.py

endif

# Native CPU checks complement the image's ring-0 fault and boot tests.
verify-hosted:
	"$(PYTHON)" tools/run_hosted.py --cc "$(HOSTCC)"

verify-framework:
	"$(PYTHON)" tools/test_framework.py --cc "$(HOSTCC)"

verify-storage:
	"$(PYTHON)" tools/test_storage.py --cc "$(HOSTCC)"
	"$(PYTHON)" tools/test_platform.py --cc "$(HOSTCC)"

ifeq ($(OS),Windows_NT)
info: windows-build
	"$(MSYS2)/mingw64/bin/size.exe" build/kernel.elf
else
info: build/kernel.elf image
	@echo "Kernel ELF:" && size build/kernel.elf
	@echo "Image:" && ls -lh dist/$(PROJECT).img

endif

# Example: make extract IMAGE=/path/to/86box-run.img [OUT=my-results.txt]
IMAGE ?= dist/$(PROJECT).img
OUT ?= RESULTS.TXT
extract:
	"$(PYTHON)" tools/extract_results.py $(IMAGE) $(OUT)

ifeq ($(OS),Windows_NT)
clean:
	"$(PYTHON)" tools/build_windows.py --clean

distclean:
	"$(PYTHON)" tools/build_windows.py --distclean
else
clean:
	rm -rf build

# Keep generated source files; they are intentionally committed/auditable.
distclean: clean
	rm -rf dist RESULTS.TXT

endif
