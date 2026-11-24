#pragma once

#include "Http.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct whisper_context;

namespace lw {
    // Mikrofonaufnahme (16 kHz, mono) ueber die klassische waveIn-API – laeuft mit jedem Windows-Mikrofon.
    class MicRecorder {
    public:
        ~MicRecorder();

        static std::vector<std::string> ListDevices();

        bool Start(int p_Device, std::string& p_Error);
        // Beendet die Aufnahme und liefert die Samples (-1..1).
        std::vector<float> Stop();
        bool IsRecording() const { return m_Recording; }

        // Pegel (0..1) des zuletzt aufgenommenen Blocks, fuer die Anzeige.
        float Level() const { return m_Level; }
        double Seconds() const;

        static constexpr int c_SampleRate = 16000;

    private:
        void CaptureLoop();

        void* m_Handle = nullptr; // HWAVEIN
        void* m_Event = nullptr;  // HANDLE
        std::thread m_Thread;
        std::atomic<bool> m_Recording = false;
        std::atomic<float> m_Level = 0.f;

        mutable std::mutex m_Mutex;
        std::vector<float> m_Samples;

        struct Block;
        std::vector<Block*> m_Blocks;
    };

    // Lautheit einer Aufnahme in dBFS (RMS ueber die lauteren Abschnitte).
    float MeasureLoudnessDb(const std::vector<float>& p_Samples);

    // Lokale Spracherkennung mit whisper.cpp.
    class SpeechRecognizer {
    public:
        ~SpeechRecognizer();

        bool Load(const std::wstring& p_ModelPath, std::string& p_Error);
        void Unload();
        bool IsLoaded() const { return m_Context != nullptr; }

        // p_Language: "de", "en" oder "auto".
        // p_Prompt: optionale Hinweise (z. B. Namen), damit Whisper sie richtig schreibt.
        bool Transcribe(
            const std::vector<float>& p_Samples, const std::string& p_Language, int p_Threads, std::string& p_Text,
            std::string& p_Error, const std::string& p_Prompt = {}
        );

    private:
        whisper_context* m_Context = nullptr;
        std::wstring m_LoadedPath;
    };

    struct TranscriptJob {
        std::vector<float> m_Samples;
        std::string m_Language;
        int m_Threads = 4;
        float m_LoudnessDb = -30.f;
        int m_Generation = 0;
        std::string m_Prompt;
    };

    struct TranscriptResult {
        bool m_Ok = false;
        std::string m_Text;
        std::string m_Error;
        float m_LoudnessDb = -30.f;
        double m_Seconds = 0.0;
        int m_Generation = 0;
    };

    // Erkennung im Hintergrund (Modell wird beim ersten Gebrauch geladen).
    class TranscriptionService {
    public:
        ~TranscriptionService();

        void Start();
        void Stop();

        void SetModelPath(const std::wstring& p_Path);
        std::wstring ModelPath() const;

        void Submit(TranscriptJob p_Job);
        bool TryPopResult(TranscriptResult& p_Result);
        bool IsBusy() const { return m_Busy; }
        bool IsModelLoaded() const { return m_ModelLoaded; }
        std::string Status() const;

        // Laedt das Modell schon vorab (z. B. beim Missionsstart), damit die erste Erkennung schnell ist.
        void Preload();
        void UnloadModel();

    private:
        void WorkerLoop();

        SpeechRecognizer m_Recognizer;
        std::thread m_Worker;
        std::atomic<bool> m_Running = false;
        std::atomic<bool> m_Busy = false;
        std::atomic<bool> m_ModelLoaded = false;
        std::atomic<bool> m_PreloadRequested = false;
        std::atomic<bool> m_UnloadRequested = false;

        mutable std::mutex m_Mutex;
        std::condition_variable m_Cv;
        std::deque<TranscriptJob> m_Queue;
        std::deque<TranscriptResult> m_Results;
        std::wstring m_ModelPath;
        std::string m_Status;
    };

    struct WhisperModelInfo {
        const char* m_File;
        const char* m_Label;
        int m_SizeMb;
    };

    const std::vector<WhisperModelInfo>& KnownWhisperModels();
    std::string WhisperModelUrl(const std::string& p_File);
}
