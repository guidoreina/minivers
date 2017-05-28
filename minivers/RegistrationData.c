#include "minivers.h"

/* Place all the following DATA and CONSTANT DATA in the INIT segment. */
#ifdef ALLOC_DATA_PRAGMA
  #pragma data_seg("INIT")
  #pragma const_seg("INIT")
#endif /* ALLOC_DATA_PRAGMA */

CONST FLT_OPERATION_REGISTRATION callbacks[] = {
  {IRP_MJ_CREATE,          0, PreOperationCallback, NULL},
  {IRP_MJ_SET_INFORMATION, 0, PreOperationCallback, NULL},
  {IRP_MJ_OPERATION_END                                 }
};

CONST FLT_REGISTRATION filter_registration = {
  sizeof(FLT_REGISTRATION),             /* Size. */
  FLT_REGISTRATION_VERSION,             /* Version. */
  0,                                    /* Flags. */
  NULL,                                 /* ContextRegistration. */
  callbacks,                            /* OperationRegistration. */
  FilterUnload,                         /* FilterUnloadCallback. */
  InstanceSetup,                        /* InstanceSetupCallback. */
  InstanceQueryTeardown,                /* InstanceQueryTeardownCallback. */
  NULL,                                 /* InstanceTeardownStartCallback. */
  NULL,                                 /* InstanceTeardownCompleteCallback. */
  NULL,                                 /* GenerateFileNameCallback. */
  NULL,                                 /* NormalizeNameComponentCallback. */
  NULL                                  /* NormalizeContextCleanupCallback. */

#if FLT_MGR_LONGHORN
  , NULL                                /* TransactionNotificationCallback. */
  , NULL                                /* NormalizeNameComponentExCallback. */
#endif /* FLT_MGR_LONGHORN */
#if FLT_MFG_WIN8
  , NULL                                /* SectionNotificationCallback. */
#endif
};

/* Restore the given section types back to their previous section definition. */
#ifdef ALLOC_DATA_PRAGMA
  #pragma data_seg()
  #pragma const_seg()
#endif /* ALLOC_DATA_PRAGMA */
