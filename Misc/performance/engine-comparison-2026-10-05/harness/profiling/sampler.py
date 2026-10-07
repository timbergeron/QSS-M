"""Minimal non-admin stack sampler: suspend a thread, StackWalk64, symbolize."""
import ctypes, ctypes.wintypes as W, time, collections

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
dbg = ctypes.WinDLL('dbghelp', use_last_error=True)

TH32CS_SNAPTHREAD = 0x4
class THREADENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', W.DWORD), ('cntUsage', W.DWORD), ('th32ThreadID', W.DWORD),
                ('th32OwnerProcessID', W.DWORD), ('tpBasePri', W.LONG), ('tpDeltaPri', W.LONG),
                ('dwFlags', W.DWORD)]

class ADDRESS64(ctypes.Structure):
    _fields_ = [('Offset', ctypes.c_uint64), ('Segment', W.WORD), ('Mode', W.DWORD)]

class KDHELP64(ctypes.Structure):
    _fields_ = [('f', ctypes.c_uint64 * 8), ('r', ctypes.c_uint64 * 5)]

class STACKFRAME64(ctypes.Structure):
    _fields_ = [('AddrPC', ADDRESS64), ('AddrReturn', ADDRESS64), ('AddrFrame', ADDRESS64),
                ('AddrStack', ADDRESS64), ('AddrBStore', ADDRESS64), ('FuncTableEntry', ctypes.c_void_p),
                ('Params', ctypes.c_uint64 * 4), ('Far', W.BOOL), ('Virtual', W.BOOL),
                ('Reserved', ctypes.c_uint64 * 3), ('KdHelp', KDHELP64)]

class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [('SizeOfStruct', W.ULONG), ('TypeIndex', W.ULONG), ('Reserved', ctypes.c_uint64 * 2),
                ('Index', W.ULONG), ('Size', W.ULONG), ('ModBase', ctypes.c_uint64), ('Flags', W.ULONG),
                ('Value', ctypes.c_uint64), ('Address', ctypes.c_uint64), ('Register', W.ULONG),
                ('Scope', W.ULONG), ('Tag', W.ULONG), ('NameLen', W.ULONG), ('MaxNameLen', W.ULONG),
                ('Name', ctypes.c_char * 512)]

class IMAGEHLP_MODULE64(ctypes.Structure):
    _fields_ = [('SizeOfStruct', W.DWORD), ('BaseOfImage', ctypes.c_uint64), ('ImageSize', W.DWORD),
                ('TimeDateStamp', W.DWORD), ('CheckSum', W.DWORD), ('NumSyms', W.DWORD),
                ('SymType', W.DWORD), ('ModuleName', ctypes.c_char * 32), ('ImageName', ctypes.c_char * 256),
                ('LoadedImageName', ctypes.c_char * 256), ('rest', ctypes.c_byte * 4096)]

dbg.SymFunctionTableAccess64.restype = ctypes.c_void_p
dbg.SymFunctionTableAccess64.argtypes = [W.HANDLE, ctypes.c_uint64]
dbg.SymGetModuleBase64.restype = ctypes.c_uint64
dbg.SymGetModuleBase64.argtypes = [W.HANDLE, ctypes.c_uint64]
FTA = ctypes.WINFUNCTYPE(ctypes.c_void_p, W.HANDLE, ctypes.c_uint64)
GMB = ctypes.WINFUNCTYPE(ctypes.c_uint64, W.HANDLE, ctypes.c_uint64)
fta = FTA(lambda h, a: dbg.SymFunctionTableAccess64(h, a))
gmb = GMB(lambda h, a: dbg.SymGetModuleBase64(h, a))
dbg.StackWalk64.argtypes = [W.DWORD, W.HANDLE, W.HANDLE, ctypes.POINTER(STACKFRAME64), ctypes.c_void_p,
                            ctypes.c_void_p, FTA, GMB, ctypes.c_void_p]
