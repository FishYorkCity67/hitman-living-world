#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace lw::http {
    // Erlaubt es, eine laufende Anfrage von einem anderen Thread aus sofort abzubrechen
    // (z. B. beim Entladen der Mod, waehrend das Sprachmodell noch rechnet).
    class CancelToken {
    public:
        void Cancel();
        bool IsCancelled() const { return m_Cancelled; }
        void Reset() { m_Cancelled = false; }

        // Intern: aktuelles WinHTTP-Handle merken, damit Cancel() es schliessen kann.
        bool Attach(void* p_Handle);

        // true = Handle gehoert wieder dem Aufrufer (muss es schliessen); false = Cancel() hat es schon geschlossen.
        bool Detach();

    private:
        std::atomic<bool> m_Cancelled = false;
        std::mutex m_Mutex;
        void* m_Handle = nullptr;
    };

    struct Request {
        std::string m_Method = "GET";
        std::string m_Url;
        std::string m_Body;
        std::vector<std::pair<std::string, std::string>> m_Headers;
        int m_TimeoutMs = 30000;
        CancelToken* m_Cancel = nullptr;

        // Optional: Daten stueckweise verarbeiten (Rueckgabe false = abbrechen). Ohne Callback landet alles in m_Body.
        std::function<bool(const char* p_Data, size_t p_Size)> m_OnData;
        std::function<void(uint64_t p_Received, uint64_t p_Total)> m_OnProgress;
    };

    struct Response {
        int m_Status = 0;
        std::string m_Body;
        std::string m_Error;

        bool Ok() const { return m_Error.empty() && m_Status >= 200 && m_Status < 300; }
    };

    Response Send(const Request& p_Request);

    // Laedt eine Datei herunter (folgt Weiterleitungen). Schreibt erst in <Ziel>.part und benennt dann um.
    bool DownloadToFile(
        const std::string& p_Url,
        const std::wstring& p_TargetPath,
        const std::function<void(uint64_t, uint64_t)>& p_Progress,
        CancelToken* p_Cancel,
        std::string& p_Error
    );
}
