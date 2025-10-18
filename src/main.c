#include <efi.h>
#include <efilib.h>

EFI_STATUS
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    Print(L"Eos: Awakening Atlas...\n");

    EFI_LOADED_IMAGE *LoadedImage;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    EFI_FILE_HANDLE RootFS, KernelFile;

    // Get filesystem from the device we booted from
    uefi_call_wrapper(BS->HandleProtocol, 3, ImageHandle,
                      &LoadedImageProtocol, (void**)&LoadedImage);
    uefi_call_wrapper(BS->HandleProtocol, 3, LoadedImage->DeviceHandle,
                      &FileSystemProtocol, (void**)&FileSystem);
    FileSystem->OpenVolume(FileSystem, &RootFS);

    // Open kernel
    if (RootFS->Open(RootFS, &KernelFile, L"\\boot\\bzImage",
                     EFI_FILE_MODE_READ, 0) != EFI_SUCCESS) {
        Print(L"Failed to open kernel\n");
        return EFI_LOAD_ERROR;
    }

    // Read kernel into memory (simplified: assumes small size)
    UINTN KernelSize = 16*1024*1024; // 16 MB buffer
    VOID *KernelBuffer;
    BS->AllocatePool(EfiLoaderData, KernelSize, &KernelBuffer);
    KernelFile->Read(KernelFile, &KernelSize, KernelBuffer);

    Print(L"Kernel loaded (%d bytes). Jumping to Atlas...\n", KernelSize);

    // Normally: set up Linux boot params, memory map, initrd, etc.
    // For a custom kernel, you can just call its entry point:
    void (*KernelEntry)(void) = (void(*)(void))KernelBuffer;

    // Exit boot services before jumping
    UINTN MapKey, MapSize = 0, DescSize;
    UINT32 DescVersion;
    BS->GetMemoryMap(&MapSize, NULL, &MapKey, &DescSize, &DescVersion);
    EFI_MEMORY_DESCRIPTOR *MemMap;
    BS->AllocatePool(EfiLoaderData, MapSize, (void**)&MemMap);
    BS->GetMemoryMap(&MapSize, MemMap, &MapKey, &DescSize, &DescVersion);
    BS->ExitBootServices(ImageHandle, MapKey);

    KernelEntry(); // hand off to Atlas

    return EFI_SUCCESS;
}
