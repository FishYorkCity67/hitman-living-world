#include "LlmClient.h"

#include "Log.h"
#include "Text.h"

#include <Windows.h>
#include <ShlObj.h>

#include <algorithm>
#include <chrono>

using nlohmann::json;

namespace lw {
    namespace {
        double NowSeconds() {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        std::string TrimSlash(std::string p_Url) {
            while (!p_Url.empty() && p_Url.back() == '/') {
                p_Url.pop_back();
            }

            return p_Url;
        }

        bool IsLocalUrl(const std::string& p_Url) {
            const std::string s_Lower = ToLowerAscii(p_Url);
            return s_Lower.find("://127.0.0.1") != std::string::npos ||
                s_Lower.find("://localhost") != std::string::npos ||
                s_Lower.find("://[::1]") != std::string::npos;
        }

        // Neuere "Denk"-Modelle wuerden sonst erst lange intern nachdenken – fuer Dialoge zu langsam.
        bool ModelSupportsThinking(const std::string& p_Model) {
            const std::string s_Lower = ToLowerAscii(p_Model);

            for (const char* s_Marker : { "qwen3", "deepseek-r1", "gpt-oss", "magistral", "think", "phi4-reasoning" }) {
                if (s_Lower.find(s_Marker) != std::string::npos) {
                    return true;
                }
            }

            return false;
        }

        std::string ErrorFromBody(const std::string& p_Body) {
            try {
                const json s_Json = json::parse(p_Body);

                if (s_Json.contains("error")) {
                    const auto& s_Error = s_Json["error"];

                    if (s_Error.is_string()) {
                        return s_Error.get<std::string>();
                    }

                    if (s_Error.is_object() && s_Error.contains("message")) {
                        return s_Error["message"].get<std::string>();
                    }
                }
            }
            catch (...) {}

            return TruncateUtf8(p_Body, 200);
        }
    }

    LlmConfig LlmConfig::FromSettings(const Settings& p_Settings) {
        LlmConfig s_Config;
        s_Config.m_Provider = p_Settings.m_LlmProvider;
        s_Config.m_Url = TrimSlash(p_Settings.m_LlmUrl);
        s_Config.m_Model = p_Settings.m_LlmModel;
        s_Config.m_ApiKey = p_Settings.m_LlmApiKey;
        s_Config.m_Temperature = p_Settings.m_Temperature;
        s_Config.m_MaxTokens = p_Settings.m_MaxTokens;
        s_Config.m_ContextSize = p_Settings.m_ContextSize;
        s_Config.m_GpuLayers = p_Settings.m_GpuLayers;
        s_Config.m_Threads = p_Settings.m_LlmThreads;
        s_Config.m_KeepAliveMinutes = p_Settings.m_KeepAliveMinutes;
        s_Config.m_TimeoutSeconds = p_Settings.m_TimeoutSeconds;
        s_Config.m_AutoStartOllama = p_Settings.m_AutoStartOllama;
        return s_Config;
    }

    LlmClient::~LlmClient() {
        Stop();
    }

    void LlmClient::SetConfig(const LlmConfig& p_Config) {
        std::scoped_lock s_Lock(m_ConfigMutex);
        const bool s_ServerChanged = p_Config.m_Url != m_Config.m_Url || p_Config.m_Provider != m_Config.m_Provider;
        m_Config = p_Config;

        if (s_ServerChanged) {
            m_ServerStartAttempted = false;
        }
    }

    LlmConfig LlmClient::GetConfig() const {
        std::scoped_lock s_Lock(m_ConfigMutex);
        return m_Config;
    }

    void LlmClient::Start() {
        if (m_Running) {
            return;
        }

        m_Cancel.Reset();
        m_Running = true;
        m_Worker = std::thread(&LlmClient::WorkerLoop, this);
    }

    void LlmClient::Stop() {
        if (!m_Running) {
            return;
        }

        {
            std::scoped_lock s_Lock(m_QueueMutex);
            m_Running = false;
            m_Queue.clear();
        }

        m_Cancel.Cancel();
        m_QueueCv.notify_all();

        if (m_Worker.joinable()) {
            m_Worker.join();
        }
    }

