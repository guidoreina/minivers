#include "minivers.h"
#include <ntstrsafe.h>
#include "copy_file.h"

#define DEFERRED_IO            TRUE
#define IMMUTABLE_BACKUP_FILES TRUE
#define LOG_IMAGE_FILENAME     TRUE

static DRIVER_DATA driver_data;

#if LOG_IMAGE_FILENAME
  #define EXE_MAX_LEN            (4 * 1024)

  /* Prototype of the function 'ZwQueryInformationProcess()'. */
  typedef NTSTATUS (*QueryInformationProcess)(HANDLE,
                                              PROCESSINFOCLASS,
                                              PVOID,
                                              ULONG,
                                              PULONG);

  /* Pointer to the function 'ZwQueryInformationProcess()'. */
  static QueryInformationProcess fnQueryInformationProcess = NULL;
#endif /* LOG_IMAGE_FILENAME */


/******************************************************************************
 ******************************************************************************
 **                                                                          **
 ** File extensions to be taken into account.                                **
 **                                                                          **
 ******************************************************************************
 ******************************************************************************/
DECLARE_CONST_UNICODE_STRING(DOC, L"doc");
DECLARE_CONST_UNICODE_STRING(DOCX, L"docx");
DECLARE_CONST_UNICODE_STRING(XLS, L"xls");
DECLARE_CONST_UNICODE_STRING(XLSX, L"xlsx");
DECLARE_CONST_UNICODE_STRING(TXT, L"txt");

static const UNICODE_STRING* extensions[] = {&DOC, &DOCX, &XLS, &XLSX, &TXT};


/******************************************************************************
 ******************************************************************************
 **                                                                          **
 ** minivers's file extension.                                               **
 **                                                                          **
 ******************************************************************************
 ******************************************************************************/
DECLARE_CONST_UNICODE_STRING(MINIVERS_EXT, L"minivers");


DRIVER_INITIALIZE DriverEntry;
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
                     _In_ PUNICODE_STRING RegistryPath);

static NTSTATUS process_irp(PFLT_CALLBACK_DATA Data,
                            PCFLT_RELATED_OBJECTS FltObjects,
                            PVOID* CompletionContext,
                            BOOLEAN deferred_io);

static BOOLEAN get_file_name_information(PFLT_CALLBACK_DATA data,
                                         PFLT_FILE_NAME_INFORMATION* name_info);

static BOOLEAN find_extension(const UNICODE_STRING* ext);
static BOOLEAN duplicate_file(PFLT_CALLBACK_DATA CallbackData,
                              PFLT_INSTANCE instance);

static void deferred_io_workitem(PFLT_DEFERRED_IO_WORKITEM FltWorkItem,
                                 PFLT_CALLBACK_DATA CallbackData,
                                 PVOID Context);

#if LOG_IMAGE_FILENAME
  static BOOLEAN get_process_image_filename(PEPROCESS process,
                                            void* buf,
                                            size_t len);
#endif /* LOG_IMAGE_FILENAME */

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(INIT, DriverEntry)
  #pragma alloc_text(PAGE, InstanceSetup)
  #pragma alloc_text(PAGE, FilterUnload)
  #pragma alloc_text(PAGE, InstanceQueryTeardown)
#endif /* ALLOC_PRAGMA */


NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
                     _In_ PUNICODE_STRING RegistryPath)
{
#if LOG_IMAGE_FILENAME
  UNICODE_STRING name;
#endif

  NTSTATUS status;

  UNREFERENCED_PARAMETER(RegistryPath);

#if LOG_IMAGE_FILENAME
  RtlInitUnicodeString(&name, L"ZwQueryInformationProcess");

  /* Get a pointer to the function 'ZwQueryInformationProcess()'. */
  fnQueryInformationProcess = (QueryInformationProcess)
                              (ULONG_PTR) MmGetSystemRoutineAddress(&name);
#endif /* LOG_IMAGE_FILENAME */

  /* Register with the filter manager. */
  status = FltRegisterFilter(DriverObject,
                             &filter_registration,
                             &driver_data.filter);

  if (NT_SUCCESS(status)) {
    /* Start filtering. */
    status = FltStartFiltering(driver_data.filter);

    if (!NT_SUCCESS(status)) {
      FltUnregisterFilter(driver_data.filter);
    }
  }

  return status;
}

NTSTATUS InstanceSetup(_In_ PCFLT_RELATED_OBJECTS FltObjects,
                       _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
                       _In_ DEVICE_TYPE VolumeDeviceType,
                       _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType)
{
  UNREFERENCED_PARAMETER(FltObjects);
  UNREFERENCED_PARAMETER(Flags);
  UNREFERENCED_PARAMETER(VolumeFilesystemType);

  PAGED_CODE();

  return (VolumeDeviceType != FILE_DEVICE_CD_ROM_FILE_SYSTEM) ?
           STATUS_SUCCESS :
           STATUS_FLT_DO_NOT_ATTACH;
}

