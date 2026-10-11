#include <string>

#include "game.hpp"
#include "assets.hpp"

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

// Where each tile's picture is in assets/tiles.png. The sheet is 8 sprites
// wide, so sprite n is at column n % 8, row n / 8.
constexpr int SPRITE_GRASS = 0;
constexpr int SPRITE_ROAD = 16;        // + which neighbours are roads (0..15)
constexpr int SPRITE_RESIDENTIAL = 32; // + level (0..3)
constexpr int SPRITE_COMMERCIAL = 40;  // + level
constexpr int SPRITE_INDUSTRIAL = 48;  // + level

// What pressing A does. B switches to the next tool.
enum Tool {
    TOOL_ROAD = 0,
    TOOL_RESIDENTIAL,
    TOOL_COMMERCIAL,
    TOOL_INDUSTRIAL,
    TOOL_BULLDOZER,
    TOOL_COUNT, // number of tools, keep last
};

// What each tool costs per use, in the same order as Tool.
const int tool_costs[TOOL_COUNT] = {
    10, // road
    50, // residential zone
    50, // commercial zone
    50, // industrial zone
    5,  // bulldozer
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
int month = 0; // months since January 1900

const char *month_names[12] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
};

// City totals, recounted every month by simulate().
int population = 0;
int res_levels = 0, com_levels = 0, ind_levels = 0;
int road_count = 0;

// Money. Building costs money, taxes bring it in once a month.
int money = 5000;
int last_income = 0;    // taxes minus road upkeep last month

// When the player last tried to build without enough money, to flash the HUD.
uint32_t broke_time = 0;
bool was_broke = false;

// Y shows the stats screen instead of the map.
bool show_stats = false;

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

// Which of the four neighbours are roads, as bits: north 1, east 2,
// south 4, west 8. Picks the road sprite that connects the right way.
int road_mask(int x, int y) {
    int mask = 0;
    if(y > 0         && map[y - 1][x] == TILE_ROAD) mask |= 1;
    if(x < MAP_W - 1 && map[y][x + 1] == TILE_ROAD) mask |= 2;
    if(y < MAP_H - 1 && map[y + 1][x] == TILE_ROAD) mask |= 4;
    if(x > 0         && map[y][x - 1] == TILE_ROAD) mask |= 8;
    return mask;
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

    // taxes are collected on last month's city, before it grows
    int taxes = res * PEOPLE_PER_LEVEL / 4 + (com + ind) * 5;

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

    // recount after growing, for the HUD and stats screen
    res_levels = com_levels = ind_levels = road_count = 0;
    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            switch(map[y][x]) {
                case TILE_RESIDENTIAL: res_levels += level[y][x]; break;
                case TILE_COMMERCIAL:  com_levels += level[y][x]; break;
                case TILE_INDUSTRIAL:  ind_levels += level[y][x]; break;
                case TILE_ROAD:        road_count++; break;
                default: break;
            }
        }
    }
    population = res_levels * PEOPLE_PER_LEVEL;

    // every road tile costs 1 a month to maintain
    last_income = taxes - road_count;
    money += last_income;
}

// Draws white text on a black box. align says which corner p is.
void label(std::string_view text, Point p, TextAlign align, Pen colour = Pen(255, 255, 255)) {
    Size size = screen.measure_text(text, minimal_font);
    if(align & TextAlign::right)  p.x -= size.w + 2;
    if(align & TextAlign::bottom) p.y -= size.h + 2;
    screen.pen = Pen(0, 0, 0);
    screen.rectangle(Rect(p.x, p.y, size.w + 2, size.h + 2));
    screen.pen = colour;
    screen.text(text, minimal_font, Point(p.x + 1, p.y + 1));
}

