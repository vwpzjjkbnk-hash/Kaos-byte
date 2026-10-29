// SkyPortal360 - skypop: the figure menu as the console's own message box (XamShowMessageBoxUI),
// with "« Previous", "Put on portal" / "Swap in" / "Take off", "Next »" buttons. Previous/Next
// reopen the box on the neighbouring figure with the same button focused, so holding A keeps
// stepping. Opened by tapping the menu modifier (LB+RB by default).
#pragma once

namespace popmenu {

void Open(int pad);   // no-op while already open
void Poll();          // call often from the worker thread: handles the box closing
bool IsOpen();
// The game it was shown over has exited (Dashlaunch build): the box went with it, so stop waiting
// for its answer.
void Forget();

}  // namespace popmenu
