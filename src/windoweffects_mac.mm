#include "windoweffects.h"
#include <QWindow>
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>

void WindowEffects::applyNative() {
    @autoreleasepool {
        auto *view = reinterpret_cast<NSView *>(m_window->winId());
        NSWindow *window = view.window;
        if (!window) return;
        window.level = m_onTop ? NSFloatingWindowLevel : NSNormalWindowLevel;
        window.collectionBehavior = m_onTop
            ? NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary
            : NSWindowCollectionBehaviorManaged;
        window.hidesOnDeactivate = NO;
        window.opaque = NO;
        window.backgroundColor = NSColor.clearColor;
        static char effectKey;
        auto *effect = (NSVisualEffectView *)objc_getAssociatedObject(window, &effectKey);
        if (m_backdrop) {
            if (!effect) {
                effect = [[NSVisualEffectView alloc] initWithFrame:view.frame];
                effect.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
                effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
                effect.state = NSVisualEffectStateActive;
                effect.wantsLayer = YES;
                effect.layer.cornerRadius = 20;
                effect.layer.masksToBounds = YES;
                [view.superview addSubview:effect positioned:NSWindowBelow relativeTo:view];
                objc_setAssociatedObject(window, &effectKey, effect, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                [effect release];
            }
            effect.material = NSVisualEffectMaterialUnderWindowBackground;
            effect.appearance = [NSAppearance appearanceNamed:m_backdrop == 2 ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
        } else if (effect) {
            [effect removeFromSuperview];
            objc_setAssociatedObject(window, &effectKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
        if (m_onTop) [window orderFrontRegardless];
        capabilities(true, true);
    }
}