    uint64_t LlmClient::Submit(LlmJob p_Job) {
        std::scoped_lock s_Lock(m_QueueMutex);
        p_Job.m_Id = m_NextId++;
        p_Job.m_Submitted = NowSeconds();
        const uint64_t s_Id = p_Job.m_Id;

        // Neue Frage an denselben NPC: die alte, noch wartende ist ueberholt.
        if (p_Job.m_ReplaceSameTag) {
            for (auto s_It = m_Queue.begin(); s_It != m_Queue.end();) {
                if (s_It->m_Tag == p_Job.m_Tag) {
                    LlmResult s_Dropped;
                    s_Dropped.m_Id = s_It->m_Id;
                    s_Dropped.m_Tag = s_It->m_Tag;
                    s_Dropped.m_Kind = s_It->m_Kind;
                    s_Dropped.m_Generation = s_It->m_Generation;
                    s_Dropped.m_Dropped = true;
                    s_Dropped.m_Error = "ueberholt";
                    m_Results.push_back(std::move(s_Dropped));
                    s_It = m_Queue.erase(s_It);
                }
                else {
                    ++s_It;
                }
            }
        }

        // Nach Prioritaet einsortieren (stabil: gleiche Prioritaet bleibt in Reihenfolge).
        auto s_It = std::find_if(m_Queue.begin(), m_Queue.end(), [&](const LlmJob& p_Other) {
            return p_Other.m_Priority < p_Job.m_Priority;
        });

        m_Queue.insert(s_It, std::move(p_Job));
        m_QueueCv.notify_one();
        return s_Id;
    }

    bool LlmClient::TryPopResult(LlmResult& p_Result) {
        std::scoped_lock s_Lock(m_QueueMutex);

        if (m_Results.empty()) {
            return false;
        }

        p_Result = std::move(m_Results.front());
        m_Results.pop_front();
        return true;
    }

    void LlmClient::DropPending() {
        std::scoped_lock s_Lock(m_QueueMutex);
        m_Queue.clear();
        m_Results.clear();
    }

    size_t LlmClient::PendingCount() const {
        std::scoped_lock s_Lock(m_QueueMutex);
        return m_Queue.size() + (m_Busy ? 1 : 0);
    }

    std::string LlmClient::LastStatus() const {
        std::scoped_lock s_Lock(m_StatusMutex);
        return m_Status;
    }

    void LlmClient::SetStatus(std::string p_Status) {
        std::scoped_lock s_Lock(m_StatusMutex);
        m_Status = std::move(p_Status);
    }

    void LlmClient::WorkerLoop() {
        SetThreadDescription(GetCurrentThread(), L"LivingWorld LLM");

        while (true) {
            LlmJob s_Job;

            {
                std::unique_lock s_Lock(m_QueueMutex);
                m_QueueCv.wait(s_Lock, [this]() { return !m_Running || !m_Queue.empty(); });

                if (!m_Running) {
                    return;
                }

                s_Job = std::move(m_Queue.front());
                m_Queue.pop_front();

                // Zu lange gewartet (z. B. Reaktion eines Umstehenden): heute interessiert das niemanden mehr.
                const double s_Waited = NowSeconds() - s_Job.m_Submitted;

                if (s_Job.m_MaxWait > 0.0 && s_Waited > s_Job.m_MaxWait) {
                    LlmResult s_Dropped;
                    s_Dropped.m_Id = s_Job.m_Id;
                    s_Dropped.m_Tag = s_Job.m_Tag;
                    s_Dropped.m_Kind = s_Job.m_Kind;
                    s_Dropped.m_Generation = s_Job.m_Generation;
                    s_Dropped.m_Dropped = true;
                    s_Dropped.m_Error = "veraltet";
                    s_Dropped.m_WaitSeconds = s_Waited;
                    m_Results.push_back(std::move(s_Dropped));
                    continue;
                }

                m_Busy = true;
            }

            // Vorab-Ergebnis, sobald der gesprochene Teil der Antwort fertig ist – der Rest kommt danach.
            bool s_SentEarly = false;
            const double s_Started = NowSeconds();
            const auto s_OnContent = [&](const std::string& p_Content) {
                if (s_SentEarly || !s_Job.m_EarlyReady(p_Content)) {
                    return;
                }

                s_SentEarly = true;
                LlmResult s_Early;
                s_Early.m_Id = s_Job.m_Id;
                s_Early.m_Tag = s_Job.m_Tag;
                s_Early.m_Kind = s_Job.m_Kind;
                s_Early.m_Generation = s_Job.m_Generation;
                s_Early.m_Extra = s_Job.m_Extra;
                s_Early.m_Ok = true;
                s_Early.m_Partial = true;
                s_Early.m_Content = p_Content;
                s_Early.m_Seconds = NowSeconds() - s_Started;
                s_Early.m_WaitSeconds = s_Started - s_Job.m_Submitted;

                std::scoped_lock s_Lock(m_QueueMutex);
                m_Results.push_back(std::move(s_Early));
            };

            LlmResult s_Result = s_Job.m_EarlyReady ? RunNow(s_Job, &m_Cancel, s_OnContent) : RunNow(s_Job, &m_Cancel);
            s_Result.m_WaitSeconds = s_Started - s_Job.m_Submitted;

            {
                std::scoped_lock s_Lock(m_QueueMutex);
                m_Busy = false;

                if (!m_Running) {
                    return;
                }

                m_Results.push_back(std::move(s_Result));
            }
        }
    }

