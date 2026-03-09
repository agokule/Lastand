#include "physics.h"
#include "Player.h"
#include <cmath>
#include <iostream>
#include <vector>
#include "utils.h"

bool point_in_rect(int x, int y, int width, int height, int px, int py) {
    return (x <= px && px <= x + width && y <= py && py <= y + height);
}

bool detect_collision(const Player& player, const std::vector<Obstacle>& obstacles) {
    // verticies of the player
    std::pair<int, int> v1 {player.x, player.y};
    std::pair<int, int> v2 {player.x + player_size, player.y};
    std::pair<int, int> v3 {player.x, player.y + player_size};
    std::pair<int, int> v4 {player.x + player_size, player.y + player_size};
    for (auto obstacle : obstacles) {
        // Obstacle bounds
        uint16_t obstacle_right = obstacle.x + obstacle.width;
        uint16_t obstacle_bottom = obstacle.y + obstacle.height;
        uint16_t obstacle_left = obstacle.x;
        uint16_t obstacle_top = obstacle.y;

        int obstacle_side1_x {obstacle_left};
        int obstacle_side2_x {obstacle_right};
        
        int obstacle_side1_y {obstacle_top};
        int obstacle_side2_y {obstacle_bottom};

        for (auto v : {v1, v2, v3, v4}) {
            if (point_in_rect(obstacle.x, obstacle.y, obstacle.width, obstacle.height, v.first, v.second)) {
                #ifdef DEBUG
                std::cout << "Player collided with obstacle at: (" << v.first << ", " << v.second << ")" << '\n';
                #endif
                return true;
            }
            for (auto side_x : {obstacle_side1_x, obstacle_side2_x}) {
                if (!is_within(side_x, v.first, 1.0) || v.second < obstacle_top || v.second > obstacle_bottom) {
                    continue;
                }
                #ifdef DEBUG
                std::cout << "Vertical collision: x:" << side_x << " vs (" << v.first << ", " << v.second << ")" << '\n';
                #endif
                return true;
            }
            for (auto side_y : {obstacle_side1_y, obstacle_side2_y}) {
                if (!is_within(side_y, v.second, 1.0) || v.first < obstacle_left || v.first > obstacle_right) {
                    continue;
                }
                #ifdef DEBUG
                std::cout << "Horizontal collision: y:" << side_y << " vs (" << v.first << ", " << v.second << ")" << '\n';
                #endif
                return true;
            }
        }
    }
    return false;
}

