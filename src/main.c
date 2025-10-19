#include <efi.h>
#include <efilib.h>

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_STATUS                   Status;
    EFI_LOADED_IMAGE            *SelfImage;
    EFI_DEVICE_PATH             *KernelPath;
    EFI_HANDLE                   KernelImage = NULL;
    EFI_LOADED_IMAGE            *KernelLoadedImage;
    CHAR16                      *CmdLine = L"root=/dev/vda rw console=tty1 initrd=\\boot\\initramfs.cpio.gz";

    InitializeLib(ImageHandle, SystemTable);
    Print(L"\n== Eos starting ==\n");

    Status = uefi_call_wrapper(
        SystemTable->BootServices->HandleProtocol,
        3,
        ImageHandle,
        &LoadedImageProtocol,
        (VOID**)&SelfImage
    );
    if (EFI_ERROR(Status)) {
        Print(L"ERROR: couldn’t get LoadedImageProtocol for self: %r\n", Status);
        return Status;
    }
    Print(L"Loaded from DeviceHandle=%p\n", SelfImage->DeviceHandle);

    KernelPath = FileDevicePath(SelfImage->DeviceHandle, L"\\boot\\bzImage");
    if (KernelPath == NULL) {
        Print(L"ERROR: FileDevicePath returned NULL\n");
        return EFI_NOT_FOUND;
    }
    Print(L"Kernel path DP @ %p\n", KernelPath);

    Status = uefi_call_wrapper(
        SystemTable->BootServices->LoadImage,
        6,
        FALSE,
        ImageHandle,
        KernelPath,
        NULL, 0,
        &KernelImage
    );
    if (EFI_ERROR(Status)) {
        Print(L"ERROR: LoadImage failed: %r\n", Status);
        return Status;
    }
    Print(L"KernelImage handle = %p\n", KernelImage);

    Status = uefi_call_wrapper(
        SystemTable->BootServices->HandleProtocol,
        3,
        KernelImage,
        &LoadedImageProtocol,
        (VOID**)&KernelLoadedImage
    );
    if (EFI_ERROR(Status)) {
        Print(L"ERROR: couldn’t get LoadedImageProtocol for kernel: %r\n", Status);
        return Status;
    }
    KernelLoadedImage->LoadOptions     = CmdLine;
    KernelLoadedImage->LoadOptionsSize = (UINT32)((StrLen(CmdLine) + 1) * sizeof(CHAR16));
    Print(L"Set kernel cmdline: %s\n", CmdLine);

    Print(L"Starting kernel…\n\n");
    Status = uefi_call_wrapper(
        SystemTable->BootServices->StartImage,
        3,
        KernelImage,
        NULL,
        NULL
    );
    Print(L"\nERROR: kernel returned control: %r\n", Status);

    return Status;
}
