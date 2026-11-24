#include "Audio.h"

#include "Log.h"

#include <Windows.h>
#include <objbase.h>
#include <xaudio2.h>

#include <algorithm>
#include <cmath>

namespace lw {
    AudioOut::~AudioOut() {
        Shutdown();
    }

    bool AudioOut::Init(std::string& p_Error) {
        std::scoped_lock s_Lock(m_Mutex);

        if (m_Engine) {
            return true;
        }

        // XAudio2 braucht COM auf dem aufrufenden Thread (MTA reicht).
        const HRESULT s_Com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_ComInitialized = SUCCEEDED(s_Com);

        HRESULT s_Result = XAudio2Create(&m_Engine, 0, XAUDIO2_DEFAULT_PROCESSOR);

        if (FAILED(s_Result) || !m_Engine) {
            p_Error = "XAudio2Create fehlgeschlagen (0x" + std::to_string(static_cast<unsigned>(s_Result)) + ")";
            m_Engine = nullptr;
            return false;
        }

        s_Result = m_Engine->CreateMasteringVoice(&m_Master);

        if (FAILED(s_Result) || !m_Master) {
            p_Error = "Kein Audio-Ausgabegeraet (0x" + std::to_string(static_cast<unsigned>(s_Result)) + ")";
            m_Engine->Release();
            m_Engine = nullptr;
            return false;
        }

        XAUDIO2_VOICE_DETAILS s_Details {};
        m_Master->GetVoiceDetails(&s_Details);
        m_MasterChannels = std::max<uint32_t>(1, s_Details.InputChannels);

        log::Info("Audio bereit ({} Kanaele, {} Hz).", m_MasterChannels, s_Details.InputSampleRate);
        return true;
    }

    void AudioOut::Shutdown() {
        std::scoped_lock s_Lock(m_Mutex);

        for (auto& s_Voice : m_Voices) {
            if (s_Voice.m_Source) {
                s_Voice.m_Source->Stop();
                s_Voice.m_Source->DestroyVoice();
            }
        }

        m_Voices.clear();

        if (m_Master) {
            m_Master->DestroyVoice();
            m_Master = nullptr;
        }

        if (m_Engine) {
            m_Engine->Release();
            m_Engine = nullptr;
        }

        if (m_ComInitialized) {
            CoUninitialize();
            m_ComInitialized = false;
        }
    }

    void AudioOut::ApplyVolumePan(Voice& p_Voice, float p_Volume, float p_Pan) {
        if (!p_Voice.m_Source) {
            return;
        }

        p_Voice.m_Source->SetVolume(std::clamp(p_Volume, 0.f, 4.f));

        if (m_MasterChannels < 2) {
            return;
        }

        // Konstante Leistung beim Panorama: links = cos, rechts = sin.
        const float s_Angle = (std::clamp(p_Pan, -1.f, 1.f) + 1.f) * 0.25f * 3.14159265f;
        std::vector<float> s_Matrix(m_MasterChannels, 0.f);
        s_Matrix[0] = std::cos(s_Angle);
        s_Matrix[1] = std::sin(s_Angle);

        p_Voice.m_Source->SetOutputMatrix(m_Master, 1, m_MasterChannels, s_Matrix.data());
    }

    uint64_t AudioOut::Play(const PcmBuffer& p_Pcm, float p_Volume, float p_Pan, uint64_t p_Tag) {
        std::scoped_lock s_Lock(m_Mutex);

        if (!m_Engine || p_Pcm.m_Samples.empty()) {
            return 0;
        }

        WAVEFORMATEX s_Format {};
        s_Format.wFormatTag = WAVE_FORMAT_PCM;
        s_Format.nChannels = 1;
        s_Format.nSamplesPerSec = p_Pcm.m_SampleRate;
        s_Format.wBitsPerSample = 16;
        s_Format.nBlockAlign = 2;
        s_Format.nAvgBytesPerSec = p_Pcm.m_SampleRate * 2;

        Voice s_Voice;
        s_Voice.m_Handle = m_NextHandle++;
        s_Voice.m_Tag = p_Tag;
        s_Voice.m_Data = p_Pcm.m_Samples;

        if (FAILED(m_Engine->CreateSourceVoice(&s_Voice.m_Source, &s_Format))) {
            log::Warn("CreateSourceVoice fehlgeschlagen.");
            return 0;
        }

        XAUDIO2_BUFFER s_Buffer {};
        s_Buffer.AudioBytes = static_cast<UINT32>(s_Voice.m_Data.size() * sizeof(int16_t));
        s_Buffer.pAudioData = reinterpret_cast<const BYTE*>(s_Voice.m_Data.data());
        s_Buffer.Flags = XAUDIO2_END_OF_STREAM;

        if (FAILED(s_Voice.m_Source->SubmitSourceBuffer(&s_Buffer))) {
            s_Voice.m_Source->DestroyVoice();
            return 0;
        }

        ApplyVolumePan(s_Voice, p_Volume, p_Pan);
        s_Voice.m_Source->Start();

        const uint64_t s_Handle = s_Voice.m_Handle;
        m_Voices.push_back(std::move(s_Voice));
        return s_Handle;
    }

