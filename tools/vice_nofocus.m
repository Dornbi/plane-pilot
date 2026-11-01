// Injected into x64sc by the wrapper tools/vice_nofocus.sh installs. VICE and
// GDK activate the app (and so take the focus) when the emulator starts; this
// turns those calls into no-ops. Set NOFOCUS_LOG=1 to see each blocked call.
#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>

static void blocked(const char *what) {
    if (getenv("NOFOCUS_LOG")) fprintf(stderr, "nofocus: blocked %s\n", what);
}

static void nop_activate_ignoring(id self, SEL _cmd, BOOL flag) { blocked("activateIgnoringOtherApps:"); }
static void nop_activate(id self, SEL _cmd) { blocked("activate"); }
static BOOL nop_activate_options(id self, SEL _cmd, NSUInteger options) { blocked("activateWithOptions:"); return YES; }

static void replace(Class c, SEL sel, IMP imp) {
    Method m = class_getInstanceMethod(c, sel);
    if (m) method_setImplementation(m, imp);
}

__attribute__((constructor)) static void nofocus_init(void) {
    replace([NSApplication class], @selector(activateIgnoringOtherApps:), (IMP)nop_activate_ignoring);
    replace([NSApplication class], @selector(activate), (IMP)nop_activate);
    replace([NSRunningApplication class], @selector(activateWithOptions:), (IMP)nop_activate_options);
}
