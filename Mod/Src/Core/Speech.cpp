#include "Speech.h"

#include "Log.h"
#include "Text.h"

#include <Windows.h>
#include <objbase.h>
#include <sapi.h>

#include <algorithm>

namespace lw {
    namespace {
        template <typename T>
        struct ComPtr {
            T* m_Ptr = nullptr;

            ~ComPtr() { Reset(); }
            void Reset() {
                if (m_Ptr) {
                    m_Ptr->Release();
                    m_Ptr = nullptr;
                }
            }

            T** Out() {
                Reset();
                return &m_Ptr;
            }

            T* operator->() const { return m_Ptr; }
            explicit operator bool() const { return m_Ptr != nullptr; }
        };

        std::string ReadTokenString(ISpDataKey* p_Key, const wchar_t* p_Name) {
            wchar_t* s_Value = nullptr;

            if (SUCCEEDED(p_Key->GetStringValue(p_Name, &s_Value)) && s_Value) {
                std::string s_Result = WideToUtf8(s_Value);
                CoTaskMemFree(s_Value);
                return s_Result;
            }

            return {};
        }

        void EnumerateCategory(const wchar_t* p_CategoryId, bool p_OneCore, std::vector<VoiceInfo>& p_Out) {
            ComPtr<ISpObjectTokenCategory> s_Category;

            if (FAILED(CoCreateInstance(
                CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL, IID_ISpObjectTokenCategory,
                reinterpret_cast<void**>(s_Category.Out())
            ))) {
                return;
            }

            if (FAILED(s_Category->SetId(p_CategoryId, FALSE))) {
                return;
            }

            ComPtr<IEnumSpObjectTokens> s_Enum;

            if (FAILED(s_Category->EnumTokens(nullptr, nullptr, s_Enum.Out())) || !s_Enum) {
                return;
            }

            ULONG s_Count = 0;
            s_Enum->GetCount(&s_Count);

            for (ULONG i = 0; i < s_Count; ++i) {
                ComPtr<ISpObjectToken> s_Token;

                if (FAILED(s_Enum->Item(i, s_Token.Out())) || !s_Token) {
                    continue;
                }

                VoiceInfo s_Info;
                s_Info.m_OneCore = p_OneCore;

                wchar_t* s_Id = nullptr;

                if (SUCCEEDED(s_Token->GetId(&s_Id)) && s_Id) {
                    s_Info.m_TokenId = s_Id;
                    CoTaskMemFree(s_Id);
                }

                ComPtr<ISpDataKey> s_Attributes;

                if (SUCCEEDED(s_Token->OpenKey(L"Attributes", s_Attributes.Out())) && s_Attributes) {
                    s_Info.m_Name = ReadTokenString(s_Attributes.m_Ptr, L"Name");
                    s_Info.m_Language = ReadTokenString(s_Attributes.m_Ptr, L"Language");
                    s_Info.m_Female = ToLowerAscii(ReadTokenString(s_Attributes.m_Ptr, L"Gender")) == "female";
                }

                if (s_Info.m_Name.empty()) {
                    s_Info.m_Name = ReadTokenString(s_Token.m_Ptr, nullptr);
                }

                // Dieselbe Stimme gibt es oft in beiden Kategorien – nur einmal aufnehmen.
                const bool s_Duplicate = std::any_of(p_Out.begin(), p_Out.end(), [&](const VoiceInfo& p_Other) {
                    return p_Other.m_Name == s_Info.m_Name && p_Other.m_Language == s_Info.m_Language;
                });

                if (!s_Duplicate && !s_Info.m_TokenId.empty()) {
                    p_Out.push_back(std::move(s_Info));
                }
            }
        }

