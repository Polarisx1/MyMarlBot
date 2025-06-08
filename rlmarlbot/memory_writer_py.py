import os
import threading
import time
import ctypes
from ctypes import wintypes

# Ensure wintypes has ULONG_PTR for compatibility with older Python versions
if not hasattr(wintypes, "ULONG_PTR"):
    if ctypes.sizeof(ctypes.c_void_p) == ctypes.sizeof(ctypes.c_ulong):
        wintypes.ULONG_PTR = ctypes.c_ulong
    else:
        wintypes.ULONG_PTR = ctypes.c_ulonglong

if os.name != "nt":
    raise OSError("memory_writer_py is only supported on Windows")

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

PROCESS_VM_WRITE = 0x0020
PROCESS_VM_OPERATION = 0x0008
TH32CS_SNAPPROCESS = 0x00000002
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value

class PROCESSENTRY32(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("th32DefaultHeapID", wintypes.ULONG_PTR),
        ("th32ModuleID", wintypes.DWORD),
        ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", wintypes.DWORD),
        ("szExeFile", wintypes.WCHAR * 260),
    ]

kernel32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
kernel32.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32)]
kernel32.Process32FirstW.restype = wintypes.BOOL
kernel32.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32)]
kernel32.Process32NextW.restype = wintypes.BOOL
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.WriteProcessMemory.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.LPCVOID, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
kernel32.WriteProcessMemory.restype = wintypes.BOOL
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.restype = wintypes.BOOL

class MemoryWriter:
    def __init__(self):
        self.h_process = None
        self.running = False
        self.thread = None
        self.address = 0
        self.data = b""
        self.lock = threading.Lock()

    def open_process(self, process_name: str) -> bool:
        process_name_l = process_name.lower()
        entry = PROCESSENTRY32()
        entry.dwSize = ctypes.sizeof(PROCESSENTRY32)
        snapshot = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
        if snapshot == INVALID_HANDLE_VALUE:
            return False

        exact_pid = None
        partial_pid = None
        partial_name = None

        if kernel32.Process32FirstW(snapshot, ctypes.byref(entry)):
            while True:
                exe_name = entry.szExeFile
                exe_name_l = exe_name.lower()

                if exe_name_l == process_name_l:
                    exact_pid = entry.th32ProcessID
                    break

                if partial_pid is None and process_name_l in exe_name_l:
                    partial_pid = entry.th32ProcessID
                    partial_name = exe_name

                if not kernel32.Process32NextW(snapshot, ctypes.byref(entry)):
                    break

        selected_pid = exact_pid if exact_pid is not None else partial_pid
        if selected_pid is not None:
            if exact_pid is None and partial_name is not None:
                print(
                    f"Process '{process_name}' not found exactly. Using first match '{partial_name}'."
                )
            self.h_process = kernel32.OpenProcess(
                PROCESS_VM_WRITE | PROCESS_VM_OPERATION,
                False,
                selected_pid,
            )

        kernel32.CloseHandle(snapshot)
        return bool(self.h_process)

    def open_process_by_id(self, pid: int) -> bool:
        self.h_process = kernel32.OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION, False, pid)
        return bool(self.h_process)

    def auto_open(self, process_names=None) -> bool:
        """Try to attach to any known Rocket League process.

        Parameters
        ----------
        process_names : list[str] | None
            List of executable names to search for. If ``None`` a default list
            of common Rocket League executables is used.

        Returns
        -------
        bool
            ``True`` if a process was successfully attached, ``False`` otherwise.
        """
        if process_names is None:
            process_names = ["RocketLeague.exe", "RocketLeague"]

        for name in process_names:
            if self.open_process(name):
                return True

        return False

    def start(self):
        if self.h_process and not self.running:
            self.running = True
            self.thread = threading.Thread(target=self._write_loop, daemon=True)
            self.thread.start()

    def stop(self):
        self.running = False
        if self.thread:
            self.thread.join()
            self.thread = None

    def set_memory_data(self, address: int, data):
        if isinstance(data, str):
            data = data.encode("utf-8")
        with self.lock:
            self.address = address
            self.data = data

    def _write_loop(self):
        while self.running:
            with self.lock:
                addr = self.address
                buf = self.data
            if addr and buf:
                size = len(buf)
                written = ctypes.c_size_t()
                kernel32.WriteProcessMemory(self.h_process, ctypes.c_void_p(addr), buf, size, ctypes.byref(written))
            time.sleep(0.001)

    def __del__(self):
        self.stop()
        if self.h_process:
            kernel32.CloseHandle(self.h_process)
            self.h_process = None
