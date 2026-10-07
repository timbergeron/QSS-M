"""Bring an owned engine window to the foreground so Quakespasm-family clients
don't apply their unfocused 16 ms sleep. No synthetic input is sent."""
import ctypes
from ctypes import wintypes as W

_u = ctypes.windll.user32
_k = ctypes.windll.kernel32


def focus(pid):
    found = []
    cb = ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)

    def visit(h, _):
        owner = W.DWORD(); _u.GetWindowThreadProcessId(h, ctypes.byref(owner))
        if owner.value == pid and _u.IsWindowVisible(h):
            found.append(h)
        return True
    _u.EnumWindows(cb(visit), 0)
    for h in found:
        fg = _u.GetForegroundWindow()
        fg_thread = _u.GetWindowThreadProcessId(fg, None)
        me = _k.GetCurrentThreadId()
        attached = fg_thread and fg_thread != me and _u.AttachThreadInput(me, fg_thread, True)
        _u.ShowWindow(h, 5)
        _u.BringWindowToTop(h)
        _u.SetForegroundWindow(h)
        if attached:
            _u.AttachThreadInput(me, fg_thread, False)
    return bool(found) and _u.GetForegroundWindow() in found
