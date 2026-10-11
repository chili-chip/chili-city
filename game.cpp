#include <algorithm>
#include <cstdlib>
#include <cstring>
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
    TILE_POWER_LINE,
    TILE_POWER_PLANT, // 2x2; level[][] holds which quarter (0..3)
    TILE_PARK,
};

// Where each tile's picture is in assets/tiles.png. The sheet is 8 sprites
// wide, so sprite n is at column n % 8, row n / 8.
constexpr int SPRITE_GRASS = 0;
constexpr int SPRITE_ROAD = 16;        // + which neighbours are roads (0..15)
constexpr int SPRITE_RESIDENTIAL = 32; // + level (0..3)
constexpr int SPRITE_COMMERCIAL = 40;  // + level
constexpr int SPRITE_INDUSTRIAL = 48;  // + level
constexpr int SPRITE_POWER_LINE = 56;  // + which neighbours carry power (0..15)
constexpr int SPRITE_POWER_PLANT = 72; // + quarter: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right
constexpr int SPRITE_NO_POWER = 76;    // bolt drawn over zones without power
constexpr int SPRITE_PARK = 77;

// What pressing A does. B switches to the next tool.
enum Tool {
    TOOL_ROAD = 0,
    TOOL_RESIDENTIAL,
    TOOL_COMMERCIAL,
    TOOL_INDUSTRIAL,
    TOOL_POWER_LINE,
    TOOL_POWER_PLANT,
    TOOL_PARK,
    TOOL_BULLDOZER,
    TOOL_COUNT, // number of tools, keep last
};

// What each tool costs per use, in the same order as Tool.
const int tool_costs[TOOL_COUNT] = {
    10, // road
    50, // residential zone
    50, // commercial zone
    50, // industrial zone
    5,  // power line
    1500, // power plant
    20, // park
    5,  // bulldozer
};

const char *tool_names[TOOL_COUNT] = {
    "ROAD",
    "RES ZONE",
    "COM ZONE",
    "IND ZONE",
    "POWER LINE",
    "PLANT",
    "PARK",
    "BULLDOZE",
};

Tool tool = TOOL_ROAD;

// The city. map[y][x], so a row is one line of tiles.
uint8_t map[MAP_H][MAP_W];

// How built up each zone tile is: 0 = empty lot, MAX_LEVEL = dense.
constexpr int MAX_LEVEL = 3;
uint8_t level[MAP_H][MAP_W];

// Which tiles get electricity, worked out by update_power().
bool powered[MAP_H][MAP_W];

// Smog from industry and how nice each tile is to live on, 0..255,
// worked out by update_land(). Homes care about both.
uint8_t pollution[MAP_H][MAP_W];
uint8_t land_value[MAP_H][MAP_W];

// Homes stop growing at this much pollution, and lose a level at the second.
constexpr int POLLUTION_NO_GROWTH = 40;
constexpr int POLLUTION_DECLINE = 80;

// Land value without parks or pollution. Parks push it up, smog down.
constexpr int BASE_LAND_VALUE = 100;
constexpr int GOOD_LAND_VALUE = 120; // from here homes grow faster

// X cycles what is drawn over the map: nothing, pollution, land value.
enum Overlay {
    OVERLAY_NONE = 0,
    OVERLAY_POLLUTION,
    OVERLAY_LAND_VALUE,
    OVERLAY_COUNT, // keep last
};
Overlay overlay = OVERLAY_NONE;
const char *overlay_names[OVERLAY_COUNT] = {"", "POLLUTION", "LAND VALUE"};

// People living on one residential level.
constexpr int PEOPLE_PER_LEVEL = 20;

// How fast time goes, picked in the pause menu.
enum Speed {
    SPEED_PAUSED = 0,
    SPEED_NORMAL,
    SPEED_FAST,
    SPEED_COUNT, // keep last
};
Speed speed = SPEED_NORMAL;
const char *speed_names[SPEED_COUNT] = {"PAUSED", "NORMAL", "FAST"};

// One game month in real milliseconds, per speed. The simulation runs
// once a month. Paused never ticks.
const uint32_t month_ms[SPEED_COUNT] = {0, 2000, 500};
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

// Which screen the game is on. The stats screen (Y) is part of the city.
enum Screen {
    SCREEN_TITLE,
    SCREEN_CITY,
    SCREEN_PAUSE,
};
Screen current_screen = SCREEN_TITLE;

// Highlighted line in the title or pause menu.
int menu_choice = 0;

// true when a saved city was loaded, so the title offers CONTINUE.
bool has_save = false;

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

