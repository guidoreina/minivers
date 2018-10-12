Minivers File System Minifilter Driver
======================================

Minivers is a Windows file system minifilter driver which monitors changes in files with certain extensions. If a monitored file is about to be changed, deleted or renamed, a backup copy of the file is performed before the operation begins.

The file name of the backup files has the format:
```
<original-filename>..YYYYMMDD_hhmmss_mmm.minivers
```


The following file extensions are monitored:
* doc
* docx
* xls
* xlsx
* txt

You can add more file extensions by editing the file minivers.c:
```
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
```


The backup files have the extension `minivers`:
```
DECLARE_CONST_UNICODE_STRING(MINIVERS_EXT, L"minivers");
```


Files with this file extension cannot be deleted, if you want to change this, change the following `define`:
```
#define IMMUTABLE_BACKUP_FILES TRUE
```


Minivers can be used to protect against ransomware, as they search files with certain extensions (not tested though).

Minivers logs the name of the executable which produced the change, to avoid this change the following `define`:
```
#define LOG_IMAGE_FILENAME     TRUE
```
