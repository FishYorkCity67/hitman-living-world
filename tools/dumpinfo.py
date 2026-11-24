"""Minimaler Minidump-Leser: Ausnahme, Adresse, Modul + Symbol (ueber dbghelp) und Stack-Rueckverfolgung (heuristisch)."""
import ctypes
import ctypes.wintypes as wt
import struct
import sys

path = sys.argv[1]
pdb_dir = sys.argv[2] if len(sys.argv) > 2 else None
data = open(path, "rb").read()

sig, ver, nstreams, dir_rva = struct.unpack_from("<IIII", data, 0)
streams = {}
for i in range(nstreams):
    stype, size, rva = struct.unpack_from("<III", data, dir_rva + i * 12)
    streams.setdefault(stype, (size, rva))


def read_string(rva):
    length = struct.unpack_from("<I", data, rva)[0]
    return data[rva + 4: rva + 4 + length].decode("utf-16le")


modules = []
if 4 in streams:
    size, rva = streams[4]
    count = struct.unpack_from("<I", data, rva)[0]
    for i in range(count):
        off = rva + 4 + i * 108
        base, msize = struct.unpack_from("<QI", data, off)
        name_rva = struct.unpack_from("<I", data, off + 20)[0]
        modules.append((base, msize, read_string(name_rva)))


def module_of(addr):
    for base, msize, name in modules:
        if base <= addr < base + msize:
            return base, name
    return None, None


# Symbolaufloesung ueber dbghelp.dll (Windows-Bordmittel)
dbghelp = ctypes.WinDLL("dbghelp.dll")
proc = ctypes.c_void_p(0x1234)
dbghelp.SymSetOptions(0x2 | 0x10 | 0x40000)  # UNDNAME | LOAD_LINES | ...
dbghelp.SymInitializeW(proc, ctypes.c_wchar_p(pdb_dir), False)
for base, msize, name in modules:
    if "livingworld" in name.lower() or "zhmmodsdk" in name.lower():
        local = name
        if pdb_dir and "livingworld" in name.lower():
            local = pdb_dir + "\\LivingWorld.dll"
        dbghelp.SymLoadModuleExW(proc, None, ctypes.c_wchar_p(local), None, ctypes.c_uint64(base), msize, None, 0)


class SYMBOL_INFOW(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wt.ULONG), ("TypeIndex", wt.ULONG), ("Reserved", ctypes.c_uint64 * 2),
                ("Index", wt.ULONG), ("Size", wt.ULONG), ("ModBase", ctypes.c_uint64), ("Flags", wt.ULONG),
                ("Value", ctypes.c_uint64), ("Address", ctypes.c_uint64), ("Register", wt.ULONG),
                ("Scope", wt.ULONG), ("Tag", wt.ULONG), ("NameLen", wt.ULONG), ("MaxNameLen", wt.ULONG),
                ("Name", ctypes.c_wchar * 512)]


class IMAGEHLP_LINEW64(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wt.DWORD), ("Key", ctypes.c_void_p), ("LineNumber", wt.DWORD),
                ("FileName", ctypes.c_wchar_p), ("Address", ctypes.c_uint64)]


def symbol(addr):
    sym = SYMBOL_INFOW()
    sym.SizeOfStruct = 88
    sym.MaxNameLen = 500
    disp = ctypes.c_uint64(0)
    text = ""
    if dbghelp.SymFromAddrW(proc, ctypes.c_uint64(addr), ctypes.byref(disp), ctypes.byref(sym)):
        text = f"{sym.Name}+0x{disp.value:x}"
        line = IMAGEHLP_LINEW64()
        line.SizeOfStruct = ctypes.sizeof(IMAGEHLP_LINEW64)
        d32 = wt.DWORD(0)
        if dbghelp.SymGetLineFromAddrW64(proc, ctypes.c_uint64(addr), ctypes.byref(d32), ctypes.byref(line)):
            text += f"  ({line.FileName.split(chr(92))[-1]}:{line.LineNumber})"
    return text


# Ausnahme
size, rva = streams[6]
thread_id = struct.unpack_from("<I", data, rva)[0]
code, flags = struct.unpack_from("<II", data, rva + 8)
addr = struct.unpack_from("<Q", data, rva + 8 + 16)[0]
nparams = struct.unpack_from("<I", data, rva + 8 + 24)[0]
params = struct.unpack_from("<15Q", data, rva + 8 + 32)
base, name = module_of(addr)
print(f"Ausnahme 0x{code:08X} in Thread {thread_id} bei 0x{addr:X}")
print(f"  Modul: {name} + 0x{addr - base:X}" if base else "  Modul: ?")
print(f"  Symbol: {symbol(addr)}")
if code == 0xC0000005:
    print(f"  Zugriff: {'Schreiben' if params[0] == 1 else 'Lesen'} auf 0x{params[1]:X}")

# Stack des abgestuerzten Threads: Kontext -> RSP, dann Speicher lesen und Ruecksprungadressen in Modulen suchen.
ctx_size, ctx_rva = struct.unpack_from("<II", data, rva + 8 + 152)
rsp = struct.unpack_from("<Q", data, ctx_rva + 0x98)[0]
rip = struct.unpack_from("<Q", data, ctx_rva + 0xF8)[0]
print(f"  RIP 0x{rip:X}, RSP 0x{rsp:X}")

# Speicherbereiche (Memory64List = 9 oder MemoryList = 5)
mem_ranges = []
if 5 in streams:
    size, mrva = streams[5]
    count = struct.unpack_from("<I", data, mrva)[0]
    for i in range(count):
        start, dsize, drva = struct.unpack_from("<QII", data, mrva + 4 + i * 16)
        mem_ranges.append((start, dsize, drva))
if 9 in streams:
    size, mrva = streams[9]
    count, base_rva = struct.unpack_from("<QQ", data, mrva)
    cur = base_rva
    for i in range(count):
        start, dsize = struct.unpack_from("<QQ", data, mrva + 16 + i * 16)
        mem_ranges.append((start, dsize, cur))
        cur += dsize

print("\nMoegliche Aufrufkette (Ruecksprungadressen auf dem Stack):")
shown = 0
for start, dsize, drva in mem_ranges:
    if start <= rsp < start + dsize:
        for off in range(rsp - start, min(dsize, rsp - start + 0x3000), 8):
            val = struct.unpack_from("<Q", data, drva + off)[0]
            b, n = module_of(val)
            if b and ("livingworld" in n.lower() or "zhmmodsdk" in n.lower() or "hitman3" in n.lower()):
                s = symbol(val) if "hitman3" not in n.lower() else ""
                print(f"  {n.split(chr(92))[-1]} + 0x{val - b:X}  {s}")
                shown += 1
                if shown > 25:
                    break
        break
