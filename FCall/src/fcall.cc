#include <cctk.h>
#include <cctk_Schedule.h>
#include <iostream>
#include <cctk_Parameters.h>
#include <cmath>

namespace fcall {

  int pre_call(const void *arg1,void *arg2,void *arg3,void *arg4) {
    const cGH *cctkGH = (const cGH *)arg1;
    const cFunctionData *attribute = (const cFunctionData *)arg3;
    std::cout << "/=== " << attribute->thorn << "::" << attribute->routine << " in " << attribute->where << "\n";
    return 0;
  }

  int post_call(const void *arg1,void *arg2,void *arg3,void *arg4) {
    const cGH *cctkGH = (const cGH *)arg1;
    const cFunctionData *attribute = (const cFunctionData *)arg3;
    std::cout << "\\=== " << attribute->thorn << "::" << attribute->routine << " in " << attribute->where << "\n";
    return 0;
  }

  extern "C" void FCall_AddDiagnosticCalls() {
    RegisterScheduleWrapper(pre_call,post_call);
  }
}