    std::string LlmClient::ChatEndpoint(const LlmConfig& p_Config) {
        const std::string s_Base = TrimSlash(p_Config.m_Url);

        if (p_Config.m_Provider == LlmProvider::Ollama) {
            return s_Base + "/api/chat";
        }

        if (s_Base.size() >= 3 && s_Base.compare(s_Base.size() - 3, 3, "/v1") == 0) {
            return s_Base + "/chat/completions";
        }

        return s_Base + "/v1/chat/completions";
    }

    // ordered_json: die Reihenfolge der Schema-Felder bestimmt, in welcher Reihenfolge das Modell sie schreibt.
    nlohmann::ordered_json LlmClient::BuildRequestBody(const LlmConfig& p_Config, const LlmJob& p_Job, bool p_Stream) {
        using ojson = nlohmann::ordered_json;
        ojson s_Messages = ojson::array();

        for (const auto& s_Message : p_Job.m_Messages) {
            s_Messages.push_back({ { "role", s_Message.m_Role }, { "content", s_Message.m_Content } });
        }

        ojson s_Body;
        s_Body["model"] = p_Config.m_Model;
        s_Body["messages"] = std::move(s_Messages);
        s_Body["stream"] = p_Stream && p_Config.m_Provider == LlmProvider::Ollama;

        if (p_Config.m_Provider == LlmProvider::Ollama) {
            ojson s_Options;
            s_Options["temperature"] = p_Config.m_Temperature;
            s_Options["num_predict"] = p_Config.m_MaxTokens;
            s_Options["num_ctx"] = p_Config.m_ContextSize;

            if (p_Config.m_GpuLayers >= 0) {
                s_Options["num_gpu"] = p_Config.m_GpuLayers;
            }

            if (p_Config.m_Threads > 0) {
                s_Options["num_thread"] = p_Config.m_Threads;
            }

            s_Body["options"] = std::move(s_Options);
            s_Body["keep_alive"] = std::to_string(p_Config.m_KeepAliveMinutes) + "m";

            if (!p_Job.m_Schema.is_null() && !p_Job.m_Schema.empty()) {
                s_Body["format"] = p_Job.m_Schema;
            }

            if (ModelSupportsThinking(p_Config.m_Model)) {
                s_Body["think"] = false;
            }
        }
        else {
            s_Body["temperature"] = p_Config.m_Temperature;
            s_Body["max_tokens"] = p_Config.m_MaxTokens;

            if (!p_Job.m_Schema.is_null() && !p_Job.m_Schema.empty()) {
                s_Body["response_format"] = {
                    { "type", "json_schema" },
                    { "json_schema", { { "name", "npc_reply" }, { "strict", false }, { "schema", p_Job.m_Schema } } }
                };
            }
        }

        return s_Body;
    }

