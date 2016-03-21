#ifndef RDWR_DECLARE_H
#define RDWR_DECLARE_H

#define CCTKi_VarDataPtrI(CGH,TL,VI) RDWR_VarDataPtrI(CGH,TL,VI)
extern
#ifdef __cplusplus
"C"
#endif
void *RDWR_VarDataPtrI(const cGH *,int,int);

#endif
