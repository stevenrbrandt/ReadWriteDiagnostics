#include <cctk.h>
#include <cctk_Schedule.h>

namespace Carpet {

typedef CCTK_INT (*func)(CCTK_POINTER_TO_CONST cctkGH, CCTK_POINTER function,
                         CCTK_POINTER attribute, CCTK_POINTER data);

extern "C" CCTK_INT Carpet_RegisterScheduleWrapper(func const func_before,
                                                   func const func_after);

extern "C" CCTK_INT Carpet_UnRegisterScheduleWrapper(func const func_before,
                                                     func const func_after);
} // namespace Carpet