bool is_zone(uint8_t tile) {
    return tile == TILE_RESIDENTIAL || tile == TILE_COMMERCIAL || tile == TILE_INDUSTRIAL;
}

// Power flows through power lines, plants and zones (zones pass it on to
// the zone next door), but not through roads or grass.
bool carries_power(uint8_t tile) {
    return tile == TILE_POWER_LINE || tile == TILE_POWER_PLANT || is_zone(tile);
}

// Which neighbours carry power, as bits like road_mask, so a power line
// sprite connects to them.
int power_mask(int x, int y) {
    int mask = 0;
    if(y > 0         && carries_power(map[y - 1][x])) mask |= 1;
    if(x < MAP_W - 1 && carries_power(map[y][x + 1])) mask |= 2;
    if(y < MAP_H - 1 && carries_power(map[y + 1][x])) mask |= 4;
    if(x > 0         && carries_power(map[y][x - 1])) mask |= 8;
    return mask;
}

// Flood fill from every power plant through connected tiles that carry
// power. Whatever the fill reaches is powered.
void update_power() {
    // tiles still to visit; a tile is only added once, so MAP_W * MAP_H is enough
    static Point todo[MAP_W * MAP_H];
    int count = 0;

    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            powered[y][x] = map[y][x] == TILE_POWER_PLANT;
            if(powered[y][x]) {
                todo[count++] = Point(x, y);
            }
        }
    }

    const Point steps[4] = {Point(0, -1), Point(1, 0), Point(0, 1), Point(-1, 0)};
    while(count > 0) {
        Point p = todo[--count];
        for(auto &step : steps) {
            Point n = p + step;
            if(n.x < 0 || n.y < 0 || n.x >= MAP_W || n.y >= MAP_H) continue;
            if(powered[n.y][n.x] || !carries_power(map[n.y][n.x])) continue;
            powered[n.y][n.x] = true;
            todo[count++] = n;
        }
    }
}

// Adds amount - falloff * distance to every tile within reach of (cx, cy),
// clamped to 0..255. Distance is counted in steps (up/down/left/right).
void spread(uint8_t grid[MAP_H][MAP_W], int cx, int cy, int amount, int falloff) {
    int reach = amount / falloff;
    for(int y = cy - reach; y <= cy + reach; y++) {
        for(int x = cx - reach; x <= cx + reach; x++) {
            if(x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) continue;
            int dist = abs(x - cx) + abs(y - cy);
            int add = amount - falloff * dist;
            if(add <= 0) continue;
            grid[y][x] = std::min(255, grid[y][x] + add);
        }
    }
}

// Industry pollutes around it, more the bigger it is. Parks raise land
// value around them; pollution lowers it.
void update_land() {
    static uint8_t park_bonus[MAP_H][MAP_W];
    memset(pollution, 0, sizeof(pollution));
    memset(park_bonus, 0, sizeof(park_bonus));

    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            if(map[y][x] == TILE_INDUSTRIAL && level[y][x] > 0) {
                // level 3 next door: 120 - 20 = 100, enough to drive homes out
                spread(pollution, x, y, level[y][x] * 40, 20);
            } else if(map[y][x] == TILE_PARK) {
                spread(park_bonus, x, y, 30, 10);
            }
        }
    }

    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            int value = BASE_LAND_VALUE + park_bonus[y][x] - pollution[y][x] / 2;
            land_value[y][x] = std::max(0, std::min(255, value));
        }
    }
}

// Recounts the city totals shown on the HUD and the stats screen.
void count_city() {
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

    update_power();
    update_land();
}

// What goes into the save file. read_save/write_save copy this struct
// byte for byte, so magic and version let us spot a file from another
// game or an older layout and ignore it.
constexpr uint32_t SAVE_MAGIC = 0x59544943; // "CITY"
constexpr uint8_t SAVE_VERSION = 1;

struct SaveData {
    uint32_t magic;
    uint8_t version;
    uint8_t map[MAP_H][MAP_W];
    uint8_t level[MAP_H][MAP_W];
    int32_t money;
    int32_t month;
};

// When the city was last saved, to show "SAVED" on the HUD for a moment.
uint32_t saved_time = 0;
bool was_saved = false;

void save_city() {
    // static: about 2 KB, better kept off the stack
    static SaveData save;
    save.magic = SAVE_MAGIC;
    save.version = SAVE_VERSION;
    memcpy(save.map, map, sizeof(map));
    memcpy(save.level, level, sizeof(level));
    save.money = money;
    save.month = month;
    write_save(save);
}

