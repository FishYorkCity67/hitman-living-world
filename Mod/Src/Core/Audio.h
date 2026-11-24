#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;

namespace lw {
    // 16-Bit-Mono-PCM, wie es TTS liefert.
    struct PcmBuffer {
        std::vector<int16_t> m_Samples;
        uint32_t m_SampleRate = 22050;

        double DurationSeconds() const {
            return m_SampleRate ? static_cast<double>(m_Samples.size()) / m_SampleRate : 0.0;
        }
    };

    // Spielt Stimmen ueber XAudio2 ab – mit Lautstaerke und Stereo-Panorama je nach Position des Sprechers.
    class AudioOut {
    public:
        AudioOut() = default;
        ~AudioOut();

        AudioOut(const AudioOut&) = delete;
        AudioOut& operator=(const AudioOut&) = delete;

        bool Init(std::string& p_Error);
        void Shutdown();
        bool IsReady() const { return m_Engine != nullptr; }

        // Gibt ein Handle zurueck (0 = Fehler). p_Pan: -1 links .. +1 rechts.
        uint64_t Play(const PcmBuffer& p_Pcm, float p_Volume, float p_Pan, uint64_t p_Tag);

        void SetVolumePan(uint64_t p_Handle, float p_Volume, float p_Pan);
        bool IsPlaying(uint64_t p_Handle);
        bool IsAnyPlayingForTag(uint64_t p_Tag);
        void Stop(uint64_t p_Handle);
        void StopAll();

        // Aufraeumen fertiger Stimmen; regelmaessig aufrufen.
        void Collect();

        struct ActiveVoice {
            uint64_t m_Handle;
            uint64_t m_Tag;
        };

        std::vector<ActiveVoice> ActiveVoices();

    private:
        struct Voice {
            uint64_t m_Handle = 0;
            uint64_t m_Tag = 0;
            IXAudio2SourceVoice* m_Source = nullptr;
            std::vector<int16_t> m_Data; // muss leben, solange XAudio2 abspielt
        };

        void ApplyVolumePan(Voice& p_Voice, float p_Volume, float p_Pan);

        std::mutex m_Mutex;
        IXAudio2* m_Engine = nullptr;
        IXAudio2MasteringVoice* m_Master = nullptr;
        uint32_t m_MasterChannels = 2;
        std::vector<Voice> m_Voices;
        uint64_t m_NextHandle = 1;
        bool m_ComInitialized = false;
    };

    // Schneidet Stille am Anfang/Ende ab und kuerzt lange Sprechpausen (SAPI macht nach Satzenden fast 1 s Pause).
    void TightenSilence(PcmBuffer& p_Pcm, int p_MaxPauseMs = 260, int p_EdgeMs = 40);

    // Raeumliche Berechnung: Lautstaerke (0..1) und Panorama (-1..1) aus Hoerer- und Quellposition.
    struct SpatialResult {
        float m_Volume;
        float m_Pan;
    };

    SpatialResult ComputeSpatial(
        const float p_ListenerPos[3], const float p_ListenerRight[3], const float p_SourcePos[3],
        float p_MaxDistance
    );
}
