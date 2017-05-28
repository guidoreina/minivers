#ifndef COPY_FILE_H
#define COPY_FILE_H

#include <wdm.h>
#include <fltKernel.h>

BOOLEAN copy_file(PFLT_FILTER filter,
                  PFLT_INSTANCE instance,
                  UNICODE_STRING* dest,
                  UNICODE_STRING* src);

#endif /* COPY_FILE_H */
