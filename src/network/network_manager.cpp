#include "../rules/match_rules.h"
#include <cmath>
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include "network_manager.h"
#include "../constants.h"
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <cctype>
#include "../voice/proximity_voice_chat.h"


// Voice chat integration is handled by the main loop.
// NetworkManager must not depend on extern globals defined elsewhere.


NetworkManager::NetworkManager()
    : host(nullptr),
      serverPeer(nullptr),
      isHost(false),
      isConnected(false),
      localPlayerID(0),
      localSkinIndex(0),
      localWeaponSkin(1), // Default to AK47
      localKills(0),
      localDeaths(0),
      localTeamID(0), // 0 means not yet assigned (or none in FFA)
      selectedMapName("Forest"),
      currentGameMode(OnlineGameMode::FREE_FOR_ALL),
      timeLimit(0),    // Default to Kill limit active
      killLimit(40),
      roundLimit(8),
      teamScores{0, 0, 0},
      currentRoundNumber(0),
      lastRoundWinnerID(0),
      lastRoundGRScore(0),
      lastRoundBLScore(0),
      roundPhase(RoundPhase::IDLE),
      waitingForAssignment(false),
      nextPlayerID(1), // Host is always 0; clients start at 1, monotonically increasing
      joinState(JoinState::IDLE),
      pendingPeer(nullptr),
      joinTimeoutTimer(0.0f)
{
    localUsername = "Player";
}

NetworkManager::~NetworkManager() {
    Shutdown();
}

NetworkManager& NetworkManager::GetInstance() {
    static NetworkManager instance;
    return instance;
}

bool NetworkManager::Initialize() {
    // Initialize Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return false;
    }

    if (enet_initialize() != 0) {
        std::cerr << "An error occurred while initializing ENet." << std::endl;
        WSACleanup();
        return false;
    }
 
    atexit(enet_deinitialize);
    return true;
}

void NetworkManager::Shutdown() {
    Disconnect();
    WSACleanup();
}

uint32_t NetworkManager::AllocatePlayerID() {
    uint32_t id = nextPlayerID++;
    return id;
}

uint32_t NetworkManager::GetPlayerIDForIncomingPeerID(uint16_t incomingPeerID) const {
    if (incomingPeerID >= incomingPeerIDToPlayerID.size()) {
        return 0;
    }
    return incomingPeerIDToPlayerID[incomingPeerID];
}

void NetworkManager::RegisterPeerMapping(uint16_t incomingPeerID, uint32_t playerID) {
    if (incomingPeerIDToPlayerID.size() <= incomingPeerID) {
        incomingPeerIDToPlayerID.resize(incomingPeerID + 1, 0);
    }
    incomingPeerIDToPlayerID[incomingPeerID] = (uint16_t)playerID;
}

void NetworkManager::UnregisterPeerMapping(uint16_t incomingPeerID) {
    if (incomingPeerID < incomingPeerIDToPlayerID.size()) {
        incomingPeerIDToPlayerID[incomingPeerID] = 0;
    }
}

void NetworkManager::UpsertPlayer(uint32_t playerID, const std::string& username, int charSkin, int weaponSkin, int teamID) {
    for (auto& p : players) {
        if (p.peerID == playerID) {
            p.username  = username;
            p.charSkin  = charSkin;
            p.weaponSkin = weaponSkin;
            p.teamID    = teamID;
            return;
        }
    }
    players.push_back(PlayerInfo(playerID, username, charSkin, weaponSkin, teamID));
}

bool NetworkManager::RemovePlayer(uint32_t playerID) {
    auto it = std::remove_if(players.begin(), players.end(),
        [playerID](const PlayerInfo& p) { return p.peerID == playerID; });
    if (it == players.end()) return false;
    players.erase(it, players.end());
    return true;
}

const NetworkManager::PlayerInfo* NetworkManager::FindPlayer(uint32_t playerID) const {
    for (const auto& p : players) {
        if (p.peerID == playerID) return &p;
    }
    return nullptr;
}

bool NetworkManager::HostRoom(int port) {
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    host = enet_host_create(&address, 15, 2, 0, 0);
    if (host == nullptr) {
        std::cerr << "An error occurred while trying to create an ENet server host." << std::endl;
        return false;
    }

    isHost = true;
    isConnected = true;
    localPlayerID = 0; 

    UpsertPlayer(0, localUsername, localSkinIndex, localWeaponSkin, localTeamID);


    std::cout << "Hosting room on port " << port << std::endl;
    return true;
}

bool NetworkManager::ParseServerAddress(const std::string& address, std::string& host, int& port,
                                          int defaultPort) {
    host = address;
    port = defaultPort;

    while (!host.empty() && std::isspace(static_cast<unsigned char>(host.front()))) {
        host.erase(host.begin());
    }
    while (!host.empty() && std::isspace(static_cast<unsigned char>(host.back()))) {
        host.pop_back();
    }

    if (host.empty()) {
        return false;
    }

    const size_t colonPos = host.rfind(':');
    if (colonPos != std::string::npos && colonPos > 0) {
        const std::string portStr = host.substr(colonPos + 1);
        if (!portStr.empty()) {
            bool allDigits = true;
            for (char c : portStr) {
                if (!std::isdigit(static_cast<unsigned char>(c))) {
                    allDigits = false;
                    break;
                }
            }
            if (allDigits) {
                const int parsedPort = std::atoi(portStr.c_str());
                if (parsedPort > 0 && parsedPort <= 65535) {
                    port = parsedPort;
                    host = host.substr(0, colonPos);
                }
            }
        }
    }

    return !host.empty();
}

