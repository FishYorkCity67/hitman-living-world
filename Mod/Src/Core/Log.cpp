#include "Log.h"

#include <Windows.h>

#include <share.h>

#include <atomic>
#include <cstdio>

namespace lw::log {
    namespace {
        std::mutex g_Mutex;
        FILE* g_File = nullptr;
        std::function<void(Level, const std::string&)> g_Forwarder;
        std::deque<Entry> g_Recent;
        std::atomic<bool> g_Debug = false;

        constexpr size_t c_MaxRecent = 400;

        const char* LevelName(Level p_Level) {
            switch (p_Level) {
                case Level::Debug: return "DEBUG";
                case Level::Info: return "INFO ";
                case Level::Warn: return "WARN ";
                case Level::Error: return "ERROR";
            }

            return "?";
        }

        std::string Timestamp() {
            SYSTEMTIME s_Time;
            GetLocalTime(&s_Time);

            char s_Buffer[32];
            snprintf(
                s_Buffer, sizeof(s_Buffer), "%02d:%02d:%02d.%03d",
                s_Time.wHour, s_Time.wMinute, s_Time.wSecond, s_Time.wMilliseconds
            );
            return s_Buffer;
        }
    }

    void Init(const std::wstring& p_FilePath) {
        std::scoped_lock s_Lock(g_Mutex);

        if (g_File) {
            fclose(g_File);
            g_File = nullptr;
        }

        // Neu anlegen bei jedem Spielstart, damit die Datei klein bleibt.
        // _SH_DENYNO: andere Programme duerfen mitlesen, waehrend das Spiel laeuft.
        g_File = _wfsopen(p_FilePath.c_str(), L"wb", _SH_DENYNO);

        if (g_File) {
            // UTF-8-BOM, damit Notepad die Umlaute richtig anzeigt.
            fwrite("\xEF\xBB\xBF", 1, 3, g_File);
            fflush(g_File);
        }
    }

    void Shutdown() {
        std::scoped_lock s_Lock(g_Mutex);

        if (g_File) {
            fclose(g_File);
            g_File = nullptr;
        }

        g_Forwarder = nullptr;
    }

    void SetForwarder(std::function<void(Level, const std::string&)> p_Forwarder) {
        std::scoped_lock s_Lock(g_Mutex);
        g_Forwarder = std::move(p_Forwarder);
    }

    void SetDebugEnabled(bool p_Enabled) {
        g_Debug = p_Enabled;
    }

    bool IsDebugEnabled() {
        return g_Debug;
    }

    void Write(Level p_Level, const std::string& p_Text) {
        std::function<void(Level, const std::string&)> s_Forwarder;

        {
            std::scoped_lock s_Lock(g_Mutex);
            const std::string s_Time = Timestamp();

            if (g_File) {
                fprintf(g_File, "[%s] %s %s\r\n", s_Time.c_str(), LevelName(p_Level), p_Text.c_str());
                fflush(g_File);
            }

            g_Recent.push_back({ p_Level, s_Time, p_Text });

            while (g_Recent.size() > c_MaxRecent) {
                g_Recent.pop_front();
            }

            s_Forwarder = g_Forwarder;
        }

        if (s_Forwarder) {
            s_Forwarder(p_Level, p_Text);
        }
    }

    std::deque<Entry> Recent() {
        std::scoped_lock s_Lock(g_Mutex);
        return g_Recent;
    }

    void ClearRecent() {
        std::scoped_lock s_Lock(g_Mutex);
        g_Recent.clear();
    }
}
