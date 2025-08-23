#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <Python.h>
#include <iostream>
#include <vector>
#include <string>

// --- Pattern scanner helpers ---
bool DataCompare(const BYTE* data, const BYTE* pattern, const char* mask) {
    for (; *mask; ++mask, ++data, ++pattern) {
        if (*mask == 'x' && *data != *pattern) {
            return false;
        }
    }
    return (*mask) == 0;
}

uintptr_t PatternScan(HANDLE hProc, uintptr_t base, SIZE_T size, const char* pattern, const char* mask) {
    std::vector<BYTE> buffer(size);
    SIZE_T bytesRead;
    if (ReadProcessMemory(hProc, (LPCVOID)base, buffer.data(), size, &bytesRead)) {
        for (SIZE_T i = 0; i < bytesRead; i++) {
            if (DataCompare(buffer.data() + i, (BYTE*)pattern, mask)) {
                return base + i;
            }
        }
    }
    return 0;
}

// --- Globals ---
HANDLE gProcess = nullptr;
uintptr_t gInputStruct = 0;

// Attach to RocketLeague.exe
bool AttachProcess(const wchar_t* procName, DWORD& pid, uintptr_t& base, DWORD& moduleSize) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (!_wcsicmp(entry.szExeFile, procName)) {
                pid = entry.th32ProcessID;
                CloseHandle(snapshot);
                gProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
                if (!gProcess) return false;

                HMODULE hMods[1024];
                DWORD cbNeeded;
                if (EnumProcessModules(gProcess, hMods, sizeof(hMods), &cbNeeded)) {
                    base = (uintptr_t)hMods[0];
                    MODULEINFO modInfo;
                    GetModuleInformation(gProcess, hMods[0], &modInfo, sizeof(modInfo));
                    moduleSize = modInfo.SizeOfImage;
                    return true;
                }
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return false;
}

// Example: Scan for input struct pattern
bool FindOffsets() {
    DWORD pid = 0;
    uintptr_t base = 0;
    DWORD size = 0;

    if (!AttachProcess(L"RocketLeague.exe", pid, base, size)) {
        std::cerr << "[MemoryWriter] Could not attach to RocketLeague.exe\n";
        return false;
    }

    // Replace with correct pattern + mask for your input struct
    const char* pattern = "\x48\x8B\x05\x00\x00\x00\x00\x48\x8B\x48\x08";
    const char* mask    = "xxx????xxxx";

    gInputStruct = PatternScan(gProcess, base, size, pattern, mask);

    if (gInputStruct) {
        std::cout << "[MemoryWriter] Found InputStruct at 0x" << std::hex << gInputStruct << "\n";
        return true;
    } else {
        std::cerr << "[MemoryWriter] Failed to locate InputStruct via pattern scan\n";
        return false;
    }
}

// --- Exposed to Python ---
static PyObject* mw_write(PyObject* self, PyObject* args) {
    PyObject* dict;
    if (!PyArg_ParseTuple(args, "O!", &PyDict_Type, &dict)) {
        return nullptr;
    }

    if (!gProcess || !gInputStruct) {
        PyErr_SetString(PyExc_RuntimeError, "No valid process/offsets (Rocket League not attached).");
        return nullptr;
    }

    // Example: extract throttle & steer from Python dict
    PyObject* pyThrottle = PyDict_GetItemString(dict, "throttle");
    PyObject* pySteer    = PyDict_GetItemString(dict, "steer");

    float throttle = pyThrottle ? (float)PyFloat_AsDouble(pyThrottle) : 0.0f;
    float steer    = pySteer ? (float)PyFloat_AsDouble(pySteer) : 0.0f;

    // Replace with actual WriteProcessMemory call to RL struct
    BOOL success = WriteProcessMemory(gProcess, (LPVOID)(gInputStruct), &throttle, sizeof(throttle), nullptr);

    if (!success) {
        PyErr_SetString(PyExc_RuntimeError, "WriteProcessMemory failed.");
        return nullptr;
    }

    Py_RETURN_NONE;
}

static PyMethodDef MWMethods[] = {
    {"write", mw_write, METH_VARARGS, "Write inputs to Rocket League memory."},
    {nullptr, nullptr, 0, nullptr}
};

static struct PyModuleDef mwmodule = {
    PyModuleDef_HEAD_INIT, "memory_writer", nullptr, -1, MWMethods
};

PyMODINIT_FUNC PyInit_memory_writer(void) {
    if (!FindOffsets()) {
        std::cerr << "[MemoryWriter] Offsets not found, module loaded in dry-run mode.\n";
    }
    return PyModule_Create(&mwmodule);
}
