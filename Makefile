TARGET = Eos
ARCH = x86_64
EFI_DIR = /usr/include/efi
EFI_INC = -I$(EFI_DIR) -I$(EFI_DIR)/$(ARCH) -I$(EFI_DIR)/protocol
CFLAGS = -Wall -fpic -fshort-wchar -mno-red-zone -m64 $(EFI_INC)
LDFLAGS = -nostdlib -znocombreloc -T elf_$(ARCH)_efi.lds
OBJS = eos.o

.PHONY: all clean run

all: $(TARGET).efi

%.efi: %.so
    objcopy -j .text -j .sdata -j .data -j .dynamic \
            -j .dynsym -j .rel -j .rela -j .reloc \
            --target=efi-app-$(ARCH) $^ $@

%.so: $(OBJS)
    ld $(LDFLAGS) -shared -Bsymbolic -L/usr/lib -lefi -lgnuefi -o $@ $^

%.o: %.c
    $(CC) $(CFLAGS) -c $< -o $@

clean:
    rm -f *.o *.so *.efi
