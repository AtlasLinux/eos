/*
 * eos.c — robust UEFI loader (GNU-EFI)
 *
 * Behavior:
 *  - enumerate all SimpleFileSystem handles (all accessible filesystems)
 *  - try to read "\boot\bzImage" (and fallback to "\EFI\Atlas\vmlinuz.efi")
 *  - load the first one found into memory and LoadImage/StartImage it
 *
 * Build with GNU-EFI.
 */

#include <efi.h>
#include <efilib.h>

#define TIMEOUT_SECONDS 5

static CONST CHAR16 *PRIMARY_KERNEL_PATH = L"\\boot\\bzImage";
static CONST CHAR16 *FALLBACK_KERNEL_PATH = L"\\EFI\\Atlas\\vmlinuz.efi";
/* adjust kernel command line */
static const CHAR8 *kernel_cmdline = "root=/dev/vda rw console=tty1 initrd=/boot/initramfs.cpio.gz";

static UINTN ascii_len(const CHAR8 *s) {
    UINTN n = 0;
    if (!s) return 0;
    while (*s++) n++;
    return n;
}

/* Read a file from a given opened root into AllocatePool buf */
STATIC
EFI_STATUS
read_file_from_root(EFI_FILE_PROTOCOL *root, CONST CHAR16 *path, VOID **out_buf, UINTN *out_size)
{
    EFI_STATUS Status;
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_FILE_INFO *info = NULL;
    UINTN info_size = SIZE_OF_EFI_FILE_INFO + 512;
    VOID *buf = NULL;
    UINTN size;

    Status = root->Open(root, &file, (CHAR16*)path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) return Status;

    info = AllocatePool(info_size);
    if (!info) { file->Close(file); return EFI_OUT_OF_RESOURCES; }

    Status = file->GetInfo(file, &gEfiFileInfoGuid, &info_size, info);
    if (EFI_ERROR(Status)) {
        FreePool(info);
        file->Close(file);
        return Status;
    }

    size = (UINTN)info->FileSize;
    FreePool(info);

    buf = AllocatePool(size);
    if (!buf) { file->Close(file); return EFI_OUT_OF_RESOURCES; }

    Status = file->Read(file, &size, buf);
    if (EFI_ERROR(Status)) {
        FreePool(buf);
        file->Close(file);
        return Status;
    }

    file->Close(file);
    *out_buf = buf;
    *out_size = size;
    return EFI_SUCCESS;
}

/* Try to find and read 'path' from any SimpleFileSystem handle.
 * On success, returns EFI_SUCCESS and fills *out_buf,*out_size and sets *out_handle to the handle that contained it.
 * Caller must FreePool(*out_buf) when done.
 */
STATIC
EFI_STATUS
locate_file_on_any_fs(CONST CHAR16 *path, VOID **out_buf, UINTN *out_size, EFI_HANDLE *out_handle)
{
    EFI_STATUS Status;
    EFI_HANDLE *handles = NULL;
    UINTN handle_count = 0;
    UINTN i;

    Status = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
                               ByProtocol, &gEfiSimpleFileSystemProtocolGuid,
                               NULL, &handle_count, &handles);
    if (EFI_ERROR(Status) || handle_count == 0) {
        return EFI_NOT_FOUND;
    }

    for (i = 0; i < handle_count; ++i) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
        EFI_FILE_PROTOCOL *root = NULL;

        Status = uefi_call_wrapper(BS->HandleProtocol, 3, handles[i],
                                   &gEfiSimpleFileSystemProtocolGuid, (VOID**)&fs);
        if (EFI_ERROR(Status) || fs == NULL) continue;

        Status = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
        if (EFI_ERROR(Status) || root == NULL) continue;

        /* Try path on this root */
        Status = read_file_from_root(root, path, out_buf, out_size);
        if (!EFI_ERROR(Status)) {
            if (out_handle) *out_handle = handles[i];
            FreePool(handles);
            return EFI_SUCCESS;
        }
        /* else try next handle */
    }

    FreePool(handles);
    return EFI_NOT_FOUND;
}