// Returns false when there is no usable save, so the game starts fresh.
bool load_city() {
    static SaveData save;
    if(!read_save(save) || save.magic != SAVE_MAGIC || save.version != SAVE_VERSION) {
        return false;
    }
    memcpy(map, save.map, sizeof(map));
    memcpy(level, save.level, sizeof(level));
    money = save.money;
    month = save.month;
    count_city();
    return true;
}

// Empty map, starting money, January 1900.
void new_city() {
    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            map[y][x] = TILE_GRASS;
            level[y][x] = 0;
        }
    }
    money = 5000;
    month = 0;
    last_income = 0;
    cursor = Point(0, 0);
    camera = Point(0, 0);
    count_city();
}

// How much the city wants more of each zone, from its building levels.
// Above 0 means that zone grows; the HUD meter shows the same numbers.
int res_demand(int res, int com, int ind) {
    return com + ind + 4 - res; // people move in when there are jobs; the first few come anyway
}
int com_demand(int res, int com) {
    return res - com * 2;       // shops need customers
}
int ind_demand(int res, int ind) {
    return res - ind;           // factories need workers
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
            // smog drives people out of their homes
            if(tile == TILE_RESIDENTIAL && pollution[y][x] >= POLLUTION_DECLINE) {
                if(lv > 0) lv--;
                continue;
            }
            if(lv >= MAX_LEVEL || !powered[y][x]) {
                continue; // full, or no electricity: nobody moves in
            }
            if(tile == TILE_RESIDENTIAL && pollution[y][x] >= POLLUTION_NO_GROWTH) {
                continue; // too smoggy to move in
            }

            bool demand = false;
            switch(tile) {
                case TILE_RESIDENTIAL: demand = res_demand(res, com, ind) > 0; break;
                case TILE_COMMERCIAL:  demand = com_demand(res, com) > 0; break;
                case TILE_INDUSTRIAL:  demand = ind_demand(res, ind) > 0; break;
            }

            // grow slowly and unevenly: a one in three chance each month,
            // one in two for homes on good land (near parks, away from smog)
            int odds = (tile == TILE_RESIDENTIAL && land_value[y][x] >= GOOD_LAND_VALUE) ? 2 : 3;
            if(demand && blit::random() % odds == 0) {
                lv++;
            }
        }
    }

    // recount after growing, for the HUD and stats screen
    count_city();

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

// The classic R C I demand meter: a bar per zone, up (green) when the
// city wants more of it, down (red) when it has too much.
void render_demand() {
    int demand[3] = {
        res_demand(res_levels, com_levels, ind_levels),
        com_demand(res_levels, com_levels),
        ind_demand(res_levels, ind_levels),
    };
    const char *letters[3] = {"R", "C", "I"};

    Rect box(0, screen.bounds.h - 9 - 24, 23, 24);
    screen.pen = Pen(0, 0, 0);
    screen.rectangle(box);
    int middle = box.y + 15; // bars grow up and down from here, 7 px each way
    for(int i = 0; i < 3; i++) {
        int x = box.x + 2 + i * 7;
        screen.pen = Pen(255, 255, 255);
        screen.text(letters[i], minimal_font, Point(x, box.y + 1));
        int h = std::max(-7, std::min(7, demand[i]));
        if(h > 0) {
            screen.pen = Pen(60, 220, 60);
            screen.rectangle(Rect(x, middle - h, 5, h));
        } else if(h < 0) {
            screen.pen = Pen(220, 60, 60);
            screen.rectangle(Rect(x, middle, 5, -h));
        }
        screen.pen = Pen(120, 120, 120);
        screen.rectangle(Rect(x, middle, 5, 1)); // zero line
    }
}

// Draws menu lines centred from y down, with arrows around the chosen one.
void render_menu(const char *const *items, int count, int y) {
    for(int i = 0; i < count; i++) {
        std::string line = items[i];
        if(i == menu_choice) {
            line = "> " + line + " <";
        }
        screen.pen = i == menu_choice ? Pen(255, 255, 255) : Pen(150, 150, 150);
        screen.text(line, minimal_font, Point(screen.bounds.w / 2, y + i * 12), true, TextAlign::top_center);
    }
}

// CONTINUE is only offered when there is a saved city.
const char *title_items_save[] = {"CONTINUE", "NEW CITY"};
const char *title_items_fresh[] = {"NEW CITY"};

