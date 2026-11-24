#include "Voice.h"

#include "Log.h"
#include "Text.h"

#include <Windows.h>
#include <mmeapi.h>
#include <mmsystem.h>

#include <whisper.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace lw {
    // -----------------------------------------------------------------------------------------------------------------
    // MicRecorder
    // -----------------------------------------------------------------------------------------------------------------

    struct MicRecorder::Block {
        WAVEHDR m_Header {};
        std::vector<int16_t> m_Data;
    };

    namespace {
        constexpr int c_BlockSamples = MicRecorder::c_SampleRate / 10; // 100 ms
        constexpr int c_BlockCount = 8;
        constexpr size_t c_MaxSamples = static_cast<size_t>(MicRecorder::c_SampleRate) * 30; // 30 s
    }

    MicRecorder::~MicRecorder() {
        Stop();
    }

    std::vector<std::string> MicRecorder::ListDevices() {
        std::vector<std::string> s_Devices;
        const UINT s_Count = waveInGetNumDevs();

        for (UINT i = 0; i < s_Count; ++i) {
            WAVEINCAPSW s_Caps {};

            if (waveInGetDevCapsW(i, &s_Caps, sizeof(s_Caps)) == MMSYSERR_NOERROR) {
                s_Devices.push_back(WideToUtf8(s_Caps.szPname));
            }
            else {
                s_Devices.push_back("Geraet " + std::to_string(i));
            }
        }

        return s_Devices;
    }

    bool MicRecorder::Start(int p_Device, std::string& p_Error) {
        if (m_Recording) {
            return true;
        }

        Stop();

        {
            std::scoped_lock s_Lock(m_Mutex);
            m_Samples.clear();
            m_Samples.reserve(MicRecorder::c_SampleRate * 8);
        }

        WAVEFORMATEX s_Format {};
        s_Format.wFormatTag = WAVE_FORMAT_PCM;
        s_Format.nChannels = 1;
        s_Format.nSamplesPerSec = c_SampleRate;
        s_Format.wBitsPerSample = 16;
        s_Format.nBlockAlign = 2;
        s_Format.nAvgBytesPerSec = c_SampleRate * 2;

        m_Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);

        HWAVEIN s_Handle = nullptr;
        const UINT s_DeviceId = p_Device < 0 ? WAVE_MAPPER : static_cast<UINT>(p_Device);
        const MMRESULT s_Result = waveInOpen(
            &s_Handle, s_DeviceId, &s_Format, reinterpret_cast<DWORD_PTR>(m_Event), 0, CALLBACK_EVENT
        );

        if (s_Result != MMSYSERR_NOERROR) {
            CloseHandle(m_Event);
            m_Event = nullptr;
            p_Error = s_Result == MMSYSERR_BADDEVICEID
                ? "Mikrofon nicht gefunden"
                : "Mikrofon laesst sich nicht oeffnen (Fehler " + std::to_string(s_Result) +
                ") – Windows-Datenschutz: Mikrofonzugriff fuer Desktop-Apps erlauben?";
            return false;
        }

        m_Handle = s_Handle;

        for (int i = 0; i < c_BlockCount; ++i) {
            auto* s_Block = new Block();
            s_Block->m_Data.resize(c_BlockSamples);
            s_Block->m_Header.lpData = reinterpret_cast<LPSTR>(s_Block->m_Data.data());
            s_Block->m_Header.dwBufferLength = static_cast<DWORD>(c_BlockSamples * sizeof(int16_t));

            waveInPrepareHeader(s_Handle, &s_Block->m_Header, sizeof(WAVEHDR));
            waveInAddBuffer(s_Handle, &s_Block->m_Header, sizeof(WAVEHDR));
            m_Blocks.push_back(s_Block);
        }

        m_Recording = true;
        m_Level = 0.f;

        if (waveInStart(s_Handle) != MMSYSERR_NOERROR) {
            m_Recording = false;
            p_Error = "Aufnahme konnte nicht starten";
            Stop();
            return false;
        }

        m_Thread = std::thread(&MicRecorder::CaptureLoop, this);
        return true;
    }

    void MicRecorder::CaptureLoop() {
        SetThreadDescription(GetCurrentThread(), L"LivingWorld Mic");

        while (true) {
            WaitForSingleObject(m_Event, 100);
            const bool s_StillRecording = m_Recording;

            for (Block* s_Block : m_Blocks) {
                if (!(s_Block->m_Header.dwFlags & WHDR_DONE)) {
                    continue;
                }

                const size_t s_Count = s_Block->m_Header.dwBytesRecorded / sizeof(int16_t);
                double s_Sum = 0.0;

                {
                    std::scoped_lock s_Lock(m_Mutex);

                    for (size_t i = 0; i < s_Count && m_Samples.size() < c_MaxSamples; ++i) {
                        const float s_Value = static_cast<float>(s_Block->m_Data[i]) / 32768.f;
                        m_Samples.push_back(s_Value);
                        s_Sum += static_cast<double>(s_Value) * s_Value;
                    }
                }

                if (s_Count > 0) {
                    m_Level = static_cast<float>(std::min(1.0, std::sqrt(s_Sum / s_Count) * 4.0));
                }

                s_Block->m_Header.dwFlags &= ~WHDR_DONE;
                s_Block->m_Header.dwBytesRecorded = 0;

                if (s_StillRecording) {
                    waveInAddBuffer(static_cast<HWAVEIN>(m_Handle), &s_Block->m_Header, sizeof(WAVEHDR));
                }
            }

            if (!s_StillRecording) {
                break;
            }
        }
    }

    std::vector<float> MicRecorder::Stop() {
        const bool s_WasRecording = m_Recording.exchange(false);

        if (m_Handle) {
            waveInReset(static_cast<HWAVEIN>(m_Handle)); // gibt alle Puffer als "fertig" zurueck

            if (m_Event) {
                SetEvent(m_Event);
            }
        }

        if (m_Thread.joinable()) {
            m_Thread.join();
        }

        if (m_Handle) {
            for (Block* s_Block : m_Blocks) {
                waveInUnprepareHeader(static_cast<HWAVEIN>(m_Handle), &s_Block->m_Header, sizeof(WAVEHDR));
            }

            waveInClose(static_cast<HWAVEIN>(m_Handle));
            m_Handle = nullptr;
        }

        for (Block* s_Block : m_Blocks) {
            delete s_Block;
        }

        m_Blocks.clear();

        if (m_Event) {
            CloseHandle(m_Event);
            m_Event = nullptr;
        }

        m_Level = 0.f;

        std::scoped_lock s_Lock(m_Mutex);

        if (!s_WasRecording) {
            return {};
        }

        return std::move(m_Samples);
    }

    double MicRecorder::Seconds() const {
        std::scoped_lock s_Lock(m_Mutex);
        return static_cast<double>(m_Samples.size()) / c_SampleRate;
    }

    float MeasureLoudnessDb(const std::vector<float>& p_Samples) {
        constexpr size_t s_Frame = MicRecorder::c_SampleRate / 50; // 20 ms

        if (p_Samples.size() < s_Frame) {
            return -90.f;
        }

        std::vector<float> s_Energies;

        for (size_t i = 0; i + s_Frame <= p_Samples.size(); i += s_Frame) {
            double s_Sum = 0.0;

            for (size_t j = 0; j < s_Frame; ++j) {
                s_Sum += static_cast<double>(p_Samples[i + j]) * p_Samples[i + j];
            }

            s_Energies.push_back(static_cast<float>(s_Sum / s_Frame));
        }

        // Nur die lautesten 30 % (das eigentliche Sprechen) zaehlen, Pausen nicht.
        std::sort(s_Energies.begin(), s_Energies.end(), std::greater<>());
        const size_t s_Take = std::max<size_t>(1, s_Energies.size() * 3 / 10);
        double s_Mean = 0.0;

        for (size_t i = 0; i < s_Take; ++i) {
            s_Mean += s_Energies[i];
        }

        s_Mean /= static_cast<double>(s_Take);
        return s_Mean > 1e-12 ? static_cast<float>(10.0 * std::log10(s_Mean)) : -90.f;
    }

    // -----------------------------------------------------------------------------------------------------------------
    // SpeechRecognizer (whisper.cpp)
    // -----------------------------------------------------------------------------------------------------------------

    namespace {
        void WhisperLog(ggml_log_level p_Level, const char* p_Text, void*) {
            if (p_Level == GGML_LOG_LEVEL_ERROR && p_Text) {
                log::Warn("whisper: {}", Trim(p_Text));
            }
        }

        // Typische Whisper-"Halluzinationen" bei Stille oder Rauschen.
        bool IsHallucination(const std::string& p_Text) {
            const std::string s_Lower = ToLowerUtf8(p_Text);

            for (const char* s_Pattern : {
                "untertitel", "amara.org", "vielen dank fürs zuschauen", "danke fürs zuschauen", "thanks for watching",
                "copyright", "www.", "swr 20", "zdf", "musik]", "[musik", "*musik*", "applaus"
            }) {
                if (s_Lower.find(s_Pattern) != std::string::npos) {
                    return true;
                }
            }

            return false;
        }
    }

    SpeechRecognizer::~SpeechRecognizer() {
        Unload();
    }

    bool SpeechRecognizer::Load(const std::wstring& p_ModelPath, std::string& p_Error) {
        if (m_Context && m_LoadedPath == p_ModelPath) {
            return true;
        }

        Unload();

        if (GetFileAttributesW(p_ModelPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            p_Error = "Sprachmodell fehlt: " + WideToUtf8(p_ModelPath) + " (in den Einstellungen herunterladen)";
            return false;
        }

        whisper_log_set(WhisperLog, nullptr);

        whisper_context_params s_Params = whisper_context_default_params();
        s_Params.use_gpu = false;
        s_Params.flash_attn = true;

        // whisper.cpp oeffnet Dateien mit schmalen Pfaden – bei Sonderzeichen den Umweg ueber den Speicher nehmen.
        char s_Narrow[MAX_PATH * 2] = {};
        BOOL s_Lossy = FALSE;
        WideCharToMultiByte(CP_ACP, 0, p_ModelPath.c_str(), -1, s_Narrow, sizeof(s_Narrow), nullptr, &s_Lossy);

        if (!s_Lossy) {
            m_Context = whisper_init_from_file_with_params(s_Narrow, s_Params);
        }
        else {
            FILE* s_File = nullptr;

            if (_wfopen_s(&s_File, p_ModelPath.c_str(), L"rb") == 0 && s_File) {
                _fseeki64(s_File, 0, SEEK_END);
                const auto s_Size = static_cast<size_t>(_ftelli64(s_File));
                _fseeki64(s_File, 0, SEEK_SET);
                std::vector<char> s_Data(s_Size);
                fread(s_Data.data(), 1, s_Size, s_File);
                fclose(s_File);
                m_Context = whisper_init_from_buffer_with_params(s_Data.data(), s_Data.size(), s_Params);
            }
        }

        if (!m_Context) {
            p_Error = "Sprachmodell konnte nicht geladen werden (Datei beschaedigt?)";
            return false;
        }

        m_LoadedPath = p_ModelPath;
        return true;
    }

    void SpeechRecognizer::Unload() {
        if (m_Context) {
            whisper_free(m_Context);
            m_Context = nullptr;
        }

        m_LoadedPath.clear();
    }

    bool SpeechRecognizer::Transcribe(
        const std::vector<float>& p_Samples, const std::string& p_Language, int p_Threads, std::string& p_Text,
        std::string& p_Error, const std::string& p_Prompt
    ) {
        p_Text.clear();

        if (!m_Context) {
            p_Error = "Sprachmodell nicht geladen";
            return false;
        }

        if (p_Samples.size() < MicRecorder::c_SampleRate * 4 / 10) {
            p_Error = "zu kurz";
            return false;
        }

        // Bei Stille erfindet Whisper gern Saetze ("Untertitel im Auftrag ...") – gar nicht erst versuchen.
        if (MeasureLoudnessDb(p_Samples) < -52.f) {
            p_Error = "nichts gehoert (Mikrofon zu leise?)";
            return false;
        }

        // Whisper arbeitet intern mit 30-s-Fenstern; sehr kurze Aufnahmen etwas auffuellen hilft der Erkennung.
        std::vector<float> s_Samples = p_Samples;

        if (s_Samples.size() < MicRecorder::c_SampleRate) {
            s_Samples.resize(MicRecorder::c_SampleRate, 0.f);
        }

        // Kurze Aufnahmen: nur so viel Audio-Kontext rechnen wie noetig (1 Einheit = 20 ms) – spart viel Zeit.
        const double s_Seconds = static_cast<double>(s_Samples.size()) / MicRecorder::c_SampleRate;
        const int s_AudioCtx = std::min(1500, std::max(384, static_cast<int>(s_Seconds * 50.0) + 96));

        whisper_full_params s_Params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        s_Params.audio_ctx = s_AudioCtx >= 1500 ? 0 : s_AudioCtx;
        s_Params.n_threads = std::max(1, p_Threads);
        s_Params.language = p_Language.empty() ? "auto" : p_Language.c_str();
        s_Params.translate = false;
        s_Params.no_context = true;
        s_Params.no_timestamps = true;
        s_Params.single_segment = false;
        s_Params.print_progress = false;
        s_Params.print_realtime = false;
        s_Params.print_timestamps = false;
        s_Params.print_special = false;
        s_Params.suppress_blank = true;
        s_Params.suppress_nst = true;
        s_Params.temperature = 0.f;

        if (!p_Prompt.empty()) {
            s_Params.initial_prompt = p_Prompt.c_str();
        }

        if (whisper_full(m_Context, s_Params, s_Samples.data(), static_cast<int>(s_Samples.size())) != 0) {
            p_Error = "Erkennung fehlgeschlagen";
            return false;
        }

        std::string s_Text;
        const int s_Segments = whisper_full_n_segments(m_Context);

        for (int i = 0; i < s_Segments; ++i) {
            // Abschnitte, die laut Modell eher kein Sprechen sind, verwerfen.
            if (whisper_full_get_segment_no_speech_prob(m_Context, i) > 0.65f) {
                continue;
            }

            if (const char* s_Segment = whisper_full_get_segment_text(m_Context, i)) {
                s_Text += s_Segment;
            }
        }

        s_Text = CleanSpokenText(s_Text);

        if (s_Text.empty() || IsHallucination(s_Text)) {
            p_Error = "nichts verstanden";
            return false;
        }

        p_Text = s_Text;
        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------
    // TranscriptionService
    // -----------------------------------------------------------------------------------------------------------------

    TranscriptionService::~TranscriptionService() {
        Stop();
    }

    void TranscriptionService::Start() {
        if (m_Running) {
            return;
        }

        m_Running = true;
        m_Worker = std::thread(&TranscriptionService::WorkerLoop, this);
    }

    void TranscriptionService::Stop() {
        if (!m_Running) {
            return;
        }

        {
            std::scoped_lock s_Lock(m_Mutex);
            m_Running = false;
            m_Queue.clear();
        }

        m_Cv.notify_all();

        if (m_Worker.joinable()) {
            m_Worker.join();
        }
    }

    void TranscriptionService::SetModelPath(const std::wstring& p_Path) {
        std::scoped_lock s_Lock(m_Mutex);
        m_ModelPath = p_Path;
    }

    std::wstring TranscriptionService::ModelPath() const {
        std::scoped_lock s_Lock(m_Mutex);
        return m_ModelPath;
    }

    void TranscriptionService::Submit(TranscriptJob p_Job) {
        {
            std::scoped_lock s_Lock(m_Mutex);
            m_Queue.push_back(std::move(p_Job));
        }

        m_Cv.notify_one();
    }

    bool TranscriptionService::TryPopResult(TranscriptResult& p_Result) {
        std::scoped_lock s_Lock(m_Mutex);

        if (m_Results.empty()) {
            return false;
        }

        p_Result = std::move(m_Results.front());
        m_Results.pop_front();
        return true;
    }

    std::string TranscriptionService::Status() const {
        std::scoped_lock s_Lock(m_Mutex);
        return m_Status;
    }

    void TranscriptionService::Preload() {
        m_PreloadRequested = true;
        m_Cv.notify_one();
    }

    void TranscriptionService::UnloadModel() {
        m_UnloadRequested = true;
        m_Cv.notify_one();
    }

    void TranscriptionService::WorkerLoop() {
        SetThreadDescription(GetCurrentThread(), L"LivingWorld STT");

        // Unter dem Spiel einordnen, damit die Erkennung keine Ruckler verursacht.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

        const auto s_EnsureModel = [this](std::string& p_Error) {
            const std::wstring s_Path = ModelPath();

            {
                std::scoped_lock s_Lock(m_Mutex);
                m_Status = "lade Sprachmodell ...";
            }

            const bool s_Ok = m_Recognizer.Load(s_Path, p_Error);
            m_ModelLoaded = s_Ok;

            std::scoped_lock s_Lock(m_Mutex);
            m_Status = s_Ok ? "bereit" : p_Error;
            return s_Ok;
        };

        while (true) {
            TranscriptJob s_Job;
            bool s_HaveJob = false;

            {
                std::unique_lock s_Lock(m_Mutex);
                m_Cv.wait(s_Lock, [this]() {
                    return !m_Running || !m_Queue.empty() || m_PreloadRequested || m_UnloadRequested;
                });

                if (!m_Running) {
                    break;
                }

                if (!m_Queue.empty()) {
                    s_Job = std::move(m_Queue.front());
                    m_Queue.pop_front();
                    s_HaveJob = true;
                }
            }

            if (m_UnloadRequested.exchange(false)) {
                m_Recognizer.Unload();
                m_ModelLoaded = false;

                std::scoped_lock s_Lock(m_Mutex);
                m_Status = "Modell entladen";
            }

            if (m_PreloadRequested.exchange(false) && !m_ModelLoaded) {
                std::string s_Error;
                s_EnsureModel(s_Error);
            }

            if (!s_HaveJob) {
                continue;
            }

            m_Busy = true;

            TranscriptResult s_Result;
            s_Result.m_LoudnessDb = s_Job.m_LoudnessDb;
            s_Result.m_Generation = s_Job.m_Generation;

            const auto s_Start = std::chrono::steady_clock::now();

            if (s_EnsureModel(s_Result.m_Error)) {
                s_Result.m_Ok = m_Recognizer.Transcribe(
                    s_Job.m_Samples, s_Job.m_Language, s_Job.m_Threads, s_Result.m_Text, s_Result.m_Error, s_Job.m_Prompt
                );
            }

            s_Result.m_Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_Start).count();

            {
                std::scoped_lock s_Lock(m_Mutex);
                m_Results.push_back(std::move(s_Result));
            }

            m_Busy = false;
        }

        m_Recognizer.Unload();
        m_ModelLoaded = false;
    }

    const std::vector<WhisperModelInfo>& KnownWhisperModels() {
        static const std::vector<WhisperModelInfo> s_Models = {
            { "ggml-base-q5_1.bin", "Schnell (base) – ok fuer kurze Saetze", 60 },
            { "ggml-small-q5_1.bin", "Ausgewogen (small) – empfohlen", 190 },
            { "ggml-medium-q5_0.bin", "Genau, aber langsam (medium)", 540 },
            { "ggml-large-v3-turbo-q5_0.bin", "Sehr genau, braucht starke CPU (large-v3-turbo)", 575 },
        };

        return s_Models;
    }

    std::string WhisperModelUrl(const std::string& p_File) {
        return "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/" + p_File;
    }
}
