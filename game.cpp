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
    TILE_ROAD,
    TILE_RESIDENTIAL,
    TILE_COMMERCIAL,
    TILE_INDUSTRIAL,
};

// What pressing A does. B switches to the next tool.
enum Tool {
    TOOL_ROAD = 0,
    TOOL_RESIDENTIAL,
    TOOL_COMMERCIAL,
    TOOL_INDUSTRIAL,
    TOOL_BULLDOZER,
    TOOL_COUNT, // number of tools, keep last
};

const char *tool_names[TOOL_COUNT] = {
    "ROAD",
    "RES ZONE",
    "COM ZONE",
    "IND ZONE",
    "BULLDOZE",
};

Tool tool = TOOL_ROAD;

// The city. map[y][x], so a row is one line of tiles.
uint8_t map[MAP_H][MAP_W];

// How built up each zone tile is: 0 = empty lot, MAX_LEVEL = dense.
constexpr int MAX_LEVEL = 3;
uint8_t level[MAP_H][MAP_W];

// People living on one residential level.
constexpr int PEOPLE_PER_LEVEL = 20;

// One game month in real milliseconds. The simulation runs once a month.
constexpr uint32_t MONTH_MS = 2000;
uint32_t last_month_time = 0;
int month = 0;

int population = 0;

// Cursor position in map tiles, not pixels.
Point cursor(0, 0);

// Map tile shown in the top-left corner of the screen.
Point camera(0, 0);

// true if one of the four tiles next to (x, y) is a road
bool next_to_road(int x, int y) {
    if(x > 0         && map[y][x - 1] == TILE_ROAD) return true;
    if(x < MAP_W - 1 && map[y][x + 1] == TILE_ROAD) return true;
    if(y > 0         && map[y - 1][x] == TILE_ROAD) return true;
    if(y < MAP_H - 1 && map[y + 1][x] == TILE_ROAD) return true;
    return false;
}

// One month of city life: zones next to roads grow when there is demand,
// zones cut off from roads shrink.
void simulate() {
    // count first, so every tile sees the same totals this month
    int res = 0, com = 0, ind = 0;
    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            switch(map[y][x]) {
                case TILE_RESIDENTIAL: res += level[y][x]; break;
                case TILE_COMMERCIAL:  com += level[y][x]; break;
                case TILE_INDUSTRIAL:  ind += level[y][x]; break;
                default: break;
            }
        }
    }

    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            uint8_t tile = map[y][x];
            if(tile != TILE_RESIDENTIAL && tile != TILE_COMMERCIAL && tile != TILE_INDUSTRIAL) {
                continue;
            }

            uint8_t &lv = level[y][x];
            if(!next_to_road(x, y)) {
                if(lv > 0) lv--;
                continue;
            }
            if(lv >= MAX_LEVEL) {
                continue;
            }

            bool demand = false;
            switch(tile) {
                case TILE_RESIDENTIAL:
                    // people move in when there are jobs; the first few come anyway
                    demand = res < com + ind + 4;
                    break;
                case TILE_COMMERCIAL:
                    // shops need customers
                    demand = res > com * 2;
                    break;
                case TILE_INDUSTRIAL:
                    // factories need workers
                    demand = res > ind;
                    break;
            }

            // grow slowly and unevenly: a one in three chance each month
            if(demand && blit::random() % 3 == 0) {
                lv++;
            }
        }
    }

    population = 0;
    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            if(map[y][x] == TILE_RESIDENTIAL) {
                population += level[y][x] * PEOPLE_PER_LEVEL;
            }
        }
    }
}

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
            level[y][x] = 0;
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
                case TILE_ROAD:
                    screen.pen = Pen(110, 110, 110);
                    break;
                case TILE_RESIDENTIAL:
                    screen.pen = Pen(120, 220, 120);
                    break;
                case TILE_COMMERCIAL:
                    screen.pen = Pen(80, 140, 230);
                    break;
                case TILE_INDUSTRIAL:
                    screen.pen = Pen(230, 200, 60);
                    break;
                case TILE_GRASS:
                default:
                    screen.pen = Pen(30, 120, 30);
                    break;
            }
            screen.rectangle(Rect(sx * TILE_SIZE, sy * TILE_SIZE, TILE_SIZE, TILE_SIZE));

            // buildings: a darker square that gets bigger with the level
            int lv = level[my][mx];
            if(lv > 0) {
                Pen zone = screen.pen;
                screen.pen = Pen(zone.r / 2, zone.g / 2, zone.b / 2);
                int size = lv * 2;
                int offset = 1 + (TILE_SIZE - 1 - size) / 2;
                screen.rectangle(Rect(sx * TILE_SIZE + offset, sy * TILE_SIZE + offset, size, size));
            }
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

    // population in the top-right corner
    std::string pop = "POP " + std::to_string(population);
    size = screen.measure_text(pop, minimal_font);
    int tx = screen.bounds.w - size.w - 2;
    screen.pen = Pen(0, 0, 0);
    screen.rectangle(Rect(tx, 0, size.w + 2, size.h + 2));
    screen.pen = Pen(255, 255, 255);
    screen.text(pop, minimal_font, Point(tx + 1, 1));

    // active tool in the bottom-left corner
    std::string_view name = tool_names[tool];
    size = screen.measure_text(name, minimal_font);
    int ty = screen.bounds.h - size.h - 2;
    screen.pen = Pen(0, 0, 0);
    screen.rectangle(Rect(0, ty, size.w + 2, size.h + 2));
    screen.pen = Pen(255, 255, 255);
    screen.text(name, minimal_font, Point(1, ty + 1));
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

    // run the simulation once per game month
    if(time - last_month_time >= MONTH_MS) {
        last_month_time = time;
        month++;
        simulate();
    }

    // B: next tool, wrapping back to the first one
    if(buttons.pressed & Button::B) {
        tool = Tool((tool + 1) % TOOL_COUNT);
    }

    // A: use the tool on the tile under the cursor
    if(buttons.pressed & Button::A) {
        uint8_t &tile = map[cursor.y][cursor.x];
        switch(tool) {
            // building only works on empty grass; bulldoze first to replace
            case TOOL_ROAD:
                if(tile == TILE_GRASS) tile = TILE_ROAD;
                break;
            case TOOL_RESIDENTIAL:
                if(tile == TILE_GRASS) tile = TILE_RESIDENTIAL;
                break;
            case TOOL_COMMERCIAL:
                if(tile == TILE_GRASS) tile = TILE_COMMERCIAL;
                break;
            case TOOL_INDUSTRIAL:
                if(tile == TILE_GRASS) tile = TILE_INDUSTRIAL;
                break;
            case TOOL_BULLDOZER:
                tile = TILE_GRASS;
                level[cursor.y][cursor.x] = 0;
                break;
            default:
                break;
        }
    }
}
