#include <cstdlib>
#include <signal.h>
#include <fstream>
#include <unistd.h>
#include <cstring>
#include <libgen.h>
#include "app.h"

#define PID_FILE "/var/run/mfoes.pid"

App* g_app_ptr = nullptr;
bool g_restart_requested = false;
int g_argc = 0;
char** g_argv = nullptr;

void signalHandler(int signal) {
    if (g_app_ptr) {
        switch(signal) {
            case SIGTERM:
            case SIGINT:
                g_app_ptr->stop();
                break;
            case SIGHUP:
                g_restart_requested = true;
                g_app_ptr->stop();
                break;
            default:
                break;
        }
    }
}

void writePidFile(const char* pidfile) {
    std::ofstream pf(pidfile);
    if (pf.is_open()) {
        pf << getpid() << std::endl;
        pf.close();
    }
}

void removePidFile(const char* pidfile) {
    unlink(pidfile);
}

void reexecBinary() {
    // Use /proc/self/exe as the executable path (Linux)
    // This ensures we always run the latest version from disk
    char exe_path[256];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    
    if (len == -1) {
        // Fallback: use argv[0] if /proc/self/exe is not available
        if (g_argv && g_argv[0]) {
            strncpy(exe_path, g_argv[0], sizeof(exe_path) - 1);
            exe_path[sizeof(exe_path) - 1] = '\0';
        } else {
            perror("Could not find executable path");
            return;
        }
    } else {
        exe_path[len] = '\0';
    }
    
    // Build new argv (add NULL terminator)
    char** new_argv = new char*[g_argc + 1];
    for (int i = 0; i < g_argc; i++) {
        new_argv[i] = g_argv[i];
    }
    new_argv[g_argc] = nullptr;
    
    // Replace current process image with new binary
    execve(exe_path, new_argv, nullptr);
    
    // If execve returns, it failed
    perror("execve failed");
    delete[] new_argv;
}

int main(int argc, char* argv[]) {
    // Store argc and argv for potential re-execution
    g_argc = argc;
    g_argv = argv;

    // Write PID file
    writePidFile(PID_FILE);

    // Register signal handlers
    signal(SIGINT, signalHandler);   // Ctrl+C - graceful shutdown
    signal(SIGTERM, signalHandler);  // termination signal - graceful shutdown
    signal(SIGHUP, signalHandler);   // hangup signal - restart

    int code = 0;
    
    // Main loop - allows for self-restart
    do {
        g_restart_requested = false;
        
        App app;
        g_app_ptr = &app;

        app.init();
        code = app.run();
        
        g_app_ptr = nullptr;
        
        // If SIGHUP was received, will re-execute the binary
        // Otherwise, exit the loop
        if (!g_restart_requested) {
            break;
        }
        
    } while (true);
    
    // Clean up PID file
    removePidFile(PID_FILE);

    // If restart was requested, re-execute the binary from disk
    if (g_restart_requested) {
        reexecBinary();
        // If reexecBinary succeeds, this code never executes
        // If it fails, we exit normally
    }

    return code;
}