bool NetworkManager::JoinRoom(const std::string& address, int defaultPort) {
    std::string hostName;
    int port = defaultPort;
    if (!ParseServerAddress(address, hostName, port, defaultPort)) {
        std::cerr << "Invalid server address: " << address << std::endl;
        return false;
    }

    ENetHost* clientHost = enet_host_create(nullptr, 1, 2, 0, 0);
    if (clientHost == nullptr) {
        std::cerr << "An error occurred while trying to create an ENet client host." << std::endl;
        return false;
    }

    ENetAddress enetAddress;
    if (enet_address_set_host(&enetAddress, hostName.c_str()) != 0) {
        std::cerr << "Failed to resolve host: " << hostName << std::endl;
        enet_host_destroy(clientHost);
        return false;
    }
    enetAddress.port = static_cast<enet_uint16>(port);

    ENetPeer* peer = enet_host_connect(clientHost, &enetAddress, 2, 0);
    if (peer == nullptr) {
        std::cerr << "No available peers for initiating an ENet connection." << std::endl;
        enet_host_destroy(clientHost);
        return false;
    }

    ENetEvent event;
    // Use a longer timeout for internet connections (e.g. PlayIt.gg tunnels) which
    // can have higher latency than a direct LAN connection.
    if (enet_host_service(clientHost, &event, 10000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT) {
        std::cout << "Connection to " << hostName << ":" << port << " succeeded." << std::endl;
        host = clientHost;
        serverPeer = peer;
        isHost = false;
        isConnected = true;
        waitingForAssignment = true;
        localPlayerID = 0;

        // Configure peer timeouts for internet play via tunnels (e.g. PlayIt.gg).
        // Default ENet timeouts are too aggressive (~5s) for tunneled connections.
        // timeoutLimit=32 retries, timeoutMinimum=5000ms, timeoutMaximum=30000ms
        enet_peer_timeout(serverPeer, 32, 5000, 30000);

        return true;
    }

    enet_peer_reset(peer);
    std::cerr << "Connection to " << hostName << ":" << port << " failed." << std::endl;
    enet_host_destroy(clientHost);
    return false;
}

// ---------------------------------------------------------------------------
// Async (non-blocking) join implementation
// ---------------------------------------------------------------------------
bool NetworkManager::BeginJoinRoom(const std::string& address, int defaultPort) {
    // Sanity: only start if we're idle
    if (joinState == JoinState::CONNECTING) return false;

    std::string hostName;
    int port = defaultPort;
    if (!ParseServerAddress(address, hostName, port, defaultPort)) {
        std::cerr << "[Async] Invalid server address: " << address << std::endl;
        joinState = JoinState::FAILED;
        return false;
    }

    ENetHost* clientHost = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!clientHost) {
        std::cerr << "[Async] Failed to create ENet client host." << std::endl;
        joinState = JoinState::FAILED;
        return false;
    }

    ENetAddress enetAddress;
    if (enet_address_set_host(&enetAddress, hostName.c_str()) != 0) {
        std::cerr << "[Async] Failed to resolve host: " << hostName << std::endl;
        enet_host_destroy(clientHost);
        joinState = JoinState::FAILED;
        return false;
    }
    enetAddress.port = static_cast<enet_uint16>(port);

    ENetPeer* peer = enet_host_connect(clientHost, &enetAddress, 2, 0);
    if (!peer) {
        std::cerr << "[Async] No available peers." << std::endl;
        enet_host_destroy(clientHost);
        joinState = JoinState::FAILED;
        return false;
    }

    // Store the in-progress host/peer for PollJoinRoom()
    host = clientHost;
    pendingPeer = peer;
    joinState = JoinState::CONNECTING;
    joinTimeoutTimer = 15.0f; // 15 second timeout for internet tunnels
    isHost = false;
    isConnected = false;

    std::cout << "[Async] Connecting to " << hostName << ":" << port << " ..." << std::endl;
    return true;
}

NetworkManager::JoinState NetworkManager::PollJoinRoom() {
    if (joinState != JoinState::CONNECTING || !host) return joinState;

    // Decrease timeout
    // NOTE: We don't have deltaTime here so we use a small fixed step.
    // Caller should call this once per frame (~60fps), so each call ~= 16ms.
    joinTimeoutTimer -= (1.0f / 60.0f);
    if (joinTimeoutTimer <= 0.0f) {
        std::cerr << "[Async] Connection timed out." << std::endl;
        if (pendingPeer) enet_peer_reset(pendingPeer);
        enet_host_destroy(host);
        host = nullptr;
        pendingPeer = nullptr;
        joinState = JoinState::FAILED;
        return joinState;
    }

    // Non-blocking poll (timeout=0 means don't wait)
    ENetEvent event;
    int result = enet_host_service(host, &event, 0);
    if (result > 0) {
        if (event.type == ENET_EVENT_TYPE_CONNECT) {
            std::cout << "[Async] Connected!" << std::endl;
            serverPeer = pendingPeer;
            pendingPeer = nullptr;
            isConnected = true;
            waitingForAssignment = true;
            localPlayerID = 0;
            // Apply generous timeout for tunnel connections
            enet_peer_timeout(serverPeer, 32, 5000, 30000);
            joinState = JoinState::CONNECTED;
        } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
            std::cerr << "[Async] Connection refused / disconnected." << std::endl;
            enet_host_destroy(host);
            host = nullptr;
            pendingPeer = nullptr;
            joinState = JoinState::FAILED;
        } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(event.packet); // Discard stray packets during handshake
        }
    }

    return joinState;
}


void NetworkManager::Disconnect() {
    if (host != nullptr) {
        if (!isHost && serverPeer != nullptr) {
            enet_peer_disconnect(serverPeer, 0);

            enet_host_flush(host);
            enet_peer_reset(serverPeer);
        }

        enet_host_destroy(host);
        host = nullptr;
        serverPeer = nullptr;
    }
    isConnected = false;
    isHost = false;
    waitingForAssignment = false;
    localPlayerID = 0;
    players.clear();
    incomingEvents.clear();
    incomingPeerIDToPlayerID.clear();
    nextPlayerID = 1;
    joinState = JoinState::IDLE;
    pendingPeer = nullptr;
    joinTimeoutTimer = 0.0f;
    ResetMatch();
    matchActive = false;
}

