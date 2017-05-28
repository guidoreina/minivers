#include <ntifs.h>
#include "copy_file.h"
#include "minivers.h"

#define BUFFER_SIZE (4 * 1024)

static BOOLEAN write(HANDLE hFile, void* buf, ULONG len);
static BOOLEAN delete_file(PFLT_INSTANCE instance, PFILE_OBJECT file);

/* Disable warning:
 * Conditional expression is constant:
 * do {
 *   ...
 * } while (1);
 */
#pragma warning(disable:4127)

BOOLEAN copy_file(PFLT_FILTER filter,
                  PFLT_INSTANCE instance,
                  UNICODE_STRING* dest,
                  UNICODE_STRING* src)
{
  HANDLE hInput, hOutput;
  PFILE_OBJECT out;
  OBJECT_ATTRIBUTES attr;
  IO_STATUS_BLOCK io_status_block;
  PVOID buf;
  ULONGLONG filesize;
  NTSTATUS status;

  InitializeObjectAttributes(&attr,
                             src,
                             OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                             NULL,
                             NULL);

  /* Open input file for reading. */
  status = FltCreateFile(filter,                       /* Filter            */
                         instance,                     /* Instance          */
                         &hInput,                      /* FileHandle        */
                         GENERIC_READ,                 /* DesiredAccess     */
                         &attr,                        /* ObjectAttributes  */
                         &io_status_block,             /* IoStatusBlock     */
                         NULL,                         /* AllocationSize    */
                         FILE_ATTRIBUTE_NORMAL,        /* FileAttributes    */
                         FILE_SHARE_READ |             /* ShareAccess       */
                         FILE_SHARE_WRITE |
                         FILE_SHARE_DELETE,
                         FILE_OPEN,                    /* CreateDisposition */
                         FILE_SYNCHRONOUS_IO_NONALERT, /* CreateOptions     */
                         NULL,                         /* EaBuffer          */
                         0,                            /* EaLength          */
                         IO_FORCE_ACCESS_CHECK);       /* Flags             */

  /* If the input file could be opened... */
  if (NT_SUCCESS(status)) {
    InitializeObjectAttributes(&attr,
                               dest,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    /* Open output file for writing. */
    status = FltCreateFileEx(filter,                       /* Filter            */
                             instance,                     /* Instance          */
                             &hOutput,                     /* FileHandle        */
                             &out,                         /* FileObject        */
                             GENERIC_WRITE,                /* DesiredAccess     */
                             &attr,                        /* ObjectAttributes  */
                             &io_status_block,             /* IoStatusBlock     */
                             NULL,                         /* AllocationSize    */
                             FILE_ATTRIBUTE_NORMAL,        /* FileAttributes    */
                             0,                            /* ShareAccess       */
                             FILE_OVERWRITE_IF,            /* CreateDisposition */
                             FILE_SYNCHRONOUS_IO_NONALERT, /* CreateOptions     */
                             NULL,                         /* EaBuffer          */
                             0,                            /* EaLength          */
                             IO_FORCE_ACCESS_CHECK);       /* Flags             */

    /* If the output file could be opened... */
    if (NT_SUCCESS(status)) {
      /* Allocate memory. */
      if ((buf = ExAllocatePoolWithTag(NonPagedPool,
                                       BUFFER_SIZE,
                                       TAG)) != NULL) {
        filesize = 0;

        do {
          /* Read from the input file. */
          status = ZwReadFile(hInput,           /* FileHandle    */
                              NULL,             /* Event         */
                              NULL,             /* ApcRoutine    */
                              NULL,             /* ApcContext    */
                              &io_status_block, /* IoStatusBlock */
                              buf,              /* Buffer        */
                              BUFFER_SIZE,      /* Length        */
                              NULL,             /* ByteOffset    */
                              NULL);            /* Key           */

          if (NT_SUCCESS(status)) {
            if (write(hOutput, buf, io_status_block.Information)) {
              filesize += io_status_block.Information;
            } else {
              /* Free memory. */
              ExFreePoolWithTag(buf, TAG);

              /* Close input file. */
              FltClose(hInput);

              /* Delete output file. */
              delete_file(instance, out);

              /* Close output file. */
              FltClose(hOutput);
              ObDereferenceObject(out);

              return FALSE;
            }
          } else if (status == STATUS_END_OF_FILE) {
            /* Free memory. */
            ExFreePoolWithTag(buf, TAG);

            /* Close input file. */
            FltClose(hInput);

            /* If the file is not empty... */
            if (filesize > 0) {
              DbgPrint("Copied file '%wZ' -> '%wZ', filesize: %lu.",
                       src,
                       dest,
                       filesize);
            } else {
              /* Delete output file. */
              delete_file(instance, out);
            }

            /* Close output file. */
            FltClose(hOutput);
            ObDereferenceObject(out);

            return TRUE;
          } else {
            /* Free memory. */
            ExFreePoolWithTag(buf, TAG);

            /* Close input file. */
            FltClose(hInput);

            /* Delete output file. */
            delete_file(instance, out);

            /* Close output file. */
            FltClose(hOutput);
            ObDereferenceObject(out);

            return FALSE;
          }
        } while (1);
      } else {
        /* Delete output file. */
        delete_file(instance, out);

        /* Close output file. */
        FltClose(hOutput);
        ObDereferenceObject(out);
      }
    }

    /* Close input file. */
    FltClose(hInput);
  }

  return FALSE;
}

BOOLEAN write(HANDLE hFile, void* buf, ULONG len)
{
  IO_STATUS_BLOCK io_status_block;
  UCHAR* b;

  b = (UCHAR*) buf;

  while (len > 0) {
    if (!NT_SUCCESS(ZwWriteFile(hFile,            /* FileHandle    */
                                NULL,             /* Event         */
                                NULL,             /* ApcRoutine    */
                                NULL,             /* ApcContext    */
                                &io_status_block, /* IoStatusBlock */
                                b,                /* Buffer        */
                                len,              /* Length        */
                                NULL,             /* ByteOffset    */
                                NULL))) {         /* Key           */
      return FALSE;
    }

    b += io_status_block.Information;
    len -= io_status_block.Information;
  }

  return TRUE;
}

BOOLEAN delete_file(PFLT_INSTANCE instance, PFILE_OBJECT file)
{
  FILE_DISPOSITION_INFORMATION info;

  info.DeleteFile = TRUE;

  return NT_SUCCESS(FltSetInformationFile(instance,
                                          file,
                                          &info,
                                          sizeof(FILE_DISPOSITION_INFORMATION),
                                          FileDispositionInformation));
}