EFI_STATUS
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS Status;
    InitializeLib(ImageHandle, SystemTable);

    Print(L"\r\n=== Eos — Atlas loader ===\r\n");
    Print(L"Looking for kernel (primary: %s, fallback: %s)\r\n", PRIMARY_KERNEL_PATH, FALLBACK_KERNEL_PATH);
    Print(L"Timeout: %d seconds (press any key to boot immediately)\r\n", TIMEOUT_SECONDS);

    /* wait for key or timeout */
    {
        EFI_EVENT events[2];
        UINTN idx;
        events[0] = ST->ConIn->WaitForKey;
        Status = BS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &events[1]);
        if (EFI_ERROR(Status)) events[1] = NULL;
        else BS->SetTimer(events[1], TimerRelative, TIMEOUT_SECONDS * 10000000ULL);
        BS->WaitForEvent((events[1] ? 2U : 1U), events, &idx);
        if (events[1]) BS->CloseEvent(events[1]);
    }

    VOID *kernel_buf = NULL;
    UINTN kernel_size = 0;
    EFI_HANDLE kernel_volume_handle = NULL;

    /* Try primary path first (likely on real /boot), then fallback to EFI/Atlas */
    Status = locate_file_on_any_fs(PRIMARY_KERNEL_PATH, &kernel_buf, &kernel_size, &kernel_volume_handle);
    if (EFI_ERROR(Status)) {
        Print(L"Eos: primary path not found on any FS, trying fallback.\r\n");
        Status = locate_file_on_any_fs(FALLBACK_KERNEL_PATH, &kernel_buf, &kernel_size, &kernel_volume_handle);
    }

    if (EFI_ERROR(Status)) {
        Print(L"Eos: kernel not found on any filesystem: %r\n", Status);
        return Status;
    }

    Print(L"Eos: loaded kernel (%u bytes) from handle %p\r\n", (UINT32)kernel_size, kernel_volume_handle);

    /* LoadImage from memory */
    EFI_HANDLE kernel_image = NULL;
    Status = uefi_call_wrapper(BS->LoadImage, 6, FALSE, ImageHandle, NULL, kernel_buf, kernel_size, &kernel_image);
    if (EFI_ERROR(Status)) {
        Print(L"Eos: LoadImage failed: %r\n", Status);
        FreePool(kernel_buf);
        return Status;
    }

    /* Set kernel command line as LoadOptions */
    EFI_LOADED_IMAGE_PROTOCOL *kli = NULL;
    Status = uefi_call_wrapper(BS->HandleProtocol, 3, kernel_image,
                               &gEfiLoadedImageProtocolGuid, (VOID**)&kli);
    if (EFI_ERROR(Status)) {
        Print(L"Eos: cannot get LoadedImage for child: %r\n", Status);
        FreePool(kernel_buf);
        return Status;
    }

    UINTN cmdlen = ascii_len(kernel_cmdline) + 1;
    VOID *cmdmem = AllocatePool(cmdlen);
    if (!cmdmem) {
        Print(L"Eos: out of memory for cmdline\n");
        FreePool(kernel_buf);
        return EFI_OUT_OF_RESOURCES;
    }
    CopyMem(cmdmem, (VOID*)kernel_cmdline, cmdlen);
    kli->LoadOptions = cmdmem;
    kli->LoadOptionsSize = (UINT32)cmdlen;

    Print(L"Eos: starting kernel with cmdline: %a\n", kernel_cmdline);

    /* Start kernel image */
    UINTN exit_data_size = 0;
    CHAR16 *exit_data = NULL;
    Status = uefi_call_wrapper(BS->StartImage, 3, kernel_image, &exit_data_size, &exit_data);

    Print(L"Eos: StartImage returned: %r\n", Status);
    if (exit_data) {
        Print(L"Eos: child returned message: %s\n", exit_data);
        FreePool(exit_data);
    }

    if (kernel_buf) FreePool(kernel_buf);
    if (cmdmem) FreePool(cmdmem);

    return Status;
}
