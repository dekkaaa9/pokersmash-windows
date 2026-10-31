// Poker Smash Launcher
//
// Entry point. The SDL3 windowed-app main (supplied by rexglue_configure_target)
// calls GetWindowedAppCreator, which REX_DEFINE_APP provides here.

#include "launcher_app.h"

REX_DEFINE_APP(pokersmash_launcher, pokersmash_launcher::LauncherApp::Create)
