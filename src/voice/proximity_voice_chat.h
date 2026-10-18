#ifndef PROXIMITY_VOICE_CHAT_H
#define PROXIMITY_VOICE_CHAT_H

#include <opus/opus.h>
#include <miniaudio/miniaudio.h>
#include <atomic>
#include <vector>
#include <map>
#include <queue>
#include <mutex>
#include <cstdint>
#include "../network/packets.h"

// ---------------------------------------------------------------------------
// ProximityVoiceChat
//
// Handles push-to-talk voice over the existing ENet connection.
// - Opens a real miniaudio capture device on initialize() → Windows shows
//   the microphone icon in the system tray while the app is running.
// - While C is held, captured PCM is Opus-encoded and sent via NetworkManager.
// - Incoming VOICE_DATA packets (already proximity-filtered by network_manager)
//   are Opus-decoded and mixed into a miniaudio playback device.
// ---------------------------------------------------------------------------
class ProximityVoiceChat {
public:
    ProximityVoiceChat();
    ~ProximityVoiceChat();

    // Open audio devices and codecs. Returns true on success.
    bool initialize();
    void shutdown();

    // Call every frame (updates any per-frame voice logic)
    void update(float deltaTime, class Character* localPlayer);

    // Process an incoming Opus voice packet (already proximity-checked)
    void processVoicePacket(const PacketVoiceData* packet, uint32_t senderID);

    // Push-to-talk control
    void setTransmitEnabled(bool enabled);
    bool isLocalSpeaking() const;
    void setOutputVolume(float volume) { m_outputVolume.store(volume); }

    // UI: true if we recently received audio from userID
    bool isUserSpeaking(uint32_t userID) const;

    // Returns how many unique senders we have heard in the last second
    int getAudiblePlayerCount() const;

private:
    friend struct VoiceTestAccess;
    // ---- miniaudio callbacks ----
    static void captureCallback(ma_device* dev, void* pOut, const void* pIn, ma_uint32 frames);
    static void playbackCallback(ma_device* dev, void* pOut, const void* pIn, ma_uint32 frames);

    // ---- internal helpers ----
    // Encode and send PCM frame over network
    void encodeAndSend(const int16_t* pcm, int frameCount);

    // Per-sender jitter entry
    struct VoiceFrame {
        uint16_t seq;
        std::vector<uint8_t> opus;
    };

    // Per-sender state
    struct SenderState {
        OpusDecoder* decoder  = nullptr;
        std::queue<VoiceFrame> jitter;
        std::mutex            jitterMtx;
        std::atomic<uint64_t> lastHeardMs{0};
        uint16_t              lastSeq     = 0;
        // Decoded PCM ring buffer for playback callback
        std::vector<float>    pcmRing;
        std::mutex            pcmMtx;
        size_t                writePos = 0;
        size_t                readPos  = 0;
    };

    // ---- members ----
    ma_device       m_captureDevice;
    ma_device       m_playbackDevice;
    OpusEncoder*    m_encoder = nullptr;

    std::atomic<bool>     m_initialized{false};
    std::atomic<float>    m_outputVolume{0.8f};
    std::atomic<bool>     m_transmitting{false};
    std::atomic<uint64_t> m_localSpeakUntilMs{0};

    // Capture ring buffer (written by capture callback, read by encode thread path)
    static constexpr int SAMPLE_RATE = 48000;
    static constexpr int CHANNELS    = 1;
    static constexpr int FRAME_MS    = 20;
    static constexpr int FRAME_SAMPLES = (SAMPLE_RATE * FRAME_MS) / 1000; // 960

    std::vector<int16_t> m_captureBuf;   // accumulates PCM from callback
    std::mutex           m_captureMtx;

    // One SenderState per remote speaker
    std::map<uint32_t, SenderState*> m_senders;
    std::mutex                        m_sendersMtx;

    // Sequence counter for outgoing packets
    uint16_t m_sendSeq = 0;
    std::mutex m_outgoingMtx;
    std::queue<PacketVoiceData> m_outgoing;

    static constexpr uint64_t SPEAK_TIMEOUT_MS = 800;
};

#endif // PROXIMITY_VOICE_CHAT_H