void render_stats() {
    screen.pen = Pen(0, 0, 40);
    screen.clear();
    screen.pen = Pen(255, 255, 255);

    std::string lines[] = {
        "CHILI CITY",
        "",
        std::string(month_names[month % 12]) + " " + std::to_string(1900 + month / 12),
        "MONEY  $" + std::to_string(money),
        "INCOME $" + std::to_string(last_income) + "/MONTH",
        "",
        "POPULATION " + std::to_string(population),
        "RES LEVELS " + std::to_string(res_levels),
        "COM LEVELS " + std::to_string(com_levels),
        "IND LEVELS " + std::to_string(ind_levels),
        "ROADS      " + std::to_string(road_count),
        "",
        "Y: BACK TO MAP",
    };
    int y = 4;
    for(auto &line : lines) {
        screen.text(line, minimal_font, Point(4, y));
        y += 9;
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

    // the tile pictures; screen.sprite(n, ...) draws sprite n from this sheet
    screen.sprites = Surface::load(asset_tiles);

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

    if(show_stats) {
        render_stats();
        return;
    }

    int view_w = screen.bounds.w / TILE_SIZE;
    int view_h = screen.bounds.h / TILE_SIZE;

    // draw the visible part of the map
    for(int sy = 0; sy < view_h; sy++) {
        for(int sx = 0; sx < view_w; sx++) {
            int mx = camera.x + sx;
            int my = camera.y + sy;

            int sprite;
            switch(map[my][mx]) {
                case TILE_ROAD:        sprite = SPRITE_ROAD + road_mask(mx, my); break;
                case TILE_RESIDENTIAL: sprite = SPRITE_RESIDENTIAL + level[my][mx]; break;
                case TILE_COMMERCIAL:  sprite = SPRITE_COMMERCIAL + level[my][mx]; break;
                case TILE_INDUSTRIAL:  sprite = SPRITE_INDUSTRIAL + level[my][mx]; break;
                case TILE_GRASS:
                default:               sprite = SPRITE_GRASS; break;
            }
            screen.sprite(sprite, Point(sx * TILE_SIZE, sy * TILE_SIZE));
        }
    }

    // cursor: white frame around the selected tile, in screen pixels
    screen.pen = Pen(255, 255, 255);
    int px = (cursor.x - camera.x) * TILE_SIZE;
    int py = (cursor.y - camera.y) * TILE_SIZE;
    screen.rectangle(Rect(px, py, TILE_SIZE, 1));                 // top
    screen.rectangle(Rect(px, py + TILE_SIZE - 1, TILE_SIZE, 1)); // bottom
    screen.rectangle(Rect(px, py, 1, TILE_SIZE));                 // left
    screen.rectangle(Rect(px + TILE_SIZE - 1, py, 1, TILE_SIZE)); // right

    // HUD: money top-left (red for a moment after a failed build),
    // population top-right, tool and cost bottom-left, date bottom-right
    bool flash = was_broke && time - broke_time < 1000;
    label("$" + std::to_string(money), Point(0, 0), TextAlign::top_left,
          flash ? Pen(255, 60, 60) : Pen(255, 255, 255));
    label("POP " + std::to_string(population), Point(screen.bounds.w, 0), TextAlign::top_right);
    label(std::string(tool_names[tool]) + " $" + std::to_string(tool_costs[tool]),
          Point(0, screen.bounds.h), TextAlign::bottom_left);
    label(std::string(month_names[month % 12]) + " " + std::to_string(1900 + month / 12),
          Point(screen.bounds.w, screen.bounds.h), TextAlign::bottom_right);
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

    // Y: stats screen on and off
    if(buttons.pressed & Button::Y) {
        show_stats = !show_stats;
    }
    if(show_stats) {
        return; // the city keeps running, but the map controls are off
    }

    // B: next tool, wrapping back to the first one
    if(buttons.pressed & Button::B) {
        tool = Tool((tool + 1) % TOOL_COUNT);
    }

    // A: use the tool on the tile under the cursor
    if(buttons.pressed & Button::A) {
        uint8_t &tile = map[cursor.y][cursor.x];
        uint8_t before = tile;
        int cost = tool_costs[tool];
        if(money < cost) {
            was_broke = true;
            broke_time = time;
            return;
        }
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
        // only pay when something actually changed
        if(tile != before) {
            money -= cost;
        }
    }
}