        std::wstring EscapeXml(const std::wstring& p_Text) {
            std::wstring s_Result;
            s_Result.reserve(p_Text.size() + 16);

            for (const wchar_t c : p_Text) {
                switch (c) {
                    case L'&': s_Result += L"&amp;"; break;
                    case L'<': s_Result += L"&lt;"; break;
                    case L'>': s_Result += L"&gt;"; break;
                    default: s_Result.push_back(c); break;
                }
            }

            return s_Result;
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // SapiTts
    // -----------------------------------------------------------------------------------------------------------------

    SapiTts::~SapiTts() {
        Shutdown();
    }

    bool SapiTts::Init(std::string& p_Error) {
        if (m_Voice) {
            return true;
        }

        const HRESULT s_Com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_ComInitialized = SUCCEEDED(s_Com);

        const HRESULT s_Result = CoCreateInstance(
            CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, reinterpret_cast<void**>(&m_Voice)
        );

        if (FAILED(s_Result) || !m_Voice) {
            p_Error = "Windows-Sprachausgabe nicht verfuegbar (SAPI)";
            m_Voice = nullptr;
            return false;
        }

        return true;
    }

    void SapiTts::Shutdown() {
        if (m_Voice) {
            m_Voice->Release();
            m_Voice = nullptr;
        }

        if (m_ComInitialized) {
            CoUninitialize();
            m_ComInitialized = false;
        }
    }

    std::vector<VoiceInfo> SapiTts::ListVoices() {
        std::vector<VoiceInfo> s_Voices;

        // Die neueren Stimmen (Katja, Stefan, ...) stehen nur in der OneCore-Kategorie.
        EnumerateCategory(L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech_OneCore\\Voices", true, s_Voices);
        EnumerateCategory(SPCAT_VOICES, false, s_Voices);
        return s_Voices;
    }

    bool SapiTts::Synthesize(
        const std::string& p_TextUtf8, const std::wstring& p_TokenId, const SpeechStyle& p_Style, PcmBuffer& p_Out,
        std::string& p_Error
    ) {
        if (!m_Voice) {
            p_Error = "SAPI nicht initialisiert";
            return false;
        }

        if (!p_TokenId.empty()) {
            ComPtr<ISpObjectToken> s_Token;

            if (SUCCEEDED(CoCreateInstance(
                CLSID_SpObjectToken, nullptr, CLSCTX_ALL, IID_ISpObjectToken, reinterpret_cast<void**>(s_Token.Out())
            )) && SUCCEEDED(s_Token->SetId(nullptr, p_TokenId.c_str(), FALSE))) {
                m_Voice->SetVoice(s_Token.m_Ptr);
            }
        }

        ComPtr<IStream> s_Memory;

        if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, s_Memory.Out()))) {
            p_Error = "Speicherstream fehlgeschlagen";
            return false;
        }

        ComPtr<ISpStream> s_SpStream;

        if (FAILED(CoCreateInstance(
            CLSID_SpStream, nullptr, CLSCTX_ALL, IID_ISpStream, reinterpret_cast<void**>(s_SpStream.Out())
        ))) {
            p_Error = "SpStream fehlgeschlagen";
            return false;
        }

        WAVEFORMATEX s_Format {};
        s_Format.wFormatTag = WAVE_FORMAT_PCM;
        s_Format.nChannels = 1;
        s_Format.nSamplesPerSec = 22050;
        s_Format.wBitsPerSample = 16;
        s_Format.nBlockAlign = 2;
        s_Format.nAvgBytesPerSec = 22050 * 2;

        if (FAILED(s_SpStream->SetBaseStream(s_Memory.m_Ptr, SPDFID_WaveFormatEx, &s_Format))) {
            p_Error = "SetBaseStream fehlgeschlagen";
            return false;
        }

        if (FAILED(m_Voice->SetOutput(s_SpStream.m_Ptr, TRUE))) {
            p_Error = "SetOutput fehlgeschlagen";
            return false;
        }

        const int s_Rate = std::clamp(p_Style.m_Rate, -10, 10);
        const int s_Pitch = std::clamp(p_Style.m_Pitch, -10, 10);
        const int s_Volume = std::clamp(p_Style.m_Volume, 0, 100);

