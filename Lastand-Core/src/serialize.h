#pragma once
#include <array>
#include <vector>
#ifndef SERIALIZE_H
#define SERIALIZE_H
#include <cstdint>
#include "Player.h"
#include "Obstacle.h"
#include "Projectile.h"
#include <map>

enum class MessageToServerTypes: uint8_t {
    ClientMove = 0, // input from player to go up, down, left, right
    SetClientAttributes = 1, // used for setting the username or color of player
    Shoot = 2, // when the player shoots a projectile
    ReadyUp = 3, // when the player is ready to start the game
    UnReady = 4 // when the player is not ready to start the game
};

enum class ClientMovementTypes: uint8_t {
    Start = 0, // when player presses down on an input
    Stop = 1 // when player releases an input
};

enum class ClientMovement: uint8_t {
    None  = 0b0000,

    Up    = 0b0001,
    Down  = 0b0010,
    Left  = 0b0100,
    Right = 0b1000,

    UpRight = Up | Right,
    UpLeft = Up | Left,
    DownRight = Down | Right,
    DownLeft = Down | Left,
};

ClientMovement operator|(ClientMovement c1, ClientMovement c2);
ClientMovement operator|=(ClientMovement &c1, ClientMovement c2);
bool operator&(ClientMovement c1, ClientMovement c2);

enum class MessageToClientTypes: uint8_t {
    // player positions have changed, sent on channel_updates.
    // data from serialize_game_player_positions() should be after this
    UpdatePlayerPositions = 0,

    // player attributes (username or color) have changed
    SetPlayerAttributes = 1,
    PlayerKilled = 2, // a player has killed another player
    PlayerLeft = 3, // a player has left
    PlayerJoined = 4, // a player has joined
    PlayerWon = 5, // a player has won
    // sent when a player joins late
    PreviousGameData = 6,
    // projectiles have moved
    UpdateProjectiles = 7,
    GameStarted = 8
};

enum class ObjectType: uint8_t {
    Player = 0,
    Obstacle = 1,
};

enum class SetPlayerAttributesTypes: uint8_t {
    UsernameChanged = 0,
    ColorChanged = 1
};

std::vector<uint8_t> serialize_player(const Player &player);
Player deserialize_player(const std::vector<uint8_t> &data);

constexpr int obstacle_data_size = 12;

std::array<uint8_t, obstacle_data_size> serialize_obstacle(const Obstacle &obstacle);
Obstacle deserialize_obstacle(const std::array<uint8_t, obstacle_data_size> &data);

void update_player_delta(ClientMovement movement, bool key_up, std::pair<short, short> &player_delta);

std::vector<uint8_t> serialize_game_player_positions(const std::vector<Player> &players);
void deserialize_and_update_game_player_positions(const std::vector<uint8_t> &data, std::map<int, Player> &players);

std::vector<uint8_t> serialize_previous_game_data(const std::vector<Player> &players, const std::vector<Obstacle> &obstacles);
std::pair<std::map<int, Player>, std::vector<Obstacle>> deserialize_and_update_previous_game_data(const std::vector<uint8_t> &data);

std::array<uint8_t, 12> serialize_client_projectile(ClientProjectile p);
ClientProjectile deserialize_client_projectile(const std::array<uint8_t, 12> &data);

std::array<uint8_t, 4> serialize_projectile(Projectile p);
Projectile deserialize_projectile(const std::array<uint8_t, 4> &data);

int32_t deserialize_int32(std::array<uint8_t, 4> data);
std::array<uint8_t, 4> serialize_int32(int32_t val);

#endif