NTSTATUS FilterUnload(_In_ FLT_FILTER_UNLOAD_FLAGS Flags)
{
  UNREFERENCED_PARAMETER(Flags);

  PAGED_CODE();

  FltUnregisterFilter(driver_data.filter);

  return STATUS_SUCCESS;
}

NTSTATUS InstanceQueryTeardown(_In_ PCFLT_RELATED_OBJECTS FltObjects,
                               _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags)
{
  UNREFERENCED_PARAMETER(FltObjects);
  UNREFERENCED_PARAMETER(Flags);

  PAGED_CODE();

  return STATUS_SUCCESS;
}

FLT_PREOP_CALLBACK_STATUS
PreOperationCallback(
  _Inout_ PFLT_CALLBACK_DATA Data,
  _In_ PCFLT_RELATED_OBJECTS FltObjects,
  _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
)
{
  /* IRP-based I/O operation? */
  if (FLT_IS_IRP_OPERATION(Data)) {
    /* Open file? */
    if (Data->Iopb->MajorFunction == IRP_MJ_CREATE) {
      /* Open file for writing/appending? */
      if (Data->Iopb->Parameters.Create.SecurityContext->DesiredAccess &
          (FILE_WRITE_DATA | FILE_APPEND_DATA)) {
        return process_irp(Data, FltObjects, CompletionContext, DEFERRED_IO);
      }
    } else if (Data->Iopb->MajorFunction == IRP_MJ_SET_INFORMATION) {
      switch (Data->Iopb->Parameters.SetFileInformation.FileInformationClass) {
        case FileDispositionInformation:
          if (((FILE_DISPOSITION_INFORMATION*)
               Data->Iopb->Parameters.SetFileInformation.InfoBuffer
              )->DeleteFile) {
            return process_irp(Data, FltObjects, CompletionContext, FALSE);
          }

          break;
        case FileEndOfFileInformation:
        case FileRenameInformation:
          return process_irp(Data, FltObjects, CompletionContext, FALSE);
      }
    }
  }

  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

NTSTATUS process_irp(PFLT_CALLBACK_DATA Data,
                     PCFLT_RELATED_OBJECTS FltObjects,
                     PVOID* CompletionContext,
                     BOOLEAN deferred_io)
{
  PFLT_FILE_NAME_INFORMATION name_info;
  PFLT_DEFERRED_IO_WORKITEM work;

#if LOG_IMAGE_FILENAME
  PEPROCESS process;
  ULONG pid;
  char buf[sizeof(UNICODE_STRING) + (sizeof(WCHAR) * EXE_MAX_LEN)];
  PUNICODE_STRING exe;
#endif // LOG_IMAGE_FILENAME

  /* Get name information. */
  if (get_file_name_information(Data, &name_info)) {
    if (find_extension(&name_info->Extension)) {
#if LOG_IMAGE_FILENAME
      if (fnQueryInformationProcess) {
        exe = (PUNICODE_STRING) buf;
        exe->Buffer = (WCHAR*) (buf + sizeof(UNICODE_STRING));
        exe->Length = 0;
        exe->MaximumLength = EXE_MAX_LEN;

        /* Get process image filename. */
        if (((pid = FltGetRequestorProcessId(Data)) != 0) &&
            ((process = FltGetRequestorProcess(Data)) != NULL) &&
            (get_process_image_filename(process, buf, sizeof(buf)))) {
          DbgPrint("[PID: %u, executable: '%wZ'] Filename: '%wZ', "
                   "extension: '%wZ'.",
                   pid,
                   exe,
                   &name_info->Name,
                   &name_info->Extension);
        } else {
          DbgPrint("Filename: '%wZ', extension: '%wZ'.",
                   &name_info->Name,
                   &name_info->Extension);
        }
      } else {
        DbgPrint("Filename: '%wZ', extension: '%wZ'.",
                 &name_info->Name,
                 &name_info->Extension);
      }
#else
      DbgPrint("Filename: '%wZ', extension: '%wZ'.",
               &name_info->Name,
               &name_info->Extension);
#endif

      if (deferred_io) {
        if ((work = FltAllocateDeferredIoWorkItem()) != NULL) {
          if (NT_SUCCESS(FltQueueDeferredIoWorkItem(work,
                                                    Data,
                                                    deferred_io_workitem,
                                                    DelayedWorkQueue,
                                                    FltObjects->Instance))) {
            FltReleaseFileNameInformation(name_info);

            *CompletionContext = NULL;

            return FLT_PREOP_PENDING;
          } else {
            FltFreeDeferredIoWorkItem(work);
          }
        }
      }

      duplicate_file(Data, FltObjects->Instance);
#if IMMUTABLE_BACKUP_FILES
    } else if (RtlEqualUnicodeString(&name_info->Extension,
                                     &MINIVERS_EXT,
                                     TRUE)) {
      DbgPrint("Filename: '%wZ', extension: '%wZ'.",
               &name_info->Name,
               &name_info->Extension);

      FltReleaseFileNameInformation(name_info);

      Data->IoStatus.Status = STATUS_ACCESS_DENIED;
      return FLT_PREOP_COMPLETE;
#endif /* IMMUTABLE_BACKUP_FILES */
    }

    FltReleaseFileNameInformation(name_info);
  }

  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

BOOLEAN get_file_name_information(PFLT_CALLBACK_DATA data,
                                  PFLT_FILE_NAME_INFORMATION* name_info)
{
  /* Get name information. */
  if (NT_SUCCESS(FltGetFileNameInformation(
                   data,
                   FLT_FILE_NAME_NORMALIZED |
                   FLT_FILE_NAME_QUERY_ALWAYS_ALLOW_CACHE_LOOKUP,
                   name_info
                 ))) {
    /* Parse file name information. */
    if (NT_SUCCESS(FltParseFileNameInformation(*name_info))) {
      return TRUE;
    }

    FltReleaseFileNameInformation(*name_info);
#if OSVER(NTDDI_VERSION) > NTDDI_WIN2K
  } else {
    /*
     * We couldn't get the "normalized" name, try to get the "opened"
     * name.
     */
    if (NT_SUCCESS(FltGetFileNameInformation(data,
                     FLT_FILE_NAME_OPENED |
                     FLT_FILE_NAME_QUERY_ALWAYS_ALLOW_CACHE_LOOKUP,
                     name_info
                   ))) {
      if (NT_SUCCESS(FltParseFileNameInformation(*name_info))) {
        return TRUE;
      }

      FltReleaseFileNameInformation(*name_info);
    }
#endif /* OSVER(NTDDI_VERSION) > NTDDI_WIN2K */
  }

  return FALSE;
}

BOOLEAN find_extension(const UNICODE_STRING* ext)
{
  size_t i;

  for (i = 0; i < ARRAYSIZE(extensions); i++) {
    if (RtlEqualUnicodeString(ext, extensions[i], TRUE)) {
      return TRUE;
    }
  }

  return FALSE;
}

BOOLEAN duplicate_file(PFLT_CALLBACK_DATA CallbackData, PFLT_INSTANCE instance)
{
  PFLT_FILE_NAME_INFORMATION name_info;
  UNICODE_STRING dest;
  LARGE_INTEGER system_time, local_time;
  TIME_FIELDS time;

  /* Get name information. */
  if (get_file_name_information(CallbackData, &name_info)) {
    /* Compute size in bytes.
     * Suffix's format: .YYYYMMDD_hhmmss_mmm.<MINIVERS_EXT>
     */
    dest.MaximumLength = name_info->Name.Length +
                         42 +
                         MINIVERS_EXT.Length +
                         sizeof(WCHAR);

    /* Allocate memory for the destination file name. */
    if ((dest.Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                             dest.MaximumLength,
                                             TAG)) != NULL) {
      dest.Length = 0;

      /* Get system time. */
      KeQuerySystemTime(&system_time);

      /* Convert system time to local time. */
      ExSystemTimeToLocalTime(&system_time, &local_time);

      RtlTimeToTimeFields(&local_time, &time);

      /* Compose name of the new file. */
      if (NT_SUCCESS(RtlUnicodeStringPrintf(
                       &dest,
                       L"%wZ.%04u%02u%02u_%02u%02u%02u_%03u.%wZ",
                       &name_info->Name,
                       time.Year,
                       time.Month,
                       time.Day,
                       time.Hour,
                       time.Minute,
                       time.Second,
                       time.Milliseconds,
                       &MINIVERS_EXT
                     ))) {
        /* Copy file. */
        if (copy_file(driver_data.filter, instance, &dest, &name_info->Name)) {
          ExFreePoolWithTag(dest.Buffer, TAG);
          FltReleaseFileNameInformation(name_info);

          return TRUE;
        }
      }

      ExFreePoolWithTag(dest.Buffer, TAG);
    }

    FltReleaseFileNameInformation(name_info);
  }

  return FALSE;
}

void deferred_io_workitem(PFLT_DEFERRED_IO_WORKITEM FltWorkItem,
                          PFLT_CALLBACK_DATA CallbackData,
                          PVOID Context)
{
  duplicate_file(CallbackData, Context);

  FltFreeDeferredIoWorkItem(FltWorkItem);

  FltCompletePendedPreOperation(CallbackData,
                                FLT_PREOP_SUCCESS_NO_CALLBACK,
                                NULL);
}

#if LOG_IMAGE_FILENAME
  BOOLEAN get_process_image_filename(PEPROCESS process, void* buf, size_t len)
  {
    HANDLE hProcess;
    NTSTATUS status;

    PAGED_CODE();

    /* Get process handle. */
    if (NT_SUCCESS(ObOpenObjectByPointer(process,
                                         OBJ_KERNEL_HANDLE,
                                         NULL,
                                         0,
                                         0,
                                         KernelMode,
                                         &hProcess))) {
      /* Get process image filename. */
      status = fnQueryInformationProcess(hProcess,
                                         ProcessImageFileName,
                                         buf,
                                         len,
                                         NULL);

      ZwClose(hProcess);

      return NT_SUCCESS(status) ? TRUE : FALSE;
    }

    return FALSE;
  }
#endif /* LOG_IMAGE_FILENAME */