    bool LlmClient::ExtractContent(
        const LlmConfig& p_Config, const std::string& p_Body, std::string& p_Content, std::string& p_Error,
        int& p_PromptTokens, int& p_OutputTokens
    ) {
        json s_Json;

        try {
            s_Json = json::parse(p_Body);
        }
        catch (const std::exception& e) {
            p_Error = std::string("Antwort ist kein JSON: ") + e.what();
            return false;
        }

        if (s_Json.contains("error")) {
            p_Error = ErrorFromBody(p_Body);
            return false;
        }

        try {
            if (p_Config.m_Provider == LlmProvider::Ollama) {
                p_Content = s_Json.at("message").at("content").get<std::string>();
                p_PromptTokens = s_Json.value("prompt_eval_count", 0);
                p_OutputTokens = s_Json.value("eval_count", 0);
            }
            else {
                p_Content = s_Json.at("choices").at(0).at("message").at("content").get<std::string>();

                if (s_Json.contains("usage")) {
                    p_PromptTokens = s_Json["usage"].value("prompt_tokens", 0);
                    p_OutputTokens = s_Json["usage"].value("completion_tokens", 0);
                }
            }
        }
        catch (const std::exception& e) {
            p_Error = std::string("Unerwartetes Antwortformat: ") + e.what();
            return false;
        }

        return true;
    }

