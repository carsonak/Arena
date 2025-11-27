#ifndef DS_LEN_TYPE_H
#define DS_LEN_TYPE_H

#include <inttypes.h>  // intptr_t, PRIdPTR

/*! for lengths and counting. */
typedef intptr_t len_ty;
#define LEN_TY_max INTPTR_MAX
#define LEN_TY_min INTPTR_MIN
/*! Macro for printing len_ty. */
#define PRI_len PRIdPTR

/*! unsigned version of len_ty. */
typedef uintptr_t ulen_ty;
#define ULEN_TY_max UINTPTR_MAX
#define ULEN_TY_min UINTPTR_MIN
/*! Macro for printing ulen_ty. */
#define PRI_ulen PRIuPTR

#endif  // DS_LEN_TYPE_H
