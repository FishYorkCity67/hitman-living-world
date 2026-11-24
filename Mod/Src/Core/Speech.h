#pragma once

#include "Audio.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ISpVoice;

namespace lw {
    struct VoiceInfo {
        std::string m_Name;     // z. B. "Microsoft Stefan"
        std::string m_Language; // z. B. "407" (de-DE) oder "409" (en-US)
        bool m_Female = false;
        std::wstring m_TokenId;
        bool m_OneCore = false;

        bool IsGerman() const { return m_Language.find("407") != std::string::npos; }
        bool IsEnglish() const {
            return m_Language.find("409") != std::string::npos || m_Language.find("809") != std::string::npos;
        }
    };

    struct SpeechStyle {
        int m_Rate = 0;    // -10..10
        int m_Pitch = 0;   // -10..10
        int m_Volume = 100; // 0..100
    };

    // Windows-Sprachausgabe (SAPI 5, inkl. der neueren "OneCore"-Stimmen wie Katja/Stefan).
    // Nicht threadsicher: nur auf dem Thread benutzen, der Init() aufgerufen hat.
    class SapiTts {
    public:
        ~SapiTts();

        bool Init(std::string& p_Error);
        void Shutdown();

        std::vector<VoiceInfo> ListVoices();
        bool Synthesize(
            const std::string& p_TextUtf8, const std::wstring& p_TokenId, const SpeechStyle& p_Style,
            PcmBuffer& p_Out, std::string& p_Error
        );

    private:
        ISpVoice* m_Voice = nullptr;
        bool m_ComInitialized = false;
    };

    struct SpeakRequest {
        std::string m_Text;
        std::string m_VoiceName; // leer = automatisch nach Geschlecht/Sprache
        bool m_Female = false;
        bool m_German = true;
        SpeechStyle m_Style;
        uint64_t m_Tag = 0;       // z. B. NPC-Schluessel, fuer Positions-Updates
        float m_Volume = 1.f;
        float m_Pan = 0.f;
        int m_Generation = 0;
    };

    struct SpeechStarted {
        uint64_t m_Handle = 0;
        uint64_t m_Tag = 0;
        int m_Generation = 0;
        double m_Duration = 0.0;
    };

    // Eigener Thread: Text -> Sprache -> Lautsprecher. Das Spiel muss nie warten.
    class SpeechService {
    public:
        ~SpeechService();

        void Start();
        void Stop();
        bool IsReady() const { return m_Ready; }
        std::string LastError() const;

        void Speak(SpeakRequest p_Request);
        void CancelAll();
        bool IsBusyWith(uint64_t p_Tag);

        std::vector<VoiceInfo> Voices() const;
        bool TryPopStarted(SpeechStarted& p_Started);

        AudioOut& Audio() { return m_Audio; }

        // Waehlt die passende Stimme (Name > Sprache+Geschlecht > irgendeine).
        static const VoiceInfo* PickVoice(
            const std::vector<VoiceInfo>& p_Voices, const std::string& p_Name, bool p_Female, bool p_German
        );

    private:
        void WorkerLoop();

        AudioOut m_Audio;
        std::thread m_Worker;
        std::atomic<bool> m_Running = false;
        std::atomic<bool> m_Ready = false;

        mutable std::mutex m_Mutex;
        std::condition_variable m_Cv;
        std::deque<SpeakRequest> m_Queue;
        std::deque<SpeechStarted> m_Started;
        std::vector<VoiceInfo> m_Voices;
        std::string m_LastError;
        uint64_t m_CurrentTag = 0;
    };
}
