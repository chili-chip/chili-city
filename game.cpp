#include "game.hpp"

using namespace blit;

// Size of one map tile in pixels. 128 / 8 = 16 tiles across on VGC Zero.
constexpr int TILE_SIZE = 8;

// Cursor position in tiles, not pixels.
Point cursor(0, 0);

///////////////////////////////////////////////////////////////////////////
//
// init()
//
// setup your game here
//
void init() {
    set_screen_mode(ScreenMode::hires);
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

    // grass
    screen.pen = Pen(30, 120, 30);
    screen.clear();

    // grid lines, one pixel wide, every TILE_SIZE pixels
    screen.pen = Pen(20, 90, 20);
    for(int x = 0; x < screen.bounds.w; x += TILE_SIZE) {
        screen.rectangle(Rect(x, 0, 1, screen.bounds.h));
    }
    for(int y = 0; y < screen.bounds.h; y += TILE_SIZE) {
        screen.rectangle(Rect(0, y, screen.bounds.w, 1));
    }

    // cursor: white frame around the selected tile
    screen.pen = Pen(255, 255, 255);
    int px = cursor.x * TILE_SIZE;
    int py = cursor.y * TILE_SIZE;
    screen.rectangle(Rect(px, py, TILE_SIZE, 1));                 // top
    screen.rectangle(Rect(px, py + TILE_SIZE - 1, TILE_SIZE, 1)); // bottom
    screen.rectangle(Rect(px, py, 1, TILE_SIZE));                 // left
    screen.rectangle(Rect(px + TILE_SIZE - 1, py, 1, TILE_SIZE)); // right
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

    // keep the cursor on the visible tiles
    int cols = screen.bounds.w / TILE_SIZE;
    int rows = screen.bounds.h / TILE_SIZE;
    if(cursor.x < 0) cursor.x = 0;
    if(cursor.y < 0) cursor.y = 0;
    if(cursor.x > cols - 1) cursor.x = cols - 1;
    if(cursor.y > rows - 1) cursor.y = rows - 1;
}