    LlmResult LlmClient::RunNow(
        const LlmJob& p_Job, http::CancelToken* p_Cancel, const std::function<void(const std::string&)>& p_OnContent
    ) {
        const LlmConfig s_Config = GetConfig();
        const bool s_Stream = p_OnContent && s_Config.m_Provider == LlmProvider::Ollama;

        LlmResult s_Result;
        s_Result.m_Id = p_Job.m_Id;
        s_Result.m_Tag = p_Job.m_Tag;
        s_Result.m_Kind = p_Job.m_Kind;
        s_Result.m_Generation = p_Job.m_Generation;
        s_Result.m_Extra = p_Job.m_Extra;

        std::string s_ServerError;

        if (!EnsureServer(s_ServerError)) {
            s_Result.m_Error = s_ServerError;
            SetStatus(s_ServerError);
            return s_Result;
        }

        http::Request s_Request;
        s_Request.m_Method = "POST";
        s_Request.m_Url = ChatEndpoint(s_Config);
        s_Request.m_Body = BuildRequestBody(s_Config, p_Job, s_Stream).dump();
        s_Request.m_Headers.push_back({ "Content-Type", "application/json" });
        s_Request.m_TimeoutMs = s_Config.m_TimeoutSeconds * 1000;
        s_Request.m_Cancel = p_Cancel;

        if (!s_Config.m_ApiKey.empty()) {
            s_Request.m_Headers.push_back({ "Authorization", "Bearer " + s_Config.m_ApiKey });
        }

        // Gestreamt: Ollama schickt eine JSON-Zeile pro Stueck; die letzte (done) enthaelt die Messwerte.
        std::string s_Raw;
        std::string s_Pending;
        std::string s_Streamed;
        std::string s_StreamError;
        json s_Final;

        if (s_Stream) {
            s_Request.m_OnData = [&](const char* p_Data, size_t p_Size) {
                if (s_Raw.size() < 4096) {
                    s_Raw.append(p_Data, std::min<size_t>(p_Size, 4096 - s_Raw.size())); // fuer Fehlermeldungen
                }

                s_Pending.append(p_Data, p_Size);
                size_t s_LineEnd;
                bool s_Grew = false;

                while ((s_LineEnd = s_Pending.find('\n')) != std::string::npos) {
                    const std::string s_Line = s_Pending.substr(0, s_LineEnd);
                    s_Pending.erase(0, s_LineEnd + 1);

                    try {
                        json s_Json = json::parse(s_Line);

                        if (s_Json.contains("error")) {
                            s_StreamError = ErrorFromBody(s_Line);
                            return false;
                        }

                        if (s_Json.contains("message") && s_Json["message"].contains("content")) {
                            s_Streamed += s_Json["message"]["content"].get<std::string>();
                            s_Grew = true;
                        }

                        if (s_Json.value("done", false)) {
                            s_Final = std::move(s_Json);
                        }
                    }
                    catch (...) {}
                }

                if (s_Grew) {
                    p_OnContent(s_Streamed);
                }

                return true;
            };
        }

        const double s_Start = NowSeconds();
        http::Response s_Response = http::Send(s_Request);

        // Ollamas Rechenprozess stuerzt bei knappem Grafikspeicher gelegentlich ab (HTTP 500) –
        // er startet dann neu, ein zweiter Versuch klappt meist. (Nicht, wenn schon Text gestreamt wurde.)
        const bool s_Cancelled = p_Cancel && p_Cancel->IsCancelled();
        const bool s_ServerHiccup = s_Response.m_Status >= 500 ||
            (s_Response.m_Status == 0 && s_Response.m_Error.find("abgebrochen") == std::string::npos);

        if (s_ServerHiccup && !s_Cancelled && s_Streamed.empty() && NowSeconds() - s_Start < s_Config.m_TimeoutSeconds * 0.5) {
            log::Warn("KI-Server hatte einen Aussetzer ({}), zweiter Versuch ...",
                      s_Response.m_Error.empty() ? "HTTP " + std::to_string(s_Response.m_Status) : s_Response.m_Error);
            Sleep(500);
            s_Raw.clear();
            s_Pending.clear();
            s_StreamError.clear();
            s_Final = json();
            s_Response = http::Send(s_Request);
        }

        s_Result.m_Seconds = NowSeconds() - s_Start;

        if (!s_StreamError.empty()) {
            s_Result.m_Error = s_StreamError;
        }
        else if (!s_Response.m_Error.empty()) {
            s_Result.m_Error = s_Response.m_Error;
        }
        else if (s_Response.m_Status != 200) {
            s_Result.m_Error = "HTTP " + std::to_string(s_Response.m_Status) + ": " +
                ErrorFromBody(s_Stream ? s_Raw : s_Response.m_Body);
        }
        else if (s_Stream) {
            s_Result.m_Content = s_Streamed;

            if (s_Final.is_object()) {
                s_Result.m_PromptTokens = s_Final.value("prompt_eval_count", 0);
                s_Result.m_OutputTokens = s_Final.value("eval_count", 0);
                s_Result.m_PromptSeconds = s_Final.value("prompt_eval_duration", 0.0) / 1e9;
                s_Result.m_LoadSeconds = s_Final.value("load_duration", 0.0) / 1e9;
            }

            s_Result.m_Ok = !s_Streamed.empty();

            if (!s_Result.m_Ok) {
                s_Result.m_Error = "leere Antwort";
            }
        }
        else if (ExtractContent(
            s_Config, s_Response.m_Body, s_Result.m_Content, s_Result.m_Error, s_Result.m_PromptTokens,
            s_Result.m_OutputTokens
        )) {
            s_Result.m_Ok = true;

            if (s_Config.m_Provider == LlmProvider::Ollama) {
                try {
                    const json s_Json = json::parse(s_Response.m_Body);
                    s_Result.m_PromptSeconds = s_Json.value("prompt_eval_duration", 0.0) / 1e9;
                    s_Result.m_LoadSeconds = s_Json.value("load_duration", 0.0) / 1e9;
                }
                catch (...) {}
            }
        }

        if (s_Result.m_Ok) {
            m_LastLatency = s_Result.m_Seconds;
            SetStatus("bereit (" + s_Config.m_Model + ", letzte Antwort " + FormatSeconds(s_Result.m_Seconds) + ")");
        }
        else {
            SetStatus("Fehler: " + s_Result.m_Error);
            log::Warn("KI-Anfrage fehlgeschlagen: {}", s_Result.m_Error);
        }

        return s_Result;
    }

