#include "KeyLayout.h"

#if defined(__APPLE__)
#include <Carbon/Carbon.h>
#endif

namespace keylayout {

namespace {

// US layout, used where the current layout cannot be read.
char32_t usCharacter(Position position)
{
    switch (position) {
    case A: return 'a'; case S: return 's'; case D: return 'd'; case F: return 'f'; case H: return 'h';
    case G: return 'g'; case Z: return 'z'; case X: return 'x'; case C: return 'c'; case V: return 'v';
    case B: return 'b'; case Q: return 'q'; case W: return 'w'; case E: return 'e'; case R: return 'r';
    case Y: return 'y'; case T: return 't'; case K1: return '1'; case K2: return '2'; case K3: return '3';
    case K6: return '6'; case K5: return '5'; case K9: return '9'; case K7: return '7'; case K0: return '0';
    case O: return 'o'; case U: return 'u'; case I: return 'i'; case P: return 'p'; case L: return 'l';
    case J: return 'j'; case Semicolon: return ';'; case Comma: return ','; case Slash: return '/';
    case N: return 'n'; case M: return 'm'; case Period: return '.';
    }
    return 0;
}

} // namespace

char32_t characterAt(Position position)
{
#if defined(__APPLE__)
    TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();
    if (source == nullptr)
        return usCharacter(position);
    auto data = static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
    char32_t result = 0;
    if (data != nullptr) {
        const auto* layout = reinterpret_cast<const UCKeyboardLayout*>(CFDataGetBytePtr(data));
        UInt32 deadKeys = 0;
        UniChar chars[4];
        UniCharCount length = 0;
        if (UCKeyTranslate(layout, position, kUCKeyActionDown, 0, LMGetKbdType(), kUCKeyTranslateNoDeadKeysBit,
                           &deadKeys, 4, &length, chars) == noErr
            && length == 1)
            result = chars[0];
    }
    CFRelease(source);
    return result != 0 ? result : usCharacter(position);
#else
    return usCharacter(position);
#endif
}

} // namespace keylayout
