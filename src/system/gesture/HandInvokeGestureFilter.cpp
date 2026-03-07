#include "gesture/HandInvokeGestureFilter.h"

HandInvokeGestureFilter::HandInvokeGestureFilter()
    : unk4(Vector3(0, 0, -1), 3, 0), unk50(Vector3::GetZero(), 3, 0),
      unk8c(Vector3::GetZero(), 6, 0), unkc8(Vector3::GetZero(), 3, 0),
      unk104(Vector3::GetZero(), 6, 0), unk140(0), unk144(0) {}

HandInvokeGestureFilter::~HandInvokeGestureFilter() {}