    bool LlmClient::QueryVersion(std::string& p_Version, std::string& p_Error) {
        const LlmConfig s_Config = GetConfig();

        http::Request s_Request;
        s_Request.m_TimeoutMs = 4000;

        if (s_Config.m_Provider == LlmProvider::Ollama) {
            s_Request.m_Url = TrimSlash(s_Config.m_Url) + "/api/version";
        }
        else {
            const std::string s_Base = TrimSlash(s_Config.m_Url);
            s_Request.m_Url = (s_Base.ends_with("/v1") ? s_Base : s_Base + "/v1") + "/models";

            if (!s_Config.m_ApiKey.empty()) {
                s_Request.m_Headers.push_back({ "Authorization", "Bearer " + s_Config.m_ApiKey });
            }
        }

        const http::Response s_Response = http::Send(s_Request);

        if (!s_Response.Ok()) {
            p_Error = s_Response.m_Error.empty() ? "HTTP " + std::to_string(s_Response.m_Status) : s_Response.m_Error;
            return false;
        }

        if (s_Config.m_Provider == LlmProvider::Ollama) {
            try {
                p_Version = json::parse(s_Response.m_Body).value("version", "?");
            }
            catch (...) {
                p_Version = "?";
            }
        }
        else {
            p_Version = "OpenAI-kompatibel";
        }

        return true;
    }

    bool LlmClient::QueryModels(std::vector<std::string>& p_Models, std::string& p_Error) {
        const LlmConfig s_Config = GetConfig();

        http::Request s_Request;
        s_Request.m_TimeoutMs = 5000;

        if (s_Config.m_Provider == LlmProvider::Ollama) {
            s_Request.m_Url = TrimSlash(s_Config.m_Url) + "/api/tags";
        }
        else {
            const std::string s_Base = TrimSlash(s_Config.m_Url);
            s_Request.m_Url = (s_Base.ends_with("/v1") ? s_Base : s_Base + "/v1") + "/models";

            if (!s_Config.m_ApiKey.empty()) {
                s_Request.m_Headers.push_back({ "Authorization", "Bearer " + s_Config.m_ApiKey });
            }
        }

        const http::Response s_Response = http::Send(s_Request);

        if (!s_Response.Ok()) {
            p_Error = s_Response.m_Error.empty() ? "HTTP " + std::to_string(s_Response.m_Status) : s_Response.m_Error;
            return false;
        }

        try {
            const json s_Json = json::parse(s_Response.m_Body);
            p_Models.clear();

            if (s_Config.m_Provider == LlmProvider::Ollama) {
                for (const auto& s_Model : s_Json.at("models")) {
                    p_Models.push_back(s_Model.value("name", s_Model.value("model", "")));
                }
            }
            else {
                for (const auto& s_Model : s_Json.at("data")) {
                    p_Models.push_back(s_Model.value("id", ""));
                }
            }
        }
        catch (const std::exception& e) {
            p_Error = std::string("Modell-Liste unlesbar: ") + e.what();
            return false;
        }

        std::sort(p_Models.begin(), p_Models.end());
        return true;
    }

    bool LlmClient::PullModel(
        const std::string& p_Model, const std::function<void(const PullProgress&)>& p_Progress,
        http::CancelToken* p_Cancel, std::string& p_Error
    ) {
        const LlmConfig s_Config = GetConfig();

        if (s_Config.m_Provider != LlmProvider::Ollama) {
            p_Error = "Herunterladen geht nur mit Ollama";
            return false;
        }

        if (!EnsureServer(p_Error)) {
            return false;
        }

        std::string s_Pending;
        std::string s_StreamError;
        bool s_Success = false;

        http::Request s_Request;
        s_Request.m_Method = "POST";
        s_Request.m_Url = TrimSlash(s_Config.m_Url) + "/api/pull";
        s_Request.m_Body = json { { "model", p_Model }, { "stream", true } }.dump();
        s_Request.m_Headers.push_back({ "Content-Type", "application/json" });
        s_Request.m_TimeoutMs = 120000;
        s_Request.m_Cancel = p_Cancel;
        s_Request.m_OnData = [&](const char* p_Data, size_t p_Size) {
            s_Pending.append(p_Data, p_Size);
            size_t s_LineEnd;

            // Ollama schickt eine JSON-Zeile pro Fortschrittsschritt.
            while ((s_LineEnd = s_Pending.find('\n')) != std::string::npos) {
                const std::string s_Line = s_Pending.substr(0, s_LineEnd);
                s_Pending.erase(0, s_LineEnd + 1);

                try {
                    const json s_Json = json::parse(s_Line);

                    if (s_Json.contains("error")) {
                        s_StreamError = s_Json["error"].get<std::string>();
                        return false;
                    }

                    PullProgress s_Progress;
                    s_Progress.m_Status = s_Json.value("status", "");
                    s_Progress.m_Completed = s_Json.value("completed", 0ull);
                    s_Progress.m_Total = s_Json.value("total", 0ull);

                    if (s_Progress.m_Status == "success") {
                        s_Success = true;
                    }

                    if (p_Progress) {
                        p_Progress(s_Progress);
                    }
                }
                catch (...) {}
            }

            return true;
        };

        const http::Response s_Response = http::Send(s_Request);

        if (!s_StreamError.empty()) {
            p_Error = s_StreamError;
            return false;
        }

        if (!s_Response.m_Error.empty()) {
            p_Error = s_Response.m_Error;
            return false;
        }

        if (!s_Success) {
            p_Error = "Download unvollstaendig (HTTP " + std::to_string(s_Response.m_Status) + ")";
            return false;
        }

        return true;
    }