void NetworkManager::ResetMatch() {
    teamScores[0] = teamScores[1] = teamScores[2] = 0;
    currentRoundNumber = 0;
    lastRoundWinnerID = 0;
    lastRoundGRScore = 0;
    lastRoundBLScore = 0;
    roundPhase = RoundPhase::IDLE;
}

int NetworkManager::GetTeamScore(int teamID) const {
    if (teamID < 0 || teamID >= 3) return 0;
    return teamScores[teamID];
}

void NetworkManager::RequestTeamChange(int teamID) {
    // Mutate local state immediately so the client UI reflects the change without a round-trip.
    localTeamID = teamID;
    for (auto& pl : players) {
        if (pl.peerID == localPlayerID) {
            pl.teamID = teamID;
            break;
        }
    }

    PacketPlayerTeamChange pkt{};
    pkt.header.type     = PacketType::PLAYER_TEAM_CHANGE;
    pkt.header.playerID = localPlayerID;
    pkt.teamID          = teamID;
    SendPacket(&pkt, sizeof(pkt), true);
}

void NetworkManager::SetPlayerTeam(uint32_t playerID, int teamID) {
    if (!isHost || matchActive || teamID < 1 || teamID > 2) return;
    // Host-only: mutate the entry then broadcast.
    for (auto& pl : players) {
        if (pl.peerID == playerID) {
            pl.teamID = teamID;
            break;
        }
    }
    if (playerID == localPlayerID) {
        localTeamID = teamID;
    }

    PacketPlayerTeamChange pkt{};
    pkt.header.type     = PacketType::PLAYER_TEAM_CHANGE;
    pkt.header.playerID = playerID;
    pkt.teamID          = teamID;
    SendPacket(&pkt, sizeof(pkt), true);
}

void NetworkManager::UpdateLobbySettings(OnlineGameMode mode, int time, int kills, int rounds) {
    if (!isHost || matchActive) return;
    if ((int)mode < 0 || (int)mode > 3) return;
    if (mode != currentGameMode) {
        if (mode == OnlineGameMode::ELIMINATION) { time = 120; kills = 0; }
        else { time = 300; kills = 0; }
        if (mode==OnlineGameMode::DEATHMATCH) rounds=5;
    }
    rounds = std::clamp(rounds, 1, 30);
    time = std::clamp(time, 0, 1800);
    kills = std::clamp(kills, 0, 200);
    if (mode != OnlineGameMode::ELIMINATION && time == 0 && kills == 0) kills = 40;
    currentGameMode = mode;
    timeLimit       = time;
    killLimit       = kills;
    roundLimit      = rounds;

    PacketLobbySettings pkt{};
    pkt.header.type     = PacketType::LOBBY_SETTINGS;
    pkt.header.playerID = 0; // from host
    pkt.gameMode        = mode;
    pkt.timeLimit       = time;
    pkt.killLimit       = kills;
    pkt.roundLimit      = rounds;
    SendPacket(&pkt, sizeof(pkt), true);
}