        const std::wstring s_Xml =
            L"<volume level=\"" + std::to_wstring(s_Volume) + L"\"><rate absspeed=\"" + std::to_wstring(s_Rate) +
            L"\"><pitch absmiddle=\"" + std::to_wstring(s_Pitch) + L"\">" + EscapeXml(Utf8ToWide(p_TextUtf8)) +
            L"</pitch></rate></volume>";

        const HRESULT s_Spoken = m_Voice->Speak(s_Xml.c_str(), SPF_IS_XML | SPF_PURGEBEFORESPEAK, nullptr);

        // Ausgabe wieder loesen, damit der Stream freigegeben werden kann.
        m_Voice->SetOutput(nullptr, TRUE);

        if (FAILED(s_Spoken)) {
            p_Error = "Sprachausgabe fehlgeschlagen (0x" + std::to_string(static_cast<unsigned>(s_Spoken)) + ")";
            return false;
        }

        HGLOBAL s_Global = nullptr;

        if (FAILED(GetHGlobalFromStream(s_Memory.m_Ptr, &s_Global)) || !s_Global) {
            p_Error = "Audiodaten nicht lesbar";
            return false;
        }

        STATSTG s_Stat {};
        s_Memory->Stat(&s_Stat, STATFLAG_NONAME);
        const size_t s_Bytes = static_cast<size_t>(s_Stat.cbSize.QuadPart);

        const void* s_Data = GlobalLock(s_Global);

        if (!s_Data) {
            p_Error = "GlobalLock fehlgeschlagen";
            return false;
        }

        p_Out.m_SampleRate = s_Format.nSamplesPerSec;
        p_Out.m_Samples.resize(s_Bytes / 2);
        memcpy(p_Out.m_Samples.data(), s_Data, p_Out.m_Samples.size() * 2);
        GlobalUnlock(s_Global);

        TightenSilence(p_Out);
        return !p_Out.m_Samples.empty();
    }

    // -----------------------------------------------------------------------------------------------------------------
    // SpeechService
    // -----------------------------------------------------------------------------------------------------------------

    SpeechService::~SpeechService() {
        Stop();
    }

    void SpeechService::Start() {
        if (m_Running) {
            return;
        }

        m_Running = true;
        m_Worker = std::thread(&SpeechService::WorkerLoop, this);
    }

