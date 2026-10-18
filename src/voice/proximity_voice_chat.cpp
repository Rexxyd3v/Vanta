#include "proximity_voice_chat.h"
#include "../network/network_manager.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <chrono>

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static uint64_t NowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

// ---------------------------------------------------------------------------
// ctor / dtor
// ---------------------------------------------------------------------------
ProximityVoiceChat::ProximityVoiceChat() {
    m_captureBuf.reserve(FRAME_SAMPLES * 4);
}

ProximityVoiceChat::~ProximityVoiceChat() {
    shutdown();
}

// ---------------------------------------------------------------------------
// initialize
// ---------------------------------------------------------------------------
bool ProximityVoiceChat::initialize() {
    if (m_initialized.load()) return true;

    // ---- Opus encoder ----
    int err = 0;
    m_encoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP, &err);
    if (!m_encoder || err < 0) {
        std::cerr << "[Voice] Failed to create Opus encoder: " << opus_strerror(err) << std::endl;
        return false;
    }
    opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(24000));
    opus_encoder_ctl(m_encoder, OPUS_SET_VBR(1));
    opus_encoder_ctl(m_encoder, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(m_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(m_encoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(m_encoder, OPUS_SET_PACKET_LOSS_PERC(10));

    // ---- Capture device (microphone) ----
    // Opening this is what makes Windows show the microphone icon in the tray.
    ma_device_config capCfg = ma_device_config_init(ma_device_type_capture);
    capCfg.capture.pDeviceID = nullptr; // default mic
    capCfg.capture.format    = ma_format_s16;
    capCfg.capture.channels  = CHANNELS;
    capCfg.sampleRate        = SAMPLE_RATE;
    capCfg.dataCallback      = captureCallback;
    capCfg.pUserData         = this;

    if (ma_device_init(nullptr, &capCfg, &m_captureDevice) != MA_SUCCESS) {
        std::cerr << "[Voice] Failed to open capture device." << std::endl;
        opus_encoder_destroy(m_encoder);
        m_encoder = nullptr;
        return false;
    }

    // ---- Playback device (speakers) ----
    ma_device_config pbCfg = ma_device_config_init(ma_device_type_playback);
    pbCfg.playback.pDeviceID = nullptr; // default speakers
    pbCfg.playback.format    = ma_format_f32;
    pbCfg.playback.channels  = CHANNELS;
    pbCfg.sampleRate         = SAMPLE_RATE;
    pbCfg.dataCallback       = playbackCallback;
    pbCfg.pUserData          = this;

    if (ma_device_init(nullptr, &pbCfg, &m_playbackDevice) != MA_SUCCESS) {
        std::cerr << "[Voice] Failed to open playback device." << std::endl;
        ma_device_uninit(&m_captureDevice);
        opus_encoder_destroy(m_encoder);
        m_encoder = nullptr;
        return false;
    }

    // Start both devices
    if (ma_device_start(&m_captureDevice) != MA_SUCCESS) {
        std::cerr << "[Voice] Failed to start capture device." << std::endl;
        ma_device_uninit(&m_captureDevice);
        ma_device_uninit(&m_playbackDevice);
        opus_encoder_destroy(m_encoder);
        m_encoder = nullptr;
        return false;
    }
    if (ma_device_start(&m_playbackDevice) != MA_SUCCESS) {
        std::cerr << "[Voice] Failed to start playback device." << std::endl;
        ma_device_stop(&m_captureDevice);
        ma_device_uninit(&m_captureDevice);
        ma_device_uninit(&m_playbackDevice);
        opus_encoder_destroy(m_encoder);
        m_encoder = nullptr;
        return false;
    }

    m_initialized.store(true);
    std::cout << "[Voice] Proximity voice chat initialized (mic open, Windows icon visible)." << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------
void ProximityVoiceChat::shutdown() {
    if (!m_initialized.exchange(false)) return;

    m_transmitting.store(false);
    ma_device_stop(&m_captureDevice);
    ma_device_stop(&m_playbackDevice);
    ma_device_uninit(&m_captureDevice);
    ma_device_uninit(&m_playbackDevice);

    { std::lock_guard<std::mutex> lock(m_captureMtx);m_captureBuf.clear(); }
    { std::lock_guard<std::mutex> lock(m_outgoingMtx);while(!m_outgoing.empty()) m_outgoing.pop(); }
    m_localSpeakUntilMs.store(0);
    if (m_encoder) {
        opus_encoder_destroy(m_encoder);
        m_encoder = nullptr;
    }

    std::lock_guard<std::mutex> lk(m_sendersMtx);
    for (auto& [id, s] : m_senders) {
        if (s->decoder) opus_decoder_destroy(s->decoder);
        delete s;
    }
    m_senders.clear();

    std::cout << "[Voice] Proximity voice chat shut down." << std::endl;
}

// ---------------------------------------------------------------------------
// setTransmitEnabled  (push-to-talk)
// ---------------------------------------------------------------------------
void ProximityVoiceChat::setTransmitEnabled(bool enabled) {
    enabled=enabled && m_initialized.load();
    bool previous=m_transmitting.exchange(enabled);
    if(previous && !enabled) {
        std::lock_guard<std::mutex> captureLock(m_captureMtx);
        m_captureBuf.clear();
        std::lock_guard<std::mutex> outgoingLock(m_outgoingMtx);
        while(!m_outgoing.empty()) m_outgoing.pop();
    }
    if (enabled) {
        // Keep speaking indicator alive for a brief tail after release
        m_localSpeakUntilMs.store(NowMs() + 300);
    }
}

// ---------------------------------------------------------------------------
// isLocalSpeaking
// ---------------------------------------------------------------------------
bool ProximityVoiceChat::isLocalSpeaking() const {
    return m_transmitting.load() || (NowMs() < m_localSpeakUntilMs.load());
}

// ---------------------------------------------------------------------------
// isUserSpeaking
// ---------------------------------------------------------------------------
bool ProximityVoiceChat::isUserSpeaking(uint32_t userID) const {
    std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(m_sendersMtx));
    auto it = m_senders.find(userID);
    if (it == m_senders.end()) return false;
    return (NowMs() - it->second->lastHeardMs) < SPEAK_TIMEOUT_MS;
}

// ---------------------------------------------------------------------------
// getAudiblePlayerCount
// ---------------------------------------------------------------------------
int ProximityVoiceChat::getAudiblePlayerCount() const {
    std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(m_sendersMtx));
    int count = 0;
    uint64_t now = NowMs();
    for (auto& [id, s] : m_senders) {
        if ((now - s->lastHeardMs) < SPEAK_TIMEOUT_MS) ++count;
    }
    return count;
}

// ---------------------------------------------------------------------------
// update  (called every frame — no heavy work here)
// ---------------------------------------------------------------------------
void ProximityVoiceChat::update(float /*deltaTime*/, class Character* /*localPlayer*/) {
    // ENet is serviced exclusively on the game thread, never the audio callback.
    std::queue<PacketVoiceData> ready;
    { std::lock_guard<std::mutex> lock(m_outgoingMtx);ready.swap(m_outgoing); }
    auto& network=NetworkManager::GetInstance();
    if(!m_initialized.load() || !m_transmitting.load() || !network.IsConnected()) return;
    while(!ready.empty()) {
        auto& packet=ready.front();
        packet.header.playerID=network.GetLocalPlayerID();
        size_t bytes=sizeof(PacketHeader)+sizeof(packet.sequenceNumber)+sizeof(packet.frameSize)+packet.frameSize;
        network.SendPacket(&packet,bytes,false);
        ready.pop();
    }
}

// ---------------------------------------------------------------------------
// processVoicePacket  — called by network_manager after proximity check
// ---------------------------------------------------------------------------
void ProximityVoiceChat::processVoicePacket(const PacketVoiceData* packet, uint32_t senderID) {
    if (!m_initialized.load() || !packet || packet->frameSize == 0) return;
    if (packet->frameSize > MAX_OPUS_FRAME_SIZE) return;

    // Get or create sender state
    SenderState* s = nullptr;
    {
        std::lock_guard<std::mutex> lk(m_sendersMtx);
        auto it = m_senders.find(senderID);
        if (it == m_senders.end()) {
            s = new SenderState();
            int err = 0;
            s->decoder = opus_decoder_create(SAMPLE_RATE, CHANNELS, &err);
            if (!s->decoder || err < 0) {
                std::cerr << "[Voice] Failed to create decoder for sender " << senderID << std::endl;
                delete s;
                return;
            }
            // Ring buffer: 4 frames of float PCM
            s->pcmRing.assign(FRAME_SAMPLES * 4, 0.0f);
            m_senders[senderID] = s;
        } else {
            s = it->second;
        }
    }

    // Deduplicate / reorder check
    const uint16_t seq = packet->sequenceNumber;
    {
        std::lock_guard<std::mutex> lk(s->jitterMtx);
        // Simple check: accept if seq is ahead or first packet
        int16_t diff = static_cast<int16_t>(seq - s->lastSeq);
        if (diff <= 0 && s->lastHeardMs != 0) return; // old/duplicate
        s->lastSeq     = seq;
        s->lastHeardMs = NowMs();

        VoiceFrame f;
        f.seq = seq;
        f.opus.assign(packet->opusData, packet->opusData + packet->frameSize);
        s->jitter.push(std::move(f));
    }

    // Decode immediately from jitter queue into PCM ring
    {
        std::lock_guard<std::mutex> jlk(s->jitterMtx);
        while (!s->jitter.empty()) {
            VoiceFrame& f = s->jitter.front();

            std::vector<int16_t> pcm(FRAME_SAMPLES);
            int decoded = opus_decode(
                s->decoder,
                f.opus.data(),
                static_cast<opus_int32>(f.opus.size()),
                pcm.data(),
                FRAME_SAMPLES,
                0
            );

            if (decoded > 0) {
                // Convert int16 → float and push into ring buffer
                std::lock_guard<std::mutex> plk(s->pcmMtx);
                for (int i = 0; i < decoded; ++i) {
                    float sample = static_cast<float>(pcm[i]) / 32768.0f;
                    s->pcmRing[s->writePos % s->pcmRing.size()] = sample;
                    s->writePos++;
                }
            }
            s->jitter.pop();
        }
    }
}

// ---------------------------------------------------------------------------
// encodeAndSend
// ---------------------------------------------------------------------------
void ProximityVoiceChat::encodeAndSend(const int16_t* pcm, int frameCount) {
    if (!m_encoder) return;

    uint8_t outBuf[MAX_OPUS_FRAME_SIZE];
    int encoded = opus_encode(
        m_encoder,
        pcm,
        frameCount,
        outBuf,
        MAX_OPUS_FRAME_SIZE
    );
    if (encoded <= 0) return;

    PacketVoiceData pkt{};
    pkt.header.type     = PacketType::VOICE_DATA;
    pkt.header.playerID = 0; // Assigned by the game thread when draining the queue.
    pkt.sequenceNumber  = ++m_sendSeq;
    pkt.frameSize       = static_cast<uint16_t>(encoded);
    std::memcpy(pkt.opusData, outBuf, encoded);

    std::lock_guard<std::mutex> lock(m_outgoingMtx);
    if(!m_transmitting.load()) return;
    // Bound latency during a stalled game frame to 200 ms of recent audio.
    while(m_outgoing.size()>=10) m_outgoing.pop();
    m_outgoing.push(pkt);
}

// ---------------------------------------------------------------------------
// captureCallback  (audio thread — called by miniaudio)
// ---------------------------------------------------------------------------
void ProximityVoiceChat::captureCallback(ma_device* dev, void* /*pOut*/,
                                          const void* pIn, ma_uint32 frames)
{
    auto* self = static_cast<ProximityVoiceChat*>(dev->pUserData);
    if (!self || !self->m_initialized.load() || !pIn) return;

    // Only transmit when push-to-talk is held
    if (!self->m_transmitting.load()) {
        // Update speak-tail timer
        return;
    }

    const auto* pcmIn = static_cast<const int16_t*>(pIn);

    std::lock_guard<std::mutex> lk(self->m_captureMtx);
    if(!self->m_transmitting.load()) return;
    for (ma_uint32 i = 0; i < frames; ++i) {
        self->m_captureBuf.push_back(pcmIn[i]);
    }

    // Encode complete frames as they accumulate
    while (self->m_captureBuf.size() >= static_cast<size_t>(FRAME_SAMPLES)) {
        self->encodeAndSend(self->m_captureBuf.data(), FRAME_SAMPLES);
        self->m_captureBuf.erase(
            self->m_captureBuf.begin(),
            self->m_captureBuf.begin() + FRAME_SAMPLES
        );
    }

    // Keep speaking indicator alive
    self->m_localSpeakUntilMs.store(NowMs() + 300);
}

// ---------------------------------------------------------------------------
// playbackCallback  (audio thread — called by miniaudio)
// ---------------------------------------------------------------------------
void ProximityVoiceChat::playbackCallback(ma_device* dev, void* pOut,
                                           const void* /*pIn*/, ma_uint32 frames)
{
    auto* self = static_cast<ProximityVoiceChat*>(dev->pUserData);
    auto* out  = static_cast<float*>(pOut);

    // Zero output first
    std::memset(out, 0, frames * sizeof(float));

    if (!self || !self->m_initialized.load()) return;

    // Mix all active senders into output
    std::lock_guard<std::mutex> lk(self->m_sendersMtx);
    for (auto& [id, s] : self->m_senders) {
        std::lock_guard<std::mutex> plk(s->pcmMtx);

        size_t available = s->writePos - s->readPos;
        if (available > s->pcmRing.size()) {
            // Read cursor fell too far behind (e.g., network burst or thread delay).
            // Sync read cursor to the oldest non-overwritten sample to prevent latency/looping.
            s->readPos = s->writePos - s->pcmRing.size();
            available = s->pcmRing.size();
        }

        size_t toRead    = std::min(static_cast<size_t>(frames), available);

        for (size_t i = 0; i < toRead; ++i) {
            float sample = s->pcmRing[s->readPos % s->pcmRing.size()];
            s->readPos++;
            out[i] = std::clamp(out[i] + sample * self->m_outputVolume.load(), -1.0f, 1.0f);
        }
    }
}
