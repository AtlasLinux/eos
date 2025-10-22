CC := gcc
CFLAGS := -Ignu-efi/inc -fpic -ffreestanding -fno-stack-protector -fno-stack-check -fshort-wchar -mno-red-zone -maccumulate-outgoing-args
LD := ld
LDFLAGS := -shared -Bsymbolic -Lgnu-efi/x86_64/lib -Lgnu-efi/x86_64/gnuefi -Tgnu-efi/gnuefi/elf_x86_64_efi.lds gnu-efi/x86_64/gnuefi/crt0-efi-x86_64.o

.PHONY: all gnu-efi clean

all: gnu-efi
	@mkdir -p build
	@$(CC) $(CFLAGS) -c src/main.c -o build/main.o
	@$(LD) $(LDFLAGS) build/main.o -o build/main.so -lgnuefi -lefi
	@objcopy -j .text -j .sdata -j .data -j .rodata -j .dynamic -j .dynsym  -j .rel -j .rela -j .rel.* -j .rela.* -j .reloc --target efi-app-x86_64 --subsystem=10 build/main.so build/eos.efi

gnu-efi:
	@echo "==> Building gnu-efi"
	@$(MAKE) --no-print-directory -s -C gnu-efi > /dev/null 2>&1

clean:
	@$(MAKE) --no-print-directory -s -C gnu-efi clean > /dev/null 2>&1
	@rm -rf build