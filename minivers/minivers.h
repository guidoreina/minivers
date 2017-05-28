#ifndef MINIVERS_H
#define MINIVERS_H

#include <fltKernel.h>
#include <suppress.h>

#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")

#define TAG 'reVM'


/******************************************************************************
 ******************************************************************************
 **                                                                          **
 ** Type definitions.                                                        **
 **                                                                          **
 ******************************************************************************
 ******************************************************************************/
typedef struct {
  /* The filter that results from a call to FltRegisterFilter. */
  PFLT_FILTER filter;
} DRIVER_DATA;


/******************************************************************************
 ******************************************************************************
 **                                                                          **
 ** Global variables.                                                        **
 **                                                                          **
 ******************************************************************************
 ******************************************************************************/
extern const FLT_REGISTRATION filter_registration;


/******************************************************************************
 ******************************************************************************
 **                                                                          **
 ** Function prototypes.                                                     **
 **                                                                          **
 ******************************************************************************
 ******************************************************************************/
FLT_PREOP_CALLBACK_STATUS
PreOperationCallback(
  _Inout_ PFLT_CALLBACK_DATA Data,
  _In_ PCFLT_RELATED_OBJECTS FltObjects,
  _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
);

NTSTATUS InstanceSetup(_In_ PCFLT_RELATED_OBJECTS FltObjects,
                       _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
                       _In_ DEVICE_TYPE VolumeDeviceType,
                       _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType);

NTSTATUS FilterUnload(_In_ FLT_FILTER_UNLOAD_FLAGS Flags);

NTSTATUS InstanceQueryTeardown(_In_ PCFLT_RELATED_OBJECTS FltObjects,
                               _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags);

#endif /* MINIVERS_H */
