#include <string>

#include "game.hpp"

using namespace blit;

// Size of one map tile in pixels. 128 / 8 = 16 tiles across on VGC Zero.
constexpr int TILE_SIZE = 8;

// The whole city is bigger than the screen; the camera shows part of it.
constexpr int MAP_W = 32;
constexpr int MAP_H = 32;

// What is on each map tile. One byte per tile.
enum Tile : uint8_t {
    TILE_GRASS = 0,
};

// The city. map[y][x], so a row is one line of tiles.
uint8_t map[MAP_H][MAP_W];

// Cursor position in map tiles, not pixels.
Point cursor(0, 0);

// Map tile shown in the top-left corner of the screen.
Point camera(0, 0);

///////////////////////////////////////////////////////////////////////////
//
// init()
//
// setup your game here
//
void init() {
    set_screen_mode(ScreenMode::hires);

    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            map[y][x] = TILE_GRASS;
        }
    }
}

///////////////////////////////////////////////////////////////////////////
//
// render(time)
//
// This function is called to perform rendering of the game. time is the 
// amount if milliseconds elapsed since the start of your game
//
void render(uint32_t time) {
    screen.alpha = 255;
    screen.mask = nullptr;

    int view_w = screen.bounds.w / TILE_SIZE;
    int view_h = screen.bounds.h / TILE_SIZE;

    // draw the visible part of the map
    for(int sy = 0; sy < view_h; sy++) {
        for(int sx = 0; sx < view_w; sx++) {
            int mx = camera.x + sx;
            int my = camera.y + sy;

            switch(map[my][mx]) {
                case TILE_GRASS:
                default:
                    screen.pen = Pen(30, 120, 30);
                    break;
            }
            screen.rectangle(Rect(sx * TILE_SIZE, sy * TILE_SIZE, TILE_SIZE, TILE_SIZE));
        }
    }

    // grid lines, one pixel wide, every TILE_SIZE pixels
    screen.pen = Pen(20, 90, 20);
    for(int x = 0; x < screen.bounds.w; x += TILE_SIZE) {
        screen.rectangle(Rect(x, 0, 1, screen.bounds.h));
    }
    for(int y = 0; y < screen.bounds.h; y += TILE_SIZE) {
        screen.rectangle(Rect(0, y, screen.bounds.w, 1));
    }

    // cursor: white frame around the selected tile, in screen pixels
    screen.pen = Pen(255, 255, 255);
    int px = (cursor.x - camera.x) * TILE_SIZE;
    int py = (cursor.y - camera.y) * TILE_SIZE;
    screen.rectangle(Rect(px, py, TILE_SIZE, 1));                 // top
    screen.rectangle(Rect(px, py + TILE_SIZE - 1, TILE_SIZE, 1)); // bottom
    screen.rectangle(Rect(px, py, 1, TILE_SIZE));                 // left
    screen.rectangle(Rect(px + TILE_SIZE - 1, py, 1, TILE_SIZE)); // right

    // cursor position in map tiles, so you can see the camera scroll
    std::string pos = std::to_string(cursor.x) + "," + std::to_string(cursor.y);
    Size size = screen.measure_text(pos, minimal_font);
    screen.pen = Pen(0, 0, 0);
    screen.rectangle(Rect(0, 0, size.w + 2, size.h + 2));
    screen.pen = Pen(255, 255, 255);
    screen.text(pos, minimal_font, Point(1, 1));
}

///////////////////////////////////////////////////////////////////////////
//
// update(time)
//
// This is called to update your game state. time is the 
// amount if milliseconds elapsed since the start of your game
//
void update(uint32_t time) {
    // buttons.pressed only has the buttons that went down since the last
    // update, so holding a direction moves the cursor once, not every frame
    if(buttons.pressed & Button::DPAD_LEFT)  cursor.x--;
    if(buttons.pressed & Button::DPAD_RIGHT) cursor.x++;
    if(buttons.pressed & Button::DPAD_UP)    cursor.y--;
    if(buttons.pressed & Button::DPAD_DOWN)  cursor.y++;

    // keep the cursor on the map
    if(cursor.x < 0) cursor.x = 0;
    if(cursor.y < 0) cursor.y = 0;
    if(cursor.x > MAP_W - 1) cursor.x = MAP_W - 1;
    if(cursor.y > MAP_H - 1) cursor.y = MAP_H - 1;

    // move the camera only when the cursor would leave the screen
    int view_w = screen.bounds.w / TILE_SIZE;
    int view_h = screen.bounds.h / TILE_SIZE;
    if(cursor.x < camera.x) camera.x = cursor.x;
    if(cursor.y < camera.y) camera.y = cursor.y;
    if(cursor.x > camera.x + view_w - 1) camera.x = cursor.x - view_w + 1;
    if(cursor.y > camera.y + view_h - 1) camera.y = cursor.y - view_h + 1;
}