    void AudioOut::SetVolumePan(uint64_t p_Handle, float p_Volume, float p_Pan) {
        std::scoped_lock s_Lock(m_Mutex);

        for (auto& s_Voice : m_Voices) {
            if (s_Voice.m_Handle == p_Handle) {
                ApplyVolumePan(s_Voice, p_Volume, p_Pan);
                return;
            }
        }
    }

    bool AudioOut::IsPlaying(uint64_t p_Handle) {
        std::scoped_lock s_Lock(m_Mutex);

        for (auto& s_Voice : m_Voices) {
            if (s_Voice.m_Handle == p_Handle && s_Voice.m_Source) {
                XAUDIO2_VOICE_STATE s_State {};
                s_Voice.m_Source->GetState(&s_State, XAUDIO2_VOICE_NOSAMPLESPLAYED);
                return s_State.BuffersQueued > 0;
            }
        }

        return false;
    }

    bool AudioOut::IsAnyPlayingForTag(uint64_t p_Tag) {
        std::scoped_lock s_Lock(m_Mutex);

        for (auto& s_Voice : m_Voices) {
            if (s_Voice.m_Tag == p_Tag && s_Voice.m_Source) {
                XAUDIO2_VOICE_STATE s_State {};
                s_Voice.m_Source->GetState(&s_State, XAUDIO2_VOICE_NOSAMPLESPLAYED);

                if (s_State.BuffersQueued > 0) {
                    return true;
                }
            }
        }

        return false;
    }

    void AudioOut::Stop(uint64_t p_Handle) {
        std::scoped_lock s_Lock(m_Mutex);

        for (auto& s_Voice : m_Voices) {
            if (s_Voice.m_Handle == p_Handle && s_Voice.m_Source) {
                s_Voice.m_Source->Stop();
                s_Voice.m_Source->FlushSourceBuffers();
            }
        }
    }

    void AudioOut::StopAll() {
        std::scoped_lock s_Lock(m_Mutex);

        for (auto& s_Voice : m_Voices) {
            if (s_Voice.m_Source) {
                s_Voice.m_Source->Stop();
                s_Voice.m_Source->FlushSourceBuffers();
            }
        }
    }

    void AudioOut::Collect() {
        std::scoped_lock s_Lock(m_Mutex);

        std::erase_if(m_Voices, [](Voice& p_Voice) {
            if (!p_Voice.m_Source) {
                return true;
            }

            XAUDIO2_VOICE_STATE s_State {};
            p_Voice.m_Source->GetState(&s_State, XAUDIO2_VOICE_NOSAMPLESPLAYED);

            if (s_State.BuffersQueued > 0) {
                return false;
            }

            p_Voice.m_Source->DestroyVoice();
            p_Voice.m_Source = nullptr;
            return true;
        });
    }

    std::vector<AudioOut::ActiveVoice> AudioOut::ActiveVoices() {
        std::scoped_lock s_Lock(m_Mutex);
        std::vector<ActiveVoice> s_Result;

        for (const auto& s_Voice : m_Voices) {
            s_Result.push_back({ s_Voice.m_Handle, s_Voice.m_Tag });
        }

        return s_Result;
    }