    bool LlmClient::EnsureServer(std::string& p_Error) {
        std::string s_Version;

        if (QueryVersion(s_Version, p_Error)) {
            return true;
        }

        const LlmConfig s_Config = GetConfig();

        if (s_Config.m_Provider != LlmProvider::Ollama || !s_Config.m_AutoStartOllama || !IsLocalUrl(s_Config.m_Url)) {
            p_Error = "KI-Server nicht erreichbar (" + s_Config.m_Url + "): " + p_Error;
            return false;
        }

        // Nur einmal pro Sitzung automatisch starten, sonst haengt jede Anfrage im Startversuch.
        if (m_ServerStartAttempted.exchange(true)) {
            p_Error = "Ollama laeuft nicht (automatischer Start schon versucht): " + p_Error;
            return false;
        }

        log::Info("Ollama antwortet nicht – starte 'ollama serve' im Hintergrund.");
        SetStatus("starte Ollama ...");

        if (!StartOllamaProcess(p_Error)) {
            return false;
        }

        for (int i = 0; i < 40; ++i) {
            Sleep(500);

            if (m_Cancel.IsCancelled()) {
                p_Error = "abgebrochen";
                return false;
            }

            if (QueryVersion(s_Version, p_Error)) {
                log::Info("Ollama {} ist bereit.", s_Version);
                m_ServerStartAttempted = false;
                return true;
            }
        }

        p_Error = "Ollama wurde gestartet, antwortet aber nicht.";
        return false;
    }

    bool LlmClient::StartOllamaProcess(std::string& p_Error) {
        std::wstring s_Exe;
        wchar_t s_LocalAppData[MAX_PATH] = {};

        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, s_LocalAppData))) {
            const std::wstring s_Candidate = std::wstring(s_LocalAppData) + L"\\Programs\\Ollama\\ollama.exe";

            if (GetFileAttributesW(s_Candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
                s_Exe = s_Candidate;
            }
        }

        if (s_Exe.empty()) {
            wchar_t s_Found[MAX_PATH] = {};

            if (SearchPathW(nullptr, L"ollama.exe", nullptr, MAX_PATH, s_Found, nullptr) > 0) {
                s_Exe = s_Found;
            }
        }

        if (s_Exe.empty()) {
            p_Error = "Ollama ist nicht installiert (ollama.com) – oder einen anderen KI-Server eintragen.";
            return false;
        }

        std::wstring s_CommandLine = L"\"" + s_Exe + L"\" serve";
        STARTUPINFOW s_Startup {};
        s_Startup.cb = sizeof(s_Startup);
        PROCESS_INFORMATION s_Process {};

        if (!CreateProcessW(
            s_Exe.c_str(), s_CommandLine.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &s_Startup, &s_Process
        )) {
            p_Error = "Ollama-Start fehlgeschlagen (Fehler " + std::to_string(GetLastError()) + ")";
            return false;
        }

        CloseHandle(s_Process.hThread);
        CloseHandle(s_Process.hProcess);
        return true;
    }
}