void NetworkManager::Update() {
    if (!host) return;

    incomingEvents.clear();
    ENetEvent event;

    // Process all pending events
    while (enet_host_service(host, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                std::cout << "A new client connected from "
                          << event.peer->address.host << ":"
                          << event.peer->address.port << std::endl;
                if (isHost) {
                    if (matchActive) { enet_peer_disconnect_now(event.peer, 1); break; }

                    uint16_t incomingPeerID = event.peer->incomingPeerID;
                    uint32_t newPlayerID = AllocatePlayerID();
                    RegisterPeerMapping(incomingPeerID, newPlayerID);
                    UpsertPlayer(newPlayerID, "Unknown", 1, 0, 0);

                    // Auto-assign late joiners to the smaller team in team modes so
                    // matches stay balanced. FFA leaves them at teamID 0 (unassigned).
                    if (currentGameMode != OnlineGameMode::FREE_FOR_ALL) {
                        int grCount = 0, blCount = 0;
                        for (const auto& pl : players) {
                            if (pl.teamID == 1) grCount++;
                            else if (pl.teamID == 2) blCount++;
                        }
                        int assignedTeam = (grCount <= blCount) ? 1 : 2;
                        SetPlayerTeam(newPlayerID, assignedTeam);
                    }

                    // Configure generous peer timeouts for internet/tunnel connections.
                    enet_peer_timeout(event.peer, 32, 5000, 30000);

                    PacketIDAssignment idPkt{};
                    idPkt.header.type = PacketType::ID_ASSIGNMENT;
                    idPkt.header.playerID = 0; // From host
                    idPkt.assignedID = newPlayerID;
                    ENetPacket* idPktPtr = enet_packet_create(&idPkt, sizeof(idPkt), ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(event.peer, 0, idPktPtr);

                    // Also send current lobby settings to the new joiner
                    PacketLobbySettings settingsPkt{};
                    settingsPkt.header.type     = PacketType::LOBBY_SETTINGS;
                    settingsPkt.header.playerID = 0;
                    settingsPkt.gameMode        = currentGameMode;
                    settingsPkt.timeLimit        = timeLimit;
                    settingsPkt.killLimit        = killLimit;
                    settingsPkt.roundLimit       = roundLimit;
                    ENetPacket* sp = enet_packet_create(&settingsPkt, sizeof(settingsPkt), ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(event.peer, 0, sp);

                    for (const auto& existingPlayer : players) {
                        if (existingPlayer.peerID == newPlayerID) continue; // skip self
                        PacketPlayerConnect connectPacket{};
                        connectPacket.header.type = PacketType::PLAYER_CONNECT;
                        connectPacket.header.playerID = existingPlayer.peerID;
                        strncpy(connectPacket.username, existingPlayer.username.c_str(),
                        sizeof(connectPacket.username) - 1);
                        connectPacket.username[sizeof(connectPacket.username) - 1] = '\0';
                        connectPacket.charSkin   = existingPlayer.charSkin;
                        connectPacket.weaponSkin = existingPlayer.weaponSkin;
                        connectPacket.teamID     = existingPlayer.teamID;

                        ENetPacket* p = enet_packet_create(&connectPacket, sizeof(connectPacket),
                                                            ENET_PACKET_FLAG_RELIABLE);
                        enet_peer_send(event.peer, 0, p);
                    }
                }
                // We do nothing special on the client side for CONNECT events.
                break;
            }

            case ENET_EVENT_TYPE_RECEIVE: {
                NetworkEvent netEvent;
                if (isHost) {
                    // Translate the transport-layer incomingPeerID to an authoritative playerID.
                    uint16_t incomingPeerID = event.peer->incomingPeerID;
                    uint32_t senderPlayerID = GetPlayerIDForIncomingPeerID(incomingPeerID);
                    // If the sender isn't registered yet, drop the packet silently.
                    // (The ID_ASSIGNMENT is sent BEFORE we expect any other traffic from
                    // a brand-new client, but we still tolerate the race.)
                    if (senderPlayerID == 0) {
                        enet_packet_destroy(event.packet);
                        break;
                    }
                    netEvent.senderID = senderPlayerID;
                } else {
                    // Client: anything we receive is from the host.
                    netEvent.senderID = 0;
                }

                netEvent.data.resize(event.packet->dataLength);
                std::memcpy(netEvent.data.data(), event.packet->data, event.packet->dataLength);

                if (netEvent.data.size() < sizeof(PacketHeader)) { enet_packet_destroy(event.packet); break; }
                auto* wire = reinterpret_cast<PacketHeader*>(netEvent.data.data());
                if (isHost) {
                    bool hostOnly = wire->type == PacketType::GAME_START || wire->type == PacketType::MAP_CHANGED ||
                        wire->type == PacketType::LOBBY_SETTINGS || wire->type == PacketType::ROUND_START ||
                        wire->type == PacketType::ROUND_END || wire->type == PacketType::MATCH_STATE ||
                        wire->type == PacketType::RETURN_LOBBY || wire->type == PacketType::PLAYER_DISCONNECT || wire->type == PacketType::FLAG_STATE;
                    if (hostOnly) { enet_packet_destroy(event.packet); break; }
                    wire->playerID = netEvent.senderID;
                    if (wire->type==PacketType::FLAG_INTERACT && (!matchActive || currentGameMode!=OnlineGameMode::DEATHMATCH || netEvent.data.size()!=sizeof(PacketHeader))) {
                        enet_packet_destroy(event.packet); break;
                    }
                    if (matchActive && (wire->type == PacketType::PLAYER_TEAM_CHANGE || wire->type == PacketType::PLAYER_READY)) {
                        enet_packet_destroy(event.packet); break;
                    }
                    if (wire->type == PacketType::PLAYER_DAMAGE) {
                        if (netEvent.data.size() < sizeof(PacketPlayerDamage)) { enet_packet_destroy(event.packet); break; }
                        auto* d = reinterpret_cast<PacketPlayerDamage*>(wire);
                        auto* shooter = FindPlayer(wire->playerID); auto* victim = FindPlayer(d->targetPlayerID);
                        if (!shooter || !victim || !std::isfinite(d->damageAmount) || d->damageAmount <= 0 || d->damageAmount > 40 ||
                            Rules::FriendlyFire(currentGameMode != OnlineGameMode::FREE_FOR_ALL, shooter->teamID, victim->teamID)) {
                            enet_packet_destroy(event.packet); break;
                        }
                    }
                    if (wire->type == PacketType::PLAYER_GRENADE) {
                        if (!matchActive || netEvent.data.size() != sizeof(PacketPlayerGrenade)) { enet_packet_destroy(event.packet); break; }
                        auto* g = reinterpret_cast<PacketPlayerGrenade*>(wire);
                        float dx = g->target.x-g->start.x, dy = g->target.y-g->start.y;
                        if (g->kind > 1 || !std::isfinite(g->start.x) || !std::isfinite(g->start.y) ||
                            !std::isfinite(g->target.x) || !std::isfinite(g->target.y) || dx*dx+dy*dy > 651*651) {
                            enet_packet_destroy(event.packet); break;
                        }
                    }
                    if (wire->type == PacketType::PLAYER_KILLED) {
                        if (netEvent.data.size() < sizeof(PacketPlayerKilled)) { enet_packet_destroy(event.packet); break; }
                        auto* k = reinterpret_cast<PacketPlayerKilled*>(wire);
                        if (k->victimPlayerID != wire->playerID || !FindPlayer(k->killerPlayerID)) { enet_packet_destroy(event.packet); break; }
                    }
                    if (wire->type == PacketType::PLAYER_RESPAWN && matchActive && currentGameMode == OnlineGameMode::ELIMINATION) {
                        enet_packet_destroy(event.packet); break;
                    }
                }

                // Parse the packet type
                if (netEvent.data.size() >= sizeof(PacketType)) {
                    PacketType packetType = static_cast<PacketType>(netEvent.data[0]);

                    if (packetType == PacketType::ID_ASSIGNMENT) {

                        // CLIENT side: host just granted us an authoritative playerID.
                        if (netEvent.data.size() >= sizeof(PacketIDAssignment)) {
                            if (!isHost) {
                                PacketIDAssignment* idPkt =
                                    reinterpret_cast<PacketIDAssignment*>(netEvent.data.data());
                                localPlayerID = idPkt->assignedID;
                                waitingForAssignment = false;
                                std::cout << "[Client] Assigned playerID=" << localPlayerID << std::endl;

                                // Send a PLAYER_CONNECT announcement back to the host with our
                                // name and skin. The host will rebroadcast to everyone (so they
                                // can update our entry from "Unknown" to the real name).
                                // The header.playerID is the authoritative one the host just gave us.
                                PacketPlayerConnect announce{};
                                announce.header.type = PacketType::PLAYER_CONNECT;
                                announce.header.playerID = localPlayerID;
                                strncpy(announce.username, localUsername.c_str(),
                                        sizeof(announce.username) - 1);
                                announce.username[sizeof(announce.username) - 1] = '\0';
                                announce.charSkin   = localSkinIndex;
                                announce.weaponSkin = localWeaponSkin;
                                announce.teamID     = localTeamID;
                                ENetPacket* p = enet_packet_create(
                                    &announce, sizeof(announce), ENET_PACKET_FLAG_RELIABLE);
                                enet_peer_send(serverPeer, 0, p);

                                // Add ourselves to our own local player list too.
                                UpsertPlayer(localPlayerID, localUsername, localSkinIndex, localWeaponSkin, localTeamID);

                            }
                            // Host ignores this packet type.
                        }
                        // Do not enqueue this packet for the game — it's network-manager-internal.
                        enet_packet_destroy(event.packet);
                        break;
                    }

                    if (packetType == PacketType::PLAYER_CONNECT) {
                        // PLAYER_CONNECT is only ever emitted by the HOST. Clients never
                        // generate their own. Both host and clients process it the same way:
                        // add or update a player entry by authoritative playerID.
                        if (netEvent.data.size() >= sizeof(PacketPlayerConnect)) {
                            PacketPlayerConnect* connectPacket =
                                reinterpret_cast<PacketPlayerConnect*>(netEvent.data.data());

                            uint32_t playerID  = connectPacket->header.playerID;
                            connectPacket->username[sizeof(connectPacket->username)-1] = '\0';
                            std::string uname(connectPacket->username);
                            int skin       = connectPacket->charSkin;
                            int weaponSkin = connectPacket->weaponSkin;
                            int teamID = connectPacket->teamID;
                            connectPacket->username[sizeof(connectPacket->username)-1] = '\0';
                            if (isHost) {
                                if (auto* existing = FindPlayer(playerID)) teamID = existing->teamID;
                                connectPacket->teamID = teamID;
                            } else if (playerID == localPlayerID) localTeamID = teamID;

                            // Dedupe + update in one shot.
                            UpsertPlayer(playerID, uname, skin, weaponSkin, teamID);

                            // Initialize player position for proximity voice chat
                            // Default position (0,0) - will be updated by first PLAYER_UPDATE
                            playerPositions[playerID] = {0.0f, 0.0f};

                            if (isHost) {
                                // Host: rebroadcast to all OTHER connected clients so they
                                // also upsert this player. We rewrite the header to use the
                                // authoritative playerID (which is already in the packet),
                                // and we never forward the original sender's other fields.
                                for (size_t i = 0; i < host->peerCount; ++i) {
                                    ENetPeer* peer = &host->peers[i];

                                    if (peer->state != ENET_PEER_STATE_CONNECTED) continue;
                                    ENetPacket* p = enet_packet_create(
                                        netEvent.data.data(), netEvent.data.size(),
                                        ENET_PACKET_FLAG_RELIABLE);
                                    enet_peer_send(peer, 0, p);
                                }
                            }
                        }
                    } else if (packetType == PacketType::PLAYER_READY) {
                        if (netEvent.data.size() >= sizeof(PacketPlayerReady)) {
                            PacketPlayerReady* readyPacket =
                                reinterpret_cast<PacketPlayerReady*>(netEvent.data.data());

                            uint32_t pid = readyPacket->header.playerID;
                            for (auto& player : players) {
                                if (player.peerID == pid) {
                                    player.isReady = readyPacket->isReady;
                                    break;
                                }
                            }

                            if (isHost) {
                                for (size_t i = 0; i < host->peerCount; ++i) {
                                    ENetPeer* peer = &host->peers[i];
                                    if (peer == event.peer) continue;
                                    if (peer->state != ENET_PEER_STATE_CONNECTED) continue;
                                    ENetPacket* p = enet_packet_create(
                                        netEvent.data.data(), netEvent.data.size(),
                                        ENET_PACKET_FLAG_RELIABLE);
                                    enet_peer_send(peer, 0, p);
                                }
                            }
                        }
                    } else if (packetType == PacketType::PLAYER_UPDATE) {
                        // PLAYER_UPDATE is sent by clients (their own state) and rebroadcast by
                        // the host to everyone else. The host's incomingPeerID-to-playerID map
                        // ensures the rebroadcasted packet always carries the correct
                        // authoritative playerID.
                        // Update local player info with skin data.
                        if (netEvent.data.size() >= sizeof(PacketPlayerUpdate)) {
                            PacketPlayerUpdate* updatePacket =
                                reinterpret_cast<PacketPlayerUpdate*>(netEvent.data.data());
                            uint32_t playerID  = updatePacket->header.playerID;
                            // Preserve existing username and teamID if known.
                            std::string username;
                            int existingTeamID = 0;
                            if (auto* p = FindPlayer(playerID)) {
                                username      = p->username;
                                existingTeamID = p->teamID;
                            }
                            int charSkin   = updatePacket->charSkin;
                            int weaponSkin = updatePacket->weaponSkin;
                            UpsertPlayer(playerID, username, charSkin, weaponSkin, existingTeamID);

                            // Update player position for proximity voice chat
                            playerPositions[playerID] = updatePacket->position;
                        }
                        if (isHost) {
                            for (size_t i = 0; i < host->peerCount; ++i) {
                                ENetPeer* peer = &host->peers[i];
                                if (peer == event.peer) continue;
                                if (peer->state != ENET_PEER_STATE_CONNECTED) continue;
                                ENetPacket* p = enet_packet_create(
                                    netEvent.data.data(), netEvent.data.size(), 0); // unreliable
                                enet_peer_send(peer, 0, p);
                            }
                        }
                    } else if (packetType == PacketType::PLAYER_SHOOT || packetType == PacketType::PLAYER_GRENADE) {
                        if (isHost) {
                            for (size_t i = 0; i < host->peerCount; ++i) {
                                ENetPeer* peer = &host->peers[i];
                                if (peer == event.peer) continue;
                                if (peer->state != ENET_PEER_STATE_CONNECTED) continue;
                                ENetPacket* p = enet_packet_create(
                                    netEvent.data.data(), netEvent.data.size(),
                                    ENET_PACKET_FLAG_RELIABLE);
                                enet_peer_send(peer, 0, p);
                            }
                        }
                    } else if (packetType == PacketType::PLAYER_DAMAGE) {
                        if (isHost) {
                            for (size_t i = 0; i < host->peerCount; ++i) {
                                ENetPeer* peer = &host->peers[i];
                                if (peer == event.peer) continue;
                                if (peer->state != ENET_PEER_STATE_CONNECTED) continue;
                                ENetPacket* p = enet_packet_create(
                                    netEvent.data.data(), netEvent.data.size(),
                                    ENET_PACKET_FLAG_RELIABLE);
                                enet_peer_send(peer, 0, p);
                            }
                        }
                    } else if (packetType == PacketType::PLAYER_KILLED) {
                        if (isHost) {
                            for (size_t i = 0; i < host->peerCount; ++i) {
                                ENetPeer* peer = &host->peers[i];
                                if (peer == event.peer) continue;
                                if (peer->state != ENET_PEER_STATE_CONNECTED) continue;
                                ENetPacket* p = enet_packet_create(
                                    netEvent.data.data(), netEvent.data.size(),
                                    ENET_PACKET_FLAG_RELIABLE);
                                enet_peer_send(peer, 0, p);
                            }
                        }
                    } else if (packetType == PacketType::PLAYER_RESPAWN) {
                        if (isHost) {
                            // Update player info from respawn packet
                            if (netEvent.data.size() >= sizeof(PacketPlayerRespawn)) {
                                PacketPlayerRespawn* respPacket =
                                    reinterpret_cast<PacketPlayerRespawn*>(netEvent.data.data());
                                uint32_t playerID  = respPacket->header.playerID;
                                int charSkin   = respPacket->charSkin;
                                int weaponSkin = respPacket->weaponSkin;
                                // Preserve existing username and teamID
                                const PlayerInfo* existing = FindPlayer(playerID);
                                std::string username = existing ? existing->username : "";
                                int existingTeamID   = existing ? existing->teamID   : 0;
                                UpsertPlayer(playerID, username, charSkin, weaponSkin, existingTeamID);


                                // Update player position for proximity voice chat
                                playerPositions[playerID] = respPacket->spawnPosition;
                            }
                            // Rebroadcast to other clients
                            for (size_t i = 0; i < host->peerCount; ++i) {
                                ENetPeer* peer = &host->peers[i];
                                if (peer == event.peer) continue;
                                if (peer->state != ENET_PEER_STATE_CONNECTED) continue;
                                ENetPacket* p = enet_packet_create(
                                    netEvent.data.data(), netEvent.data.size(),
                                    ENET_PACKET_FLAG_RELIABLE);
                                enet_peer_send(peer, 0, p);
                            }
                        }
                    } else if (packetType == PacketType::PLAYER_DISCONNECT) {
                        if (isHost) {
                            for (size_t i = 0; i < host->peerCount; ++i) {
                                ENetPeer* peer = &host->peers[i];
                                if (peer == event.peer) continue;
                                if (peer->state != ENET_PEER_STATE_CONNECTED) continue;
                                ENetPacket* p = enet_packet_create(
                                    netEvent.data.data(), netEvent.data.size(),
                                    ENET_PACKET_FLAG_RELIABLE);
                                enet_peer_send(peer, 0, p);
                            }
                        }
                    } else if (packetType == PacketType::LOBBY_SETTINGS) {
                        // Client-side apply of in-session settings rebroadcasts (the host already
                        // applies the same fields directly on the new-joiner path).
                        if (netEvent.data.size() >= sizeof(PacketLobbySettings)) {
                            PacketLobbySettings* p =
                                reinterpret_cast<PacketLobbySettings*>(netEvent.data.data());
                            currentGameMode = p->gameMode;
                            timeLimit       = p->timeLimit;
                            killLimit       = p->killLimit;
                            roundLimit      = p->roundLimit;
                        }
                    } else if (packetType == PacketType::PLAYER_TEAM_CHANGE) {
                        if (netEvent.data.size() >= sizeof(PacketPlayerTeamChange)) {
                            PacketPlayerTeamChange* p =
                                reinterpret_cast<PacketPlayerTeamChange*>(netEvent.data.data());
                            if (p->teamID < 1 || p->teamID > 2) { enet_packet_destroy(event.packet); break; }
                            uint32_t changedID = p->header.playerID;
                            for (auto& pl : players) {
                                if (pl.peerID == changedID) {
                                    pl.teamID = p->teamID;
                                    break;
                                }
                            }
                            if (changedID == localPlayerID) {
                                localTeamID = p->teamID;
                            }
                            if (isHost) {
                                // Rebroadcast to other peers (skip the sender).
                                for (size_t i = 0; i < host->peerCount; ++i) {
                                    ENetPeer* peer = &host->peers[i];
                                    if (peer == event.peer) continue;
                                    if (peer->state != ENET_PEER_STATE_CONNECTED) continue;
                                    ENetPacket* q = enet_packet_create(
                                        netEvent.data.data(), netEvent.data.size(),
                                        ENET_PACKET_FLAG_RELIABLE);
                                    enet_peer_send(peer, 0, q);
                                }
                            }
                        }
                    } else if (packetType == PacketType::ROUND_START) {
                        if (netEvent.data.size() >= sizeof(PacketRoundStart)) {
                            PacketRoundStart* p =
                                reinterpret_cast<PacketRoundStart*>(netEvent.data.data());
                            currentRoundNumber = p->roundNumber;
                            roundPhase = RoundPhase::IN_ROUND;
                        }
                    } else if (packetType == PacketType::ROUND_END) {
                        if (netEvent.data.size() >= sizeof(PacketRoundEnd)) {
                            PacketRoundEnd* p =
                                reinterpret_cast<PacketRoundEnd*>(netEvent.data.data());
                            lastRoundWinnerID = p->winningTeamID;
                            lastRoundGRScore  = p->grScore;
                            lastRoundBLScore  = p->blScore;
                            // The host already updated teamScores before broadcasting.
                            // Mirror on the client side so score queries stay consistent.
                            if (!isHost) {
                                teamScores[1] = p->grScore;
                                teamScores[2] = p->blScore;
                            }
                            roundPhase = RoundPhase::ROUND_OVER;
                        }
                    } else if (packetType == PacketType::GAME_START) {
                        matchActive = true;
                        localKills = localDeaths = 0;
                    } else if (packetType == PacketType::RETURN_LOBBY) {
                        matchActive = false;
                    } else if (packetType == PacketType::VOICE_DATA) {
                        // Voice data packets are variable-length: header + seq + frameSize + only
                        // the actually-encoded audio bytes (NOT the full MAX_OPUS_FRAME_SIZE buffer).
                        // Validate against the real minimum header size first, then confirm the
                        // declared frameSize actually fits within what we received.
                        constexpr size_t kVoiceHeaderSize =
                            sizeof(PacketHeader) + sizeof(uint16_t) + sizeof(uint16_t);
                        if (netEvent.data.size() >= kVoiceHeaderSize) {
                            const PacketVoiceData* voicePacket =
                                reinterpret_cast<const PacketVoiceData*>(netEvent.data.data());

                            if (netEvent.data.size() >= kVoiceHeaderSize + voicePacket->frameSize) {
                                uint32_t speakerID = voicePacket->header.playerID;

                                // Echo prevention: don't play back our own voice reflected from host
                                if (speakerID != localPlayerID) {
                                    // If host, rebroadcast this voice packet to all other connected clients
                                    if (isHost) {
                                        for (size_t i = 0; i < host->peerCount; ++i) {
                                            ENetPeer* peer = &host->peers[i];
                                            if (peer == event.peer) continue;
                                            if (peer->state != ENET_PEER_STATE_CONNECTED) continue;
                                            ENetPacket* p = enet_packet_create(
                                                netEvent.data.data(), netEvent.data.size(), 0);
                                            enet_peer_send(peer, 0, p);
                                        }
                                    }

                                    // Check proximity before processing voice locally
                                    bool shouldProcess = false;

                                    // Get speaker position from our tracking
                                    auto senderPosIt = playerPositions.find(speakerID);
                                    Vector2 senderPos = {0.0f, 0.0f};
                                    bool senderPosValid = false;
                                    if (senderPosIt != playerPositions.end()) {
                                        senderPos = senderPosIt->second;
                                        senderPosValid = true;
                                    }

                                    // Get local player position from gameplay screen
                                    Vector2 localPos = {0.0f, 0.0f};
                                    bool localPosValid = false;

                                    // Try to get local player position from gameplay screen
                                    extern bool (*getLocalPlayerPosCallback)(Vector2& outPos);
                                    if (getLocalPlayerPosCallback && getLocalPlayerPosCallback(localPos)) {
                                        localPosValid = true;
                                    }

                                    // If we have both positions, check distance
                                    if (localPosValid && senderPosValid) {
                                        float dx = senderPos.x - localPos.x;
                                        float dy = senderPos.y - localPos.y;
                                        float distanceSq = dx*dx + dy*dy;
                                        float hearingDistanceSq = 800.0f * 800.0f; // 800 units hearing range

                                        if (distanceSq <= hearingDistanceSq) {
                                            shouldProcess = true;
                                        }
                                    }
                                    // If we can't get positions for some reason, process the packet anyway
                                    else {
                                        shouldProcess = true;
                                    }

                                    // Forward to proximity voice chat system for processing if within range
                                    if (shouldProcess) {
                                        extern ProximityVoiceChat proximityVoiceChat;
                                        extern bool proximityVoiceInitialized;
                                        if (proximityVoiceInitialized) {
                                            proximityVoiceChat.processVoicePacket(voicePacket, speakerID);
                                        }
                                    }
                                }
                            }
                        }
                        enet_packet_destroy(event.packet);
                        break;
                    }
                }
                incomingEvents.push_back(netEvent);

                enet_packet_destroy(event.packet);
                break;
            }

            case ENET_EVENT_TYPE_DISCONNECT:
                std::cout << "Client disconnected." << std::endl;
                if (isHost) {
                    // Translate incomingPeerID -> authoritative playerID, then remove.
                    uint16_t incomingPeerID = event.peer->incomingPeerID;
                    uint32_t disconnectedPlayerID = GetPlayerIDForIncomingPeerID(incomingPeerID);
                    if (disconnectedPlayerID != 0) {
                        RemovePlayer(disconnectedPlayerID);
                        UnregisterPeerMapping(incomingPeerID);

                        // Broadcast disconnect to remaining clients.
                        PacketPlayerDisconnectHeader disconnectPacket{};
                        disconnectPacket.header.type = PacketType::PLAYER_DISCONNECT;
                        disconnectPacket.header.playerID = disconnectedPlayerID;

                        for (size_t i = 0; i < host->peerCount; ++i) {
                            ENetPeer* peer = &host->peers[i];
                            if (peer == event.peer) continue;
                            if (peer->state != ENET_PEER_STATE_CONNECTED) continue;
                            ENetPacket* p = enet_packet_create(&disconnectPacket,
                                                              sizeof(disconnectPacket),
                                                              ENET_PACKET_FLAG_RELIABLE);
                            enet_peer_send(peer, 0, p);
                        }
                    }
                } else {
                    // Client side: the host disconnected. Clear everything; the user
                    // will probably go back to the main menu.
                    players.clear();
                    localPlayerID = 0;
                    waitingForAssignment = false;
                }
                break;

            case ENET_EVENT_TYPE_NONE:
                break;
        }
    }
}

std::string NetworkManager::GetLocalIPAddress() {
    char hostbuffer[256];
    char *IPbuffer;
    struct hostent *host_entry;
    int hostname;

    // To retrieve hostname
    hostname = gethostname(hostbuffer, sizeof(hostbuffer));
    if (hostname == -1) {
        return "127.0.0.1";
    }

    // To retrieve host information
    host_entry = gethostbyname(hostbuffer);
    if (host_entry == nullptr) {
        return "127.0.0.1";
    }

    // Convert an Internet network address into ASCII string
    IPbuffer = inet_ntoa(*((struct in_addr*) host_entry->h_addr_list[0]));

    return std::string(IPbuffer);
}

void NetworkManager::SendPacket(const void* data, size_t size, bool reliable) {
    if (!host || !isConnected) return;

    // Defensive: a client must have a non-zero localPlayerID before sending anything that
    // requires player identity. The host is always ID 0.
    if (!isHost && localPlayerID == 0) {
        // Allow non-player-tagged packets (none currently), otherwise drop.
        // For safety, drop all packets until the host grants us an ID.
        return;
    }

    ENetPacket* packet = enet_packet_create(data, size, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);

    if (isHost) {
        // Broadcast to all clients
        enet_host_broadcast(host, 0, packet);
    } else {
        // Send to server
        enet_peer_send(serverPeer, 0, packet);
    }
}

std::vector<NetworkManager::NetworkEvent> NetworkManager::GetIncomingEvents() {
    return incomingEvents;
}

void NetworkManager::SetPlayerReady(bool ready) {
    // Find the local player in the list (or, if the client hasn't been assigned yet, no-op).
    if (localPlayerID == 0 && !isHost) return;

    for (auto& player : players) {
        if (player.peerID == localPlayerID) {
            player.isReady = ready;

            PacketPlayerReady readyPacket{};
            readyPacket.header.type = PacketType::PLAYER_READY;
            readyPacket.header.playerID = localPlayerID;
            readyPacket.isReady = ready;

            SendPacket(&readyPacket, sizeof(readyPacket), true);
            break;
        }
    }
}

bool NetworkManager::IsLocalPlayerReady() const {
    for (const auto& player : players) {
        if (player.peerID == localPlayerID) {
            return player.isReady;
        }
    }
    return false;
}

bool NetworkManager::AllPlayersReady() const {
    if (players.empty()) return false;

    for (const auto& player : players) {
        if (!player.isReady) {
            return false;
        }
    }
    return true;
}

int NetworkManager::GetReadyPlayerCount() const {
    int count = 0;
    for (const auto& player : players) {
        if (player.isReady) {
            count++;
        }
    }
    return count;
}

int NetworkManager::GetTotalPlayerCount() const {
    return static_cast<int>(players.size());
}

bool NetworkManager::StartGame() {
    if (!CanStartGame()) return false;
    ResetMatch();
    localKills = localDeaths = 0;
    matchActive = true;

    PacketGameStart startPacket{};
    startPacket.header.type = PacketType::GAME_START;
    startPacket.header.playerID = localPlayerID; // Host's ID is 0
    std::strncpy(startPacket.mapName, selectedMapName.c_str(), sizeof(startPacket.mapName) - 1);
    startPacket.mapName[sizeof(startPacket.mapName) - 1] = '\0';

    SendPacket(&startPacket, sizeof(startPacket), true);
    return true;
}

int NetworkManager::GetPingMs() const {
    return !isHost && serverPeer && isConnected ? (int)serverPeer->roundTripTime : 0;
}
std::string NetworkManager::StartRequirement() const {
    if (!isHost) return "Waiting for host to start";
    if (players.size() < 2) return "At least 2 players required";
    std::vector<int> teams;
    for (const auto& p : players) {
        if (p.username.empty() || p.username == "Unknown") return "Waiting for player information";
        teams.push_back(p.teamID);
    }
    if (!Rules::TeamsValid(teams, currentGameMode != OnlineGameMode::FREE_FOR_ALL))
        return "Both teams needed; size difference must be at most 1";
    if (!AllPlayersReady()) return "Every player must be ready";
    return "Ready to deploy";
}
bool NetworkManager::CanStartGame() const {
    return isHost && isConnected && !matchActive && StartRequirement() == "Ready to deploy";
}
void NetworkManager::PrepareLobby() {
    matchActive = false;
    ResetMatch();
    localKills = localDeaths = 0;
    for (auto& p : players) p.isReady = false;
}
