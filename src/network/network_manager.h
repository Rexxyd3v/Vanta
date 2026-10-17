#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

// These defines MUST come before enet/enet.h to prevent Windows SDK
// from defining conflicting names like Rectangle, DrawText, LoadImage, etc.
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <enet/enet.h>
#include <string>
#include <vector>
#include <ws2tcpip.h>
#include <map>
#include "../constants.h"
#include "packets.h"

class NetworkManager {
public:
    static NetworkManager& GetInstance();

    bool Initialize();
    void Shutdown();

    // Host a room
    bool HostRoom(int port = DEFAULT_GAME_PORT);

    // Join a room. Address may be "host" or "host:port" (e.g. playit.gg tunnel).
    bool JoinRoom(const std::string& address, int defaultPort = DEFAULT_GAME_PORT);

    // Async (non-blocking) join: call BeginJoinRoom() once, then poll PollJoinRoom() every frame.
    enum class JoinState { IDLE, CONNECTING, CONNECTED, FAILED };
    bool BeginJoinRoom(const std::string& address, int defaultPort = DEFAULT_GAME_PORT);
    JoinState PollJoinRoom(); // Call every frame until CONNECTED or FAILED
    JoinState GetJoinState() const { return joinState; }

    // Parse "host" or "host:port" into host + port. Returns false if host is empty.
    static bool ParseServerAddress(const std::string& address, std::string& host, int& port,
                                   int defaultPort = DEFAULT_GAME_PORT);

    // Disconnect or stop hosting
    void Disconnect();

    // Update ENet events (call every frame)
    void Update();

    // Check status
    bool IsConnected() const { return isConnected; }
    bool IsHost() const { return isHost; }
    uint32_t GetLocalPeerID() const { return localPlayerID; } // Returns authoritative playerID (kept name for compat)
    uint32_t GetLocalPlayerID() const { return localPlayerID; }
    bool IsWaitingForAssignment() const { return waitingForAssignment; }

    // Get local IP address for display
    std::string GetLocalIPAddress();

    // Player ready state methods
    void SetPlayerReady(bool ready);
    bool IsLocalPlayerReady() const;
    bool AllPlayersReady() const;
    int GetReadyPlayerCount() const;
    int GetTotalPlayerCount() const;
    bool StartGame();
    bool CanStartGame() const;
    std::string StartRequirement() const;
    int GetPingMs() const;
    void PrepareLobby();
    bool matchActive = false;

    // Send packets
    void SendPacket(const void* data, size_t size, bool reliable = false);

    // Received packets since last frame
    struct NetworkEvent {
        uint32_t senderID;
        std::vector<uint8_t> data;
    };
    std::vector<NetworkEvent> GetIncomingEvents();

    std::string localUsername;
    int localSkinIndex;
    // TODO: Should be per-slot array instead of single value to support per-weapon skin selection.
    int localWeaponSkin;
    int localKills;
    int localDeaths;
    int localTeamID; // 0 = None, 1 = GR, 2 = BL
    std::string selectedMapName; // Map folder name from registry (e.g. "Forest")

    // Lobby Settings
    OnlineGameMode currentGameMode;
    int timeLimit;
    int killLimit;
    int roundLimit;

    // Match state (host-authoritative, mirrored on clients).
    // teamScores[i] = total rounds won (Elimination) or kill count (TDM) for team i.
    int teamScores[3];
    int currentRoundNumber;     // 0 = match hasn't started, 1..N = current round
    int lastRoundWinnerID;      // 0 = draw, 1 = GR, 2 = BL
    int lastRoundGRScore;
    int lastRoundBLScore;
    enum class RoundPhase { IDLE, IN_ROUND, ROUND_OVER, MATCH_OVER };
    RoundPhase roundPhase;

    void RequestTeamChange(int teamID);
    void UpdateLobbySettings(OnlineGameMode mode, int time, int kills, int rounds);

    // Zero all match-state counters. Call from Disconnect and at match end.
    void ResetMatch();
    int  GetTeamScore(int teamID) const;

private:
    NetworkManager();
    ~NetworkManager();

    ENetHost* host;
    ENetPeer* serverPeer; // Only used by clients to point to the server

    bool isHost;
    bool isConnected;
    uint32_t localPlayerID;   // Authoritative playerID for this client (assigned by host; 0 = host)
    bool waitingForAssignment; // Set to true after connecting as client, waiting for ID assignment from host

    // Async join state
    JoinState joinState;
    ENetPeer* pendingPeer;      // Peer being connected to (async join)
    float joinTimeoutTimer;     // Seconds remaining before async join times out

    // Player tracking for lobby. The PlayerInfo.peerID field is repurposed to mean
    // "authoritative playerID assigned by the host". It is NEVER set from ENet incomingPeerID
    // on the client side, and is always assigned by the host.
    struct PlayerInfo {
        uint32_t peerID;        // Authoritative playerID
        std::string username;
        int charSkin;
        int weaponSkin;         // Weapon skin ID (0 = default)
        int teamID;
        bool isReady;

        PlayerInfo(uint32_t id, const std::string& name, int skin, int weaponSkin, int team)
            : peerID(id), username(name), charSkin(skin), weaponSkin(weaponSkin), teamID(team), isReady(false) {}
    };
    std::vector<PlayerInfo> players;

    // Host-only: monotonically increasing playerID counter. IDs start at 1 (host is always 0).
    // Once assigned, an ID is never reused within a session — disconnects do not free the ID.
    uint32_t nextPlayerID;

    // Host-only: maps the host-side ENet incomingPeerID (transport-layer index) to an
    // authoritative playerID. This is the ONLY place ENet internals are used to look up a
    // player. Game code must always work with the playerID.
    // Keyed by (uint16_t)incomingPeerID; values are 0 if the slot is unused.
    std::vector<uint16_t> incomingPeerIDToPlayerID;

    // Player positions for proximity voice chat (world coordinates)
    std::map<uint32_t, Vector2> playerPositions;

    std::vector<NetworkEvent> incomingEvents;

public:
    // Get player list (for lobby UI)
    const std::vector<PlayerInfo>& GetPlayers() const { return players; }

    // Add or update a player entry by authoritative playerID. Idempotent.
    // If a player with the same ID already exists, updates username/skin/weaponSkin. Otherwise inserts.
    // Never creates duplicates and never generates a new ID.
    void UpsertPlayer(uint32_t playerID, const std::string& username, int charSkin, int weaponSkin, int teamID);

    // Remove a player entry by authoritative playerID. Returns true if removed.
    bool RemovePlayer(uint32_t playerID);

    // Set a player's team ID (host-only helper). Mutates PlayerInfo and broadcasts
    // a PacketPlayerTeamChange to every connected peer.
    void SetPlayerTeam(uint32_t playerID, int teamID);

    // Find a player entry by authoritative playerID. Returns nullptr if not present.
    const PlayerInfo* FindPlayer(uint32_t playerID) const;

    // Host-only: assign the next authoritative playerID. Never returns 0 (reserved for host).
    uint32_t AllocatePlayerID();

    // Host-only: look up the authoritative playerID for a given host-side incomingPeerID.
    // Returns 0 if the peer is not registered (e.g., not yet assigned).
    uint32_t GetPlayerIDForIncomingPeerID(uint16_t incomingPeerID) const;

    // Host-only: register which incomingPeerID maps to which authoritative playerID.
    void RegisterPeerMapping(uint16_t incomingPeerID, uint32_t playerID);
    void UnregisterPeerMapping(uint16_t incomingPeerID);
};

#endif // NETWORK_MANAGER_H
