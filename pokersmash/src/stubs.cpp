// stubs.cpp -- project-level kernel stubs for APIs the ReXGlue runtime doesn't export.
//
// The SDK implements the Xbox 360 Vision Camera API in
// src/kernel/xboxkrnl/xboxkrnl_usbcam.cpp but leaves that translation unit
// commented out of the runtime build (see src/kernel/CMakeLists.txt), so
// rexruntime.dll does not export the __imp__XUsbcam* symbols. Poker Smash pulls
// the camera library in through a shared XDK component and never actually uses
// a camera, so the whole surface is stubbed here.
//
// Create must report success: titles that treat a nonzero result as fatal abort
// their init path. GetState reports "no camera connected" (0). The rest are
// no-ops, since no frame is ever read.
#include <rex/hook.h>
#include <rex/types.h>
#include <rex/system/xtypes.h>
#include <rex/system/kernel_state.h>

namespace rex::kernel::xboxkrnl {

u32 XUsbcamCreate_entry(u32 buffer, u32 buffer_size, mapped_void unk3_ptr) {
  (void)buffer;
  (void)buffer_size;
  (void)unk3_ptr;
  return X_STATUS_SUCCESS;
}

u32 XUsbcamGetState_entry() {
  return 0;  // 0 = camera not connected
}

}  // namespace rex::kernel::xboxkrnl

REX_EXPORT(__imp__XUsbcamCreate, rex::kernel::xboxkrnl::XUsbcamCreate_entry)
REX_EXPORT(__imp__XUsbcamGetState, rex::kernel::xboxkrnl::XUsbcamGetState_entry)
REX_EXPORT_STUB(__imp__XUsbcamDestroy);
REX_EXPORT_STUB(__imp__XUsbcamSetConfig);
REX_EXPORT_STUB(__imp__XUsbcamSetView);
REX_EXPORT_STUB(__imp__XUsbcamSetCaptureMode);
REX_EXPORT_STUB(__imp__XUsbcamReadFrame);