dbg.SymFromAddr.argtypes = [W.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(SYMBOL_INFO)]
dbg.SymGetModuleInfo64.argtypes = [W.HANDLE, ctypes.c_uint64, ctypes.POINTER(IMAGEHLP_MODULE64)]
k32.OpenThread.restype = W.HANDLE
k32.OpenProcess.restype = W.HANDLE

CTX_SIZE = 1232
def make_ctx():
    raw = ctypes.create_string_buffer(CTX_SIZE + 16)
    addr = (ctypes.addressof(raw) + 15) & ~15
    return raw, addr

def threads(pid):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    te = THREADENTRY32(); te.dwSize = ctypes.sizeof(te); out = []
    ok = k32.Thread32First(snap, ctypes.byref(te))
    while ok:
        if te.th32OwnerProcessID == pid: out.append(te.th32ThreadID)
        ok = k32.Thread32Next(snap, ctypes.byref(te))
    k32.CloseHandle(snap); return out

def thread_cpu(h):
    c, e, kt, ut = (W.FILETIME() for _ in range(4))
    k32.GetThreadTimes(h, ctypes.byref(c), ctypes.byref(e), ctypes.byref(kt), ctypes.byref(ut))
    f = lambda t: (t.dwHighDateTime << 32) | t.dwLowDateTime
    return f(kt) + f(ut)

