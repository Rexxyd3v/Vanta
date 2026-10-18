#ifndef PACKETS_H
#define PACKETS_H

// Must define these before any Windows headers to avoid conflicts with Raylib
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <raylib.h>
#include <stdint.h>

enum class OnlineGameMode : uint8_t {
    ELIMINATION = 0,
    FREE_FOR_ALL,
    TEAM_DEATHMATCH,
    DEATHMATCH // Team flag delivery with continuous base respawns.
};

enum class PacketType : uint8_t {
    ID_ASSIGNMENT = 0,  // Host -> Client: grants the authoritative playerID
    PLAYER_CONNECT,     // Player info announcement (used for both new and existing)
    PLAYER_DISCONNECT,
    PLAYER_READY,       // Player ready state changed
    PLAYER_UPDATE,      // Sent constantly: position, state, weapon, facing
    PLAYER_SHOOT,       // Event: player fired a shot
    PLAYER_DAMAGE,      // Event: player took damage
    PLAYER_KILLED,      // Event: player was killed by another (updates score)
    PLAYER_RESPAWN,     // Event: player respawned
    GAME_START,         // Host signals game should start
    MAP_CHANGED,        // Host signals selected map changed
    LOBBY_SETTINGS,     // Host signals mode/limit changed
    PLAYER_TEAM_CHANGE, // Client requests team change / Host broadcasts
    ROUND_START,        // Host signals start of elimination round
    ROUND_END,          // Host signals end of elimination round
    VOICE_DATA,         // Voice audio data (Opus encoded)
    MATCH_STATE,       // Host clock/scores and end-of-match result
    RETURN_LOBBY,
    PLAYER_GRENADE,
    FLAG_INTERACT,      // Client requests a drop or teammate handoff.
    FLAG_STATE          // Host owns flag positions, carriers, and capture scores.
};

// Base header for all packets
#pragma pack(push, 1)
struct PacketHeader {
    PacketType type;
    uint32_t playerID;  // Authoritative player ID assigned by the host. 0 = host, >0 = clients.
};

// Host -> single client: "your authoritative playerID is X"
struct PacketIDAssignment {
    PacketHeader header;
    uint32_t assignedID;   // The host-assigned playerID
};

// Host -> all clients: announcement that a player exists.
// This packet is ONLY emitted by the host. The header.playerID is authoritative.
// Clients NEVER generate or modify playerIDs.
struct PacketPlayerConnect {
    PacketHeader header;
    char username[20];
    int charSkin;     // 1, 2, 3, or 4
    int weaponSkin;   // WeaponSkinId enum value
    int teamID;       // 0 = None, 1 = Global Risk (Blue), 2 = Black List (Red)
};

struct PacketPlayerUpdate {
    PacketHeader header;
    Vector2 position;
    int state;             // CharState (Idle, Walk, Death, etc)
    int currentWeaponIndex;
    int faceDirection;     // 1 or -1
    float health;
    float jumpHeight;      // Vertical jump sync
    float jumpVelocity;
    char username[16];
    int charSkin;
    float dashTimer = 0, shieldTimer = 0;
    int weaponSkin;        // WeaponSkinId enum value
    int melee=-1,meleeSkin=0,utility=-1;
};

struct PacketPlayerShoot {
    PacketHeader header;
    Vector2 aimDir;   // Normalized direction vector (not world position)
    int weapon=0,skin=0,melee=-1,meleeSkin=0;
};

struct PacketPlayerGrenade {
    PacketHeader header;
    Vector2 start, target;
    uint8_t kind; // 0 = frag, 1 = poison smoke, 2 = flashbang
};

struct PacketPlayerDamage {
    PacketHeader header;
    uint32_t targetPlayerID; // Authoritative ID of damaged player (assigned by host)
    float damageAmount;
    int weaponIndex=0,weaponSkin=0; // Captured when the projectile was fired.
};

struct PacketPlayerKilled {
    PacketHeader header;
    uint32_t killerPlayerID; // Authoritative ID of killer
    uint32_t victimPlayerID; // Authoritative ID of victim
    int weaponIndex=0,weaponSkin=0; // 0..WeaponCount-1 guns, WeaponCount grenade, WeaponCount+1 poison.
};

struct PacketPlayerRespawn {
    PacketHeader header;
    Vector2 spawnPosition;
    int charSkin;  // Skin ID (1-4)
    int weaponSkin; // WeaponSkinId enum value
};

struct PacketPlayerReady {
    PacketHeader header;
    bool isReady;  // True if player is ready, false otherwise
};

struct PacketPlayerTeamChange {
    PacketHeader header;
    int teamID; // 1 = Global Risk, 2 = Black List
};

struct PacketPlayerDisconnectHeader {
    PacketHeader header;
    // No additional data needed for disconnect notification
};

struct PacketGameStart {
    PacketHeader header;
    char mapName[64]; // Map folder name from MapRegistry
};

struct PacketMapChanged {
    PacketHeader header;
    char mapName[64]; // Map folder name from MapRegistry
};

struct PacketLobbySettings {
    PacketHeader header;
    OnlineGameMode gameMode;
    int timeLimit;
    int killLimit;
    int roundLimit;
};

struct PacketRoundStart {
    PacketHeader header;
    int roundNumber;
};

struct PacketRoundEnd {
    PacketHeader header;
    int winningTeamID; // 0 = Draw, 1 = GR, 2 = BL
    int grScore;
    int blScore;
};

struct PacketMatchState {
    PacketHeader header;
    float elapsed;
    int grScore, blScore;
    bool finished;
    int winner; // team ID, FFA player ID, or -1 for draw
};

struct FlagWireState {
    Vector2 position;
    uint32_t carrierID; // UINT32_MAX means no carrier; host has ID zero.
    bool dropped;
    float returnTimer, respawnTimer;
};
struct PacketFlagState {
    PacketHeader header;
    FlagWireState flags[2];
    int grScore, blScore;
    uint32_t announcementSerial;
    int announcementTeam;
    char announcement[96];
};

// Voice data packet (Opus encoded audio)
#define MAX_OPUS_FRAME_SIZE 1200 // Maximum size of an Opus frame in bytes
struct PacketVoiceData {
    PacketHeader header;
    uint16_t sequenceNumber; // For detecting packet loss/reordering
    uint16_t frameSize;      // Size of the Opus frame in bytes
    uint8_t  opusData[MAX_OPUS_FRAME_SIZE]; // The encoded audio data
};
#pragma pack(pop)

#endif // PACKETS_H
