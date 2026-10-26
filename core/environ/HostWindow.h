#pragma once

class tTJSNI_Window;

// The embedded host presents one logical window. Input and presentation must
// use the same frontmost visible, enabled window; nullptr means none is ready.
tTJSNI_Window *TVPGetHostWindow();