    void TightenSilence(PcmBuffer& p_Pcm, int p_MaxPauseMs, int p_EdgeMs) {
        auto& s_Samples = p_Pcm.m_Samples;

        if (s_Samples.empty() || p_Pcm.m_SampleRate == 0) {
            return;
        }

        const size_t s_Frame = std::max<size_t>(1, p_Pcm.m_SampleRate / 100); // 10 ms
        const size_t s_Frames = s_Samples.size() / s_Frame;
        constexpr double s_Threshold = 250.0; // RMS in 16-Bit-Einheiten (~ -42 dBFS)

        std::vector<bool> s_Loud(s_Frames, false);

        for (size_t f = 0; f < s_Frames; ++f) {
            double s_Sum = 0.0;

            for (size_t i = 0; i < s_Frame; ++i) {
                const double s_Value = s_Samples[f * s_Frame + i];
                s_Sum += s_Value * s_Value;
            }

            s_Loud[f] = std::sqrt(s_Sum / static_cast<double>(s_Frame)) > s_Threshold;
        }

        size_t s_First = 0;
        while (s_First < s_Frames && !s_Loud[s_First]) ++s_First;

        if (s_First == s_Frames) {
            return; // nur Stille – lieber nichts anfassen
        }

        size_t s_Last = s_Frames - 1;
        while (s_Last > s_First && !s_Loud[s_Last]) --s_Last;

        const size_t s_Edge = static_cast<size_t>(p_EdgeMs / 10);
        const size_t s_MaxPause = static_cast<size_t>(p_MaxPauseMs / 10);
        const size_t s_Begin = s_First > s_Edge ? s_First - s_Edge : 0;
        const size_t s_End = std::min(s_Frames, s_Last + 1 + s_Edge);

        std::vector<int16_t> s_Result;
        s_Result.reserve(s_Samples.size());
        size_t s_SilentRun = 0;

        for (size_t f = s_Begin; f < s_End; ++f) {
            s_SilentRun = s_Loud[f] ? 0 : s_SilentRun + 1;

            if (s_SilentRun > s_MaxPause) {
                continue; // zu lange Pause: diesen Frame weglassen
            }

            s_Result.insert(
                s_Result.end(), s_Samples.begin() + static_cast<ptrdiff_t>(f * s_Frame),
                s_Samples.begin() + static_cast<ptrdiff_t>((f + 1) * s_Frame)
            );
        }

        s_Samples = std::move(s_Result);
    }

    SpatialResult ComputeSpatial(
        const float p_ListenerPos[3], const float p_ListenerRight[3], const float p_SourcePos[3],
        float p_MaxDistance
    ) {
        const float s_Dx = p_SourcePos[0] - p_ListenerPos[0];
        const float s_Dy = p_SourcePos[1] - p_ListenerPos[1];
        const float s_Dz = p_SourcePos[2] - p_ListenerPos[2];
        const float s_Distance = std::sqrt(s_Dx * s_Dx + s_Dy * s_Dy + s_Dz * s_Dz);

        // Bis 2 m volle Lautstaerke, danach weich bis zur Maximaldistanz abfallend (nie ganz stumm unter 0.08).
        constexpr float s_Near = 2.f;
        float s_Volume = 1.f;

        if (s_Distance > s_Near) {
            const float s_T = std::clamp((s_Distance - s_Near) / std::max(1.f, p_MaxDistance - s_Near), 0.f, 1.f);
            s_Volume = std::max(0.08f, (1.f - s_T) * (1.f - s_T));
        }

        float s_Pan = 0.f;

        if (s_Distance > 0.3f) {
            const float s_RightLength = std::sqrt(
                p_ListenerRight[0] * p_ListenerRight[0] + p_ListenerRight[1] * p_ListenerRight[1] +
                p_ListenerRight[2] * p_ListenerRight[2]
            );

            if (s_RightLength > 0.0001f) {
                const float s_Dot = (s_Dx * p_ListenerRight[0] + s_Dy * p_ListenerRight[1] + s_Dz * p_ListenerRight[2]) /
                    (s_Distance * s_RightLength);

                // Nicht ganz hart auf eine Seite – klingt sonst unnatuerlich.
                s_Pan = std::clamp(s_Dot, -1.f, 1.f) * 0.85f;
            }
        }

        return { s_Volume, s_Pan };
    }
}
