#include <windows.h>
#include <tlhelp32.h>
#include <pybind11/pybind11.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <string>
#include <mutex>
#include <pybind11/gil.h> 

namespace py = pybind11;

std::string ConvertWideToUTF8(const WCHAR* wide) {
    if (wide == nullptr) return "";
    int buffer_size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string narrow(buffer_size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, &narrow[0], buffer_size, nullptr, nullptr);
    return narrow;
}

class MemoryWriter {
public:
    MemoryWriter() : hProcess(nullptr), running(false), address(0) {}

    bool open_process(const std::string& processName) {
        PROCESSENTRY32 entry;
        entry.dwSize = sizeof(PROCESSENTRY32);
        py::print("Opening process ", processName);

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
        if (snapshot == INVALID_HANDLE_VALUE) return false;

        DWORD exactPID = 0;
        DWORD partialPID = 0;
        std::string partialName;

        if (Process32First(snapshot, &entry)) {
            do {
                std::string currentName = ConvertWideToUTF8(entry.szExeFile);
                if (_stricmp(currentName.c_str(), processName.c_str()) == 0) {
                    exactPID = entry.th32ProcessID;
                    break;
                }

                if (partialPID == 0) {
                    std::string currentLower = currentName;
                    std::string targetLower = processName;
                    std::transform(currentLower.begin(), currentLower.end(), currentLower.begin(), ::tolower);
                    std::transform(targetLower.begin(), targetLower.end(), targetLower.begin(), ::tolower);
                    if (currentLower.find(targetLower) != std::string::npos) {
                        partialPID = entry.th32ProcessID;
                        partialName = currentName;
                    }
                }
            } while (Process32Next(snapshot, &entry));
        }

        DWORD pidToOpen = exactPID != 0 ? exactPID : partialPID;
        if (pidToOpen != 0) {
            if (exactPID == 0 && !partialName.empty()) {
                py::print("Process '", processName, "' not found exactly. Using first match '", partialName, "'.");
            }
            hProcess = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, pidToOpen);
            CloseHandle(snapshot);
            return hProcess != nullptr;
        }

        CloseHandle(snapshot);
        py::print("Process not found: ", processName);
        return false;
    }

    bool open_process_by_id(DWORD processID) {
        py::print("Opening process by ID: ", processID);
        hProcess = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, processID);
        if (hProcess != nullptr) {
            py::print("Process opened by ID: ", processID);
            return true;
        }
        else {
            py::print("Failed to open process by ID: ", processID);
            return false;
        }
    }

    bool auto_open(py::object processNamesObj = py::none()) {
        std::vector<std::string> processNames;
        if (!processNamesObj.is_none()) {
            for (auto item : processNamesObj) {
                processNames.push_back(item.cast<std::string>());
            }
        } else {
            processNames = {"RocketLeague.exe", "RocketLeague"};
        }

        for (const auto& name : processNames) {
            if (open_process(name)) {
                return true;
            }
        }

        return false;
    }

    void start() {
        if (hProcess && !running) {
            running = true;
            worker = std::thread(&MemoryWriter::write_memory, this);
        }
    }

    void stop() {
        running = false;
        if (worker.joinable()) {
            worker.join();
        }
    }

    void set_memory_data(uintptr_t new_address, const std::string& new_data) {
        std::lock_guard<std::mutex> lock(data_mutex);
        address = new_address;
        data = new_data;
    }

    ~MemoryWriter() {
        stop();
        if (hProcess) {
            CloseHandle(hProcess);
        }
    }

private:
    HANDLE hProcess;
    std::atomic<bool> running;
    std::thread worker;
    uintptr_t address;
    std::string data;
    std::mutex data_mutex;

    void write_memory() {
        SIZE_T bytesWritten;
        int error_count = 0;
        const int MAX_ERRORS = 10;
        while (running) {
            std::string local_data;
            uintptr_t local_address;

            {
                std::lock_guard<std::mutex> lock(data_mutex);
                local_data = data;
                local_address = address;
            }

            if (local_address && !local_data.empty()) {
                BOOL ok = WriteProcessMemory(hProcess, (LPVOID)local_address, local_data.c_str(), local_data.size(), &bytesWritten);
                if (!ok || bytesWritten != local_data.size()) {
                    DWORD err = GetLastError();
                    {
                        py::gil_scoped_acquire gil;
                        py::print("WriteProcessMemory failed with error", err);
                    }
                    if (++error_count >= MAX_ERRORS) {
                        {
                            py::gil_scoped_acquire gil;
                            py::print("Stopping writer after consecutive errors");
                        }
                        running = false;
                        break;
                    }
                } else {
                    error_count = 0;
                }
            } else {
                // No data to write; sleep briefly to avoid busy loop
                Sleep(1);
            }

            Sleep(1);
        }
    }
};

PYBIND11_MODULE(memory_writer, m) {
    py::class_<MemoryWriter>(m, "MemoryWriter")
        .def(py::init<>())
        .def("open_process", &MemoryWriter::open_process)
        .def("open_process_by_id", &MemoryWriter::open_process_by_id)
        .def("auto_open", &MemoryWriter::auto_open, py::arg("process_names") = py::none())
        .def("start", &MemoryWriter::start)
        .def("stop", &MemoryWriter::stop)
        .def("set_memory_data", &MemoryWriter::set_memory_data);
}