void render_title() {
    screen.pen = Pen(0, 0, 40);
    screen.clear();

    // a skyline of fully grown buildings along the bottom
    int skyline[] = {SPRITE_RESIDENTIAL, SPRITE_COMMERCIAL, SPRITE_INDUSTRIAL};
    for(int x = 0; x < screen.bounds.w / TILE_SIZE; x++) {
        screen.sprite(skyline[x % 3] + MAX_LEVEL, Point(x * TILE_SIZE, screen.bounds.h - 2 * TILE_SIZE));
        screen.sprite(SPRITE_ROAD + 2 + 8, Point(x * TILE_SIZE, screen.bounds.h - TILE_SIZE)); // east-west road
    }

    screen.pen = Pen(255, 200, 0);
    screen.text("CHILI CITY", outline_font, Point(screen.bounds.w / 2, 20), true, TextAlign::top_center);

    if(has_save) {
        render_menu(title_items_save, 2, 56);
    } else {
        render_menu(title_items_fresh, 1, 56);
    }
}

const char *pause_items[] = {"RESUME", "SPEED", "SAVE + TITLE"};
constexpr int PAUSE_ITEMS = 3;

// The pause menu, drawn as a box over the frozen city.
void render_pause() {
    Rect box(14, 30, screen.bounds.w - 28, 64);
    screen.pen = Pen(0, 0, 0);
    screen.rectangle(box);
    screen.pen = Pen(255, 255, 255);
    screen.text("PAUSED", minimal_font, Point(screen.bounds.w / 2, box.y + 4), true, TextAlign::top_center);

    // show the current speed in the SPEED line
    std::string speed_line = std::string("SPEED: ") + speed_names[speed];
    const char *items[PAUSE_ITEMS] = {pause_items[0], speed_line.c_str(), pause_items[2]};
    render_menu(items, PAUSE_ITEMS, box.y + 20);
}

void render_city(uint32_t time);

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

    // load the saved city now, so CONTINUE on the title screen is instant
    has_save = load_city();
    if(!has_save) {
        new_city();
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

    switch(current_screen) {
        case SCREEN_TITLE:
            render_title();
            break;
        case SCREEN_PAUSE:
            render_city(time);
            render_pause();
            break;
        case SCREEN_CITY:
            if(show_stats) {
                render_stats();
            } else {
                render_city(time);
            }
            break;
    }
}

// The map with the cursor and the HUD on top.
void render_city(uint32_t time) {
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
                case TILE_POWER_LINE:  sprite = SPRITE_POWER_LINE + power_mask(mx, my); break;
                case TILE_POWER_PLANT: sprite = SPRITE_POWER_PLANT + level[my][mx]; break;
                case TILE_PARK:        sprite = SPRITE_PARK; break;
                case TILE_GRASS:
                default:               sprite = SPRITE_GRASS; break;
            }
            screen.sprite(sprite, Point(sx * TILE_SIZE, sy * TILE_SIZE));

            // zones without power blink a bolt, half a second on, half off
            if(is_zone(map[my][mx]) && !powered[my][mx] && (time / 500) % 2 == 0) {
                screen.sprite(SPRITE_NO_POWER, Point(sx * TILE_SIZE, sy * TILE_SIZE));
            }

            // overlay: a see-through colour on every tile
            Rect r(sx * TILE_SIZE, sy * TILE_SIZE, TILE_SIZE, TILE_SIZE);
            if(overlay == OVERLAY_POLLUTION && pollution[my][mx] > 0) {
                screen.pen = Pen(120, 70, 20, std::min(220, pollution[my][mx] * 2));
                screen.rectangle(r);
            } else if(overlay == OVERLAY_LAND_VALUE) {
                // red below the base value, green above it
                int diff = land_value[my][mx] - BASE_LAND_VALUE;
                if(diff < 0) {
                    screen.pen = Pen(220, 40, 40, std::min(220, -diff * 4));
                    screen.rectangle(r);
                } else if(diff > 0) {
                    screen.pen = Pen(40, 220, 255, std::min(220, diff * 6));
                    screen.rectangle(r);
                }
            }
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
    if(overlay != OVERLAY_NONE) {
        label(overlay_names[overlay], Point(screen.bounds.w, 9), TextAlign::top_right);
    }
    label(std::string(tool_names[tool]) + " $" + std::to_string(tool_costs[tool]),
          Point(0, screen.bounds.h), TextAlign::bottom_left);
    label(std::string(month_names[month % 12]) + " " + std::to_string(1900 + month / 12),
          Point(screen.bounds.w, screen.bounds.h), TextAlign::bottom_right);

    render_demand();

    // a short note after each autosave, just under the money
    if(was_saved && time - saved_time < 1000) {
        label("SAVED", Point(0, 9), TextAlign::top_left);
    }
}

// Up and down move through a menu of count lines, wrapping around.
void move_menu_choice(int count) {
    if(buttons.pressed & Button::DPAD_UP)   menu_choice = (menu_choice + count - 1) % count;
    if(buttons.pressed & Button::DPAD_DOWN) menu_choice = (menu_choice + 1) % count;
}