class Sampler:
    def __init__(self, pid, sympath):
        self.hp = k32.OpenProcess(0x1F0FFF, False, pid)
        self.pid = pid
        dbg.SymSetOptions(0x2 | 0x4 | 0x10)  # UNDNAME|DEFERRED_LOADS|LOAD_LINES off? (0x10 = LOAD_LINES)
        dbg.SymSetOptions(0x2 | 0x4)
        if not dbg.SymInitialize(self.hp, sympath.encode(), True):
            raise OSError('SymInitialize', ctypes.get_last_error())  # needed for StackWalk64 unwinding
        self.cache = {}

    def busiest_thread(self, window=0.5):
        # The engine's main thread is the earliest-created one; the GL driver
        # runs its own busy worker thread that would otherwise win.
        hs = {t: k32.OpenThread(0x0040 | 0x0002 | 0x0008, False, t) for t in threads(self.pid)}
        def created(h):
            c, e, kt, ut = (W.FILETIME() for _ in range(4))
            k32.GetThreadTimes(h, ctypes.byref(c), ctypes.byref(e), ctypes.byref(kt), ctypes.byref(ut))
            return (c.dwHighDateTime << 32) | c.dwLowDateTime
        tid = min(hs, key=lambda t: created(hs[t]))
        a = thread_cpu(hs[tid]); time.sleep(window); b = thread_cpu(hs[tid])
        for t, h in hs.items():
            if t != tid: k32.CloseHandle(h)
        return tid, hs[tid], (b - a) / (window * 1e7)

    def walk(self, ht, ctx_addr):
        ctypes.c_uint32.from_address(ctx_addr + 0x30).value = 0x10000B  # CONTEXT_FULL (AMD64)
        if k32.SuspendThread(ht) == 0xFFFFFFFF: return None
        try:
            if not k32.GetThreadContext(ht, ctypes.c_void_p(ctx_addr)): return None
            sf = STACKFRAME64()
            sf.AddrPC.Offset = ctypes.c_uint64.from_address(ctx_addr + 0xF8).value; sf.AddrPC.Mode = 3
            sf.AddrStack.Offset = ctypes.c_uint64.from_address(ctx_addr + 0x98).value; sf.AddrStack.Mode = 3
            sf.AddrFrame.Offset = ctypes.c_uint64.from_address(ctx_addr + 0xA0).value; sf.AddrFrame.Mode = 3
            pcs = []
            for _ in range(64):
                if not dbg.StackWalk64(0x8664, self.hp, ht, ctypes.byref(sf), ctypes.c_void_p(ctx_addr), None, fta, gmb, None):
                    break
                if not sf.AddrPC.Offset: break
                pcs.append(sf.AddrPC.Offset)
            return pcs
        finally:
            k32.ResumeThread(ht)

    def modules(self):
        psapi = ctypes.WinDLL('psapi')
        arr = (ctypes.c_void_p * 1024)(); need = W.DWORD()
        psapi.EnumProcessModulesEx(self.hp, arr, ctypes.sizeof(arr), ctypes.byref(need), 3)
        mods = []
        class MI(ctypes.Structure):
            _fields_ = [('base', ctypes.c_void_p), ('size', W.DWORD), ('entry', ctypes.c_void_p)]
        for i in range(need.value // ctypes.sizeof(ctypes.c_void_p)):
            buf = ctypes.create_unicode_buffer(520)
            psapi.GetModuleFileNameExW(self.hp, ctypes.c_void_p(arr[i]), buf, 520)
            mi = MI(); psapi.GetModuleInformation(self.hp, ctypes.c_void_p(arr[i]), ctypes.byref(mi), ctypes.sizeof(mi))
            mods.append((mi.base, mi.size, buf.value))
        return mods

    def run(self, seconds, hz=1000):
        tid, ht, util = self.busiest_thread()
        print('sampling thread', tid, 'util', util, flush=True)
        raw, ctx = make_ctx()
        stacks = []
        end = time.perf_counter() + seconds
        period = 1.0 / hz
        nxt = time.perf_counter()
        while time.perf_counter() < end:
            pcs = self.walk(ht, ctx)
            if pcs: stacks.append(tuple(pcs))
            if len(stacks) % 1000 == 1: print('n', len(stacks), flush=True)
            nxt += period
            d = nxt - time.perf_counter()
            if d > 0: time.sleep(d)
        return tid, util, stacks, self.modules()

def symbolize(stacks, mods, exe_substr='quakespasm.exe'):
    import os
    fake = W.HANDLE(0x5151)
    dbg.SymSetOptions(0x2)
    dbg.SymInitialize(fake, None, False)
    dbg.SymLoadModuleEx.restype = ctypes.c_uint64
    dbg.SymLoadModuleEx.argtypes = [W.HANDLE, W.HANDLE, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64, W.DWORD, ctypes.c_void_p, W.DWORD]
    exe = None
    for base, size, path in mods:
        if path.lower().endswith(exe_substr):
            dbg.SymLoadModuleEx(fake, None, path.encode('mbcs'), None, base, size, None, 0)
            exe = (base, size)
    cache = {}
    def name(a):
        if a in cache: return cache[a]
        mod = '?'
        for base, size, path in mods:
            if base <= a < base + size: mod = os.path.splitext(os.path.basename(path))[0]; break
        n = mod + '!?'
        if exe and exe[0] <= a < exe[0] + exe[1]:
            si = SYMBOL_INFO(); si.SizeOfStruct = 88; si.MaxNameLen = 500
            disp = ctypes.c_uint64()
            if dbg.SymFromAddr(fake, a, ctypes.byref(disp), ctypes.byref(si)):
                n = mod + '!' + si.Name.decode(errors='replace')
        cache[a] = n
        return n
    return [tuple(name(a) for a in st) for st in stacks]

def report(stacks, engine='quakespasm', top=45):
    n = len(stacks)
    self_c = collections.Counter(s[0] for s in stacks)
    incl = collections.Counter()
    eng_leaf = collections.Counter()
    for s in stacks:
        for f in set(s): incl[f] += 1
        e = next((f for f in s if f.startswith(engine + '!')), 'none')
        eng_leaf[e] += 1
    mods = collections.Counter(s[0].split('!')[0] for s in stacks)
    out = [f'samples={n}', '-- leaf module --']
    out += [f'{c*100/n:6.2f}% {m}' for m, c in mods.most_common(12)]
    out += ['-- self (leaf function) --']
    out += [f'{c*100/n:6.2f}% {f}' for f, c in self_c.most_common(top)]
    out += ['-- innermost engine function (driver/OS time charged to caller) --']
    out += [f'{c*100/n:6.2f}% {f}' for f, c in eng_leaf.most_common(top)]
    out += ['-- inclusive (engine) --']
    out += [f'{c*100/n:6.2f}% {f}' for f, c in incl.most_common(200) if f.startswith(engine + '!')][:top + 30]
    return '\n'.join(out)
