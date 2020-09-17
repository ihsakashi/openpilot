#ifndef RUN_H
#define RUN_H

#include "runmodel.h"
#include "snpemodel.h"

#if defined(QCOM) || defined(NEOS)
  #define DefaultRunModel SNPEModel
#else
  #ifdef USE_TF_MODEL
    #include "tfmodel.h"
    #define DefaultRunModel TFModel
  #else
    #define DefaultRunModel SNPEModel
  #endif
#endif

#endif
