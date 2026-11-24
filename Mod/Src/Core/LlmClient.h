#pragma once

#include "Http.h"
#include "Settings.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lw {
    struct ChatMessage {
        std::string m_Role; // "system", "user", "assistant"
        std::string m_Content;
    };

    struct LlmConfig {
        LlmProvider m_Provider = LlmProvider::Ollama;
        std::string m_Url = "http://127.0.0.1:11434";
        std::string m_Model = "qwen2.5:3b";
        std::string m_ApiKey;
        float m_Temperature = 0.8f;
        int m_MaxTokens = 180;
        int m_ContextSize = 4096;
        int m_GpuLayers = -1;
        int m_Threads = 0;
        int m_KeepAliveMinutes = 30;
        int m_TimeoutSeconds = 60;
        bool m_AutoStartOllama = true;

        static LlmConfig FromSettings(const Settings& p_Settings);
    };

    struct LlmJob {
        uint64_t m_Id = 0;
        uint64_t m_Tag = 0;  // frei fuer den Aufrufer (z. B. NPC-Schluessel)
        int m_Kind = 0;      // frei fuer den Aufrufer (z. B. Anlass)
        int m_Generation = 0;
        int m_Priority = 0;  // hoeher = frueher
        std::vector<ChatMessage> m_Messages;
        nlohmann::ordered_json m_Schema; // leer = freier Text; Reihenfolge der Felder = Reihenfolge der Ausgabe
        std::string m_Extra;     // frei fuer den Aufrufer (z. B. was 47 gesagt hat)
        double m_Submitted = 0.0; // wird von Submit gesetzt
        double m_MaxWait = 0.0;   // > 0: verwerfen, wenn die Anfrage so lange in der Warteschlange lag (veraltet)
        bool m_ReplaceSameTag = false; // aeltere, noch wartende Anfragen mit gleichem Tag verwerfen

        // Gestreamte Antwort: liefert true, sobald der bisherige Text reicht, um schon loszulegen
        // (z. B. der gesprochene Satz ist komplett). Dann kommt vorab ein Ergebnis mit m_Partial = true.
        std::function<bool(const std::string& p_SoFar)> m_EarlyReady;
    };

    struct LlmResult {
        uint64_t m_Id = 0;
        uint64_t m_Tag = 0;
        int m_Kind = 0;
        int m_Generation = 0;
        bool m_Ok = false;
        bool m_Partial = false; // Vorab-Ergebnis einer gestreamten Antwort (das endgueltige folgt)
        bool m_Dropped = false; // veraltet, nie an die KI geschickt
        std::string m_Content;
        std::string m_Error;
        std::string m_Extra;
        double m_Seconds = 0.0;
        double m_WaitSeconds = 0.0;   // Zeit in der Warteschlange
        double m_PromptSeconds = 0.0; // davon: Prompt lesen (Ollama)
        double m_LoadSeconds = 0.0;   // davon: Modell laden (Ollama)
        int m_PromptTokens = 0;
        int m_OutputTokens = 0;
    };

    struct PullProgress {
        std::string m_Status;
        uint64_t m_Completed = 0;
        uint64_t m_Total = 0;
    };

    // Spricht mit einem lokalen Sprachmodell (Ollama oder OpenAI-kompatibel, z. B. LM Studio / llama.cpp).
    // Anfragen laufen nacheinander auf einem eigenen Thread, damit das Spiel nie wartet.
    class LlmClient {
    public:
        LlmClient() = default;
        ~LlmClient();

        LlmClient(const LlmClient&) = delete;
        LlmClient& operator=(const LlmClient&) = delete;

        void SetConfig(const LlmConfig& p_Config);
        LlmConfig GetConfig() const;

        void Start();
        void Stop();

        uint64_t Submit(LlmJob p_Job);
        bool TryPopResult(LlmResult& p_Result);
        void DropPending();
        size_t PendingCount() const;
        bool IsBusy() const { return m_Busy; }

        // Blockierende Hilfsfunktionen (nicht vom Spiel-Thread aufrufen).
        bool QueryVersion(std::string& p_Version, std::string& p_Error);
        bool QueryModels(std::vector<std::string>& p_Models, std::string& p_Error);
        bool PullModel(
            const std::string& p_Model, const std::function<void(const PullProgress&)>& p_Progress,
            http::CancelToken* p_Cancel, std::string& p_Error
        );
        bool EnsureServer(std::string& p_Error);

        // p_OnContent (optional): bei Ollama wird gestreamt und nach jedem Stueck mit dem bisherigen Text aufgerufen.
        LlmResult RunNow(
            const LlmJob& p_Job, http::CancelToken* p_Cancel = nullptr,
            const std::function<void(const std::string&)>& p_OnContent = {}
        );

        // Oeffentlich fuer Tests.
        static nlohmann::ordered_json BuildRequestBody(const LlmConfig& p_Config, const LlmJob& p_Job, bool p_Stream = false);
        static bool ExtractContent(
            const LlmConfig& p_Config, const std::string& p_Body, std::string& p_Content, std::string& p_Error,
            int& p_PromptTokens, int& p_OutputTokens
        );
        static std::string ChatEndpoint(const LlmConfig& p_Config);

        // Letzter Status fuer die Anzeige ("bereit", Fehlertext, ...).
        std::string LastStatus() const;
        double LastLatency() const { return m_LastLatency; }

    private:
        void WorkerLoop();
        bool StartOllamaProcess(std::string& p_Error);
        void SetStatus(std::string p_Status);

        mutable std::mutex m_ConfigMutex;
        LlmConfig m_Config;

        mutable std::mutex m_QueueMutex;
        std::condition_variable m_QueueCv;
        std::deque<LlmJob> m_Queue;
        std::deque<LlmResult> m_Results;
        uint64_t m_NextId = 1;

        std::thread m_Worker;
        std::atomic<bool> m_Running = false;
        std::atomic<bool> m_Busy = false;
        http::CancelToken m_Cancel;

        mutable std::mutex m_StatusMutex;
        std::string m_Status;
        std::atomic<double> m_LastLatency = 0.0;
        std::atomic<bool> m_ServerStartAttempted = false;
    };
}
