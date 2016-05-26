#include <cctk.h>
#include <cctk_Schedule.h>
#include <iostream>
#include <cctk_Parameters.h>
#include <cmath>

namespace trace {
  int trace_vindex = -5;
  CCTK_REAL trace_old_value = 0;
  CCTK_REAL trace_new_value = 0;

  void fetch_var(const cGH *cctkGH) {
    trace_old_value = trace_new_value;
    if(cctkGH == 0)
      return;
    DECLARE_CCTK_PARAMETERS;
    if(trace_vindex == -5) {
      trace_vindex = CCTK_VarIndex(trace_varname);
      if(trace_vindex < 0) {
        std::cerr << "trace_vindex=" << trace_vindex << "\n";
        abort();
      }
    }
    if(trace_vindex >= 0) {
      CCTK_REAL *gf = (CCTK_REAL*)CCTK_VarDataPtrI(cctkGH,0,trace_vindex);
      if(gf != 0) {
        assert(trace_xcoord >= 0 && trace_xcoord < cctkGH->cctk_lsh[0]);
        assert(trace_ycoord >= 0 && trace_ycoord < cctkGH->cctk_lsh[1]);
        assert(trace_zcoord >= 0 && trace_zcoord < cctkGH->cctk_lsh[2]);
        int index = CCTK_GFINDEX3D(cctkGH,trace_xcoord,trace_ycoord,trace_zcoord);
        trace_new_value = gf[index];
      }
    }
  }

  bool cmp(CCTK_REAL r1,CCTK_REAL r2) {
    if(std::isnan(r1) and std::isnan(r2)) return true;
    return r1 == r2;
  }

  int pre_call(const void *arg1,void *arg2,void *arg3,void *arg4) {
    const cGH *cctkGH = (const cGH *)arg1;
    const cFunctionData *attribute = (const cFunctionData *)arg3;
    //std::cout << "/=== " << attribute->thorn << "::" << attribute->routine << " in " << attribute->where << "\n";
    fetch_var(cctkGH);
    if(!cmp(trace_new_value,trace_old_value)) {
      std::cout << std::scientific << "VALUE CHANGED BEFORE " << attribute->thorn << "::" << attribute->routine << " in " << attribute->where
        << " old value=" << trace_old_value << " new value=" << trace_new_value << "\n";
    }
    return 0;
  }

  int post_call(const void *arg1,void *arg2,void *arg3,void *arg4) {
    const cGH *cctkGH = (const cGH *)arg1;
    const cFunctionData *attribute = (const cFunctionData *)arg3;
    fetch_var(cctkGH);
    if(!cmp(trace_new_value,trace_old_value)) {
      std::cout << std::scientific << "VALUE CHANGED INSIDE " << attribute->thorn << "::" << attribute->routine << " in " << attribute->where
        << " old value=" << trace_old_value << " new value=" << trace_new_value << "\n";
    }
    //std::cout << "\\=== " << attribute->thorn << "::" << attribute->routine << " in " << attribute->where << "\n";
    return 0;
  }

  extern "C" void Trace_AddDiagnosticCalls() {
    RegisterScheduleWrapper(pre_call,post_call);
  }
}