    void SpeechService::Stop() {
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

    std::string SpeechService::LastError() const {
        std::scoped_lock s_Lock(m_Mutex);
        return m_LastError;
    }

    void SpeechService::Speak(SpeakRequest p_Request) {
        {
            std::scoped_lock s_Lock(m_Mutex);

            // Nicht endlos stauen: wenn sehr viel wartet, die aeltesten verwerfen.
            while (m_Queue.size() >= 6) {
                m_Queue.pop_front();
            }

            m_Queue.push_back(std::move(p_Request));
        }

        m_Cv.notify_one();
    }

    void SpeechService::CancelAll() {
        {
            std::scoped_lock s_Lock(m_Mutex);
            m_Queue.clear();
            m_Started.clear();
        }

        m_Audio.StopAll();
    }

    bool SpeechService::IsBusyWith(uint64_t p_Tag) {
        {
            std::scoped_lock s_Lock(m_Mutex);

            if (m_CurrentTag == p_Tag) {
                return true;
            }

            for (const auto& s_Request : m_Queue) {
                if (s_Request.m_Tag == p_Tag) {
                    return true;
                }
            }
        }

        return m_Audio.IsAnyPlayingForTag(p_Tag);
    }

    std::vector<VoiceInfo> SpeechService::Voices() const {
        std::scoped_lock s_Lock(m_Mutex);
        return m_Voices;
    }

    bool SpeechService::TryPopStarted(SpeechStarted& p_Started) {
        std::scoped_lock s_Lock(m_Mutex);

        if (m_Started.empty()) {
            return false;
        }

        p_Started = m_Started.front();
        m_Started.pop_front();
        return true;
    }

    const VoiceInfo* SpeechService::PickVoice(
        const std::vector<VoiceInfo>& p_Voices, const std::string& p_Name, bool p_Female, bool p_German
    ) {
        if (!p_Name.empty()) {
            for (const auto& s_Voice : p_Voices) {
                if (s_Voice.m_Name == p_Name) {
                    return &s_Voice;
                }
            }
        }

        const auto s_LanguageMatches = [&](const VoiceInfo& p_Voice) {
            return p_German ? p_Voice.IsGerman() : p_Voice.IsEnglish();
        };

        // 1) Sprache + Geschlecht, OneCore bevorzugt (klingen besser).
        for (const bool s_WantOneCore : { true, false }) {
            for (const auto& s_Voice : p_Voices) {
                if (s_Voice.m_OneCore == s_WantOneCore && s_LanguageMatches(s_Voice) && s_Voice.m_Female == p_Female) {
                    return &s_Voice;
                }
            }
        }

        // 2) Sprache egal welches Geschlecht.
        for (const auto& s_Voice : p_Voices) {
            if (s_LanguageMatches(s_Voice)) {
                return &s_Voice;
            }
        }

        // 3) Irgendeine.
        return p_Voices.empty() ? nullptr : &p_Voices.front();
    }

    void SpeechService::WorkerLoop() {
        SetThreadDescription(GetCurrentThread(), L"LivingWorld TTS");

        SapiTts s_Tts;
        std::string s_Error;

        if (!m_Audio.Init(s_Error) || !s_Tts.Init(s_Error)) {
            log::Error("Sprachausgabe nicht verfuegbar: {}", s_Error);

            std::scoped_lock s_Lock(m_Mutex);
            m_LastError = s_Error;
            return;
        }

        {
            std::vector<VoiceInfo> s_Voices = s_Tts.ListVoices();

            for (const auto& s_Voice : s_Voices) {
                log::Info(
                    "Stimme: {} ({}, {}{})", s_Voice.m_Name, s_Voice.m_Language, s_Voice.m_Female ? "weiblich" : "maennlich",
                    s_Voice.m_OneCore ? ", OneCore" : ""
                );
            }

            std::scoped_lock s_Lock(m_Mutex);
            m_Voices = std::move(s_Voices);
        }

        m_Ready = true;

        while (true) {
            SpeakRequest s_Request;

            {
                std::unique_lock s_Lock(m_Mutex);
                m_Cv.wait_for(s_Lock, std::chrono::milliseconds(250), [this]() {
                    return !m_Running || !m_Queue.empty();
                });

                if (!m_Running) {
                    break;
                }

                if (m_Queue.empty()) {
                    s_Lock.unlock();
                    m_Audio.Collect();
                    continue;
                }

                s_Request = std::move(m_Queue.front());
                m_Queue.pop_front();
                m_CurrentTag = s_Request.m_Tag;
            }

            const VoiceInfo* s_Voice = PickVoice(m_Voices, s_Request.m_VoiceName, s_Request.m_Female, s_Request.m_German);
            PcmBuffer s_Pcm;

            if (s_Tts.Synthesize(s_Request.m_Text, s_Voice ? s_Voice->m_TokenId : L"", s_Request.m_Style, s_Pcm, s_Error)) {
                const uint64_t s_Handle = m_Audio.Play(s_Pcm, s_Request.m_Volume, s_Request.m_Pan, s_Request.m_Tag);

                std::scoped_lock s_Lock(m_Mutex);
                m_Started.push_back({ s_Handle, s_Request.m_Tag, s_Request.m_Generation, s_Pcm.DurationSeconds() });

                while (m_Started.size() > 32) {
                    m_Started.pop_front();
                }
            }
            else {
                log::Warn("TTS fehlgeschlagen: {}", s_Error);

                std::scoped_lock s_Lock(m_Mutex);
                m_LastError = s_Error;
            }

            {
                std::scoped_lock s_Lock(m_Mutex);
                m_CurrentTag = 0;
            }

            m_Audio.Collect();
        }

        m_Ready = false;
        s_Tts.Shutdown();
        m_Audio.Shutdown();
    }
}