void update_title() {
    int count = has_save ? 2 : 1;
    move_menu_choice(count);

    if(buttons.pressed & Button::A) {
        bool continue_city = has_save && menu_choice == 0;
        if(!continue_city) {
            new_city();
        }
        current_screen = SCREEN_CITY;
        show_stats = false;
    }
}

void update_pause() {
    move_menu_choice(PAUSE_ITEMS);

    // MENU or B: back to the city
    if(buttons.pressed & (Button::MENU | Button::B)) {
        current_screen = SCREEN_CITY;
        return;
    }

    bool on_speed = menu_choice == 1;
    if(on_speed && (buttons.pressed & (Button::A | Button::DPAD_RIGHT))) {
        speed = Speed((speed + 1) % SPEED_COUNT);
    }
    if(on_speed && (buttons.pressed & Button::DPAD_LEFT)) {
        speed = Speed((speed + SPEED_COUNT - 1) % SPEED_COUNT);
    }

    if(buttons.pressed & Button::A) {
        if(menu_choice == 0) {
            current_screen = SCREEN_CITY;
        } else if(menu_choice == 2) {
            save_city();
            has_save = true;
            current_screen = SCREEN_TITLE;
            menu_choice = 0;
        }
    }
}

///////////////////////////////////////////////////////////////////////////
//
// update(time)
//
// This is called to update your game state. time is the 
// amount if milliseconds elapsed since the start of your game
//
void update(uint32_t time) {
    switch(current_screen) {
        case SCREEN_TITLE:
            update_title();
            last_month_time = time; // the city waits while a menu is open
            return;
        case SCREEN_PAUSE:
            update_pause();
            last_month_time = time;
            return;
        case SCREEN_CITY:
            break;
    }

    // MENU: pause menu
    if(buttons.pressed & Button::MENU) {
        current_screen = SCREEN_PAUSE;
        menu_choice = 0;
        return;
    }

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
    if(speed == SPEED_PAUSED) {
        last_month_time = time;
    } else if(time - last_month_time >= month_ms[speed]) {
        last_month_time = time;
        month++;
        simulate();

        // autosave once a year, every January. Not every month: on the
        // console the save lives in flash, which wears out with writes.
        if(month % 12 == 0) {
            save_city();
            was_saved = true;
            saved_time = time;
        }
    }

    // Y: stats screen on and off
    if(buttons.pressed & Button::Y) {
        show_stats = !show_stats;
    }
    if(show_stats) {
        return; // the city keeps running, but the map controls are off
    }

    // X: next overlay, wrapping back to none
    if(buttons.pressed & Button::X) {
        overlay = Overlay((overlay + 1) % OVERLAY_COUNT);
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
            case TOOL_POWER_LINE:
                if(tile == TILE_GRASS) tile = TILE_POWER_LINE;
                break;
            case TOOL_PARK:
                if(tile == TILE_GRASS) tile = TILE_PARK;
                break;
            case TOOL_POWER_PLANT:
                // the cursor is the top-left of a 2x2 plant; all four tiles must be grass
                if(cursor.x < MAP_W - 1 && cursor.y < MAP_H - 1
                   && map[cursor.y][cursor.x] == TILE_GRASS && map[cursor.y][cursor.x + 1] == TILE_GRASS
                   && map[cursor.y + 1][cursor.x] == TILE_GRASS && map[cursor.y + 1][cursor.x + 1] == TILE_GRASS) {
                    for(int part = 0; part < 4; part++) {
                        map[cursor.y + part / 2][cursor.x + part % 2] = TILE_POWER_PLANT;
                        level[cursor.y + part / 2][cursor.x + part % 2] = part;
                    }
                }
                break;
            case TOOL_BULLDOZER:
                if(tile == TILE_POWER_PLANT) {
                    // remove the whole plant: find its top-left from the quarter number
                    int part = level[cursor.y][cursor.x];
                    int left = cursor.x - part % 2;
                    int top = cursor.y - part / 2;
                    for(int p = 0; p < 4; p++) {
                        map[top + p / 2][left + p % 2] = TILE_GRASS;
                        level[top + p / 2][left + p % 2] = 0;
                    }
                }
                tile = TILE_GRASS;
                level[cursor.y][cursor.x] = 0;
                break;
            default:
                break;
        }
        // only pay when something actually changed
        if(tile != before) {
            money -= cost;
            // so the no-power bolts and overlays update right away
            update_power();
            update_land();
        }
    }
}
