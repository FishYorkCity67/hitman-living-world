#pragma once

#include <deque>
#include <format>
#include <functional>
#include <mutex>
#include <string>

namespace lw::log {
    enum class Level { Debug, Info, Warn, Error };

    struct Entry {
        Level m_Level;
        std::string m_Time;
        std::string m_Text;
    };

    // Schreibt zusaetzlich in eine eigene Datei (LivingWorld.log), damit Fehler auch ohne Konsole nachvollziehbar sind.
    void Init(const std::wstring& p_FilePath);
    void Shutdown();

    // Optionaler Weiterleiter (z. B. in die SDK-Konsole).
    void SetForwarder(std::function<void(Level, const std::string&)> p_Forwarder);

    void SetDebugEnabled(bool p_Enabled);
    bool IsDebugEnabled();

    void Write(Level p_Level, const std::string& p_Text);

    // Die letzten Eintraege fuer die Protokoll-Ansicht im Spiel.
    std::deque<Entry> Recent();
    void ClearRecent();

    template <typename... Args>
    void Debug(std::format_string<Args...> p_Format, Args&&... p_Args) {
        if (IsDebugEnabled()) {
            Write(Level::Debug, std::format(p_Format, std::forward<Args>(p_Args)...));
        }
    }

    template <typename... Args>
    void Info(std::format_string<Args...> p_Format, Args&&... p_Args) {
        Write(Level::Info, std::format(p_Format, std::forward<Args>(p_Args)...));
    }

    template <typename... Args>
    void Warn(std::format_string<Args...> p_Format, Args&&... p_Args) {
        Write(Level::Warn, std::format(p_Format, std::forward<Args>(p_Args)...));
    }

    template <typename... Args>
    void Error(std::format_string<Args...> p_Format, Args&&... p_Args) {
        Write(Level::Error, std::format(p_Format, std::forward<Args>(p_Args)...));
    }
}
