#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>

#include "game.hpp"
#include "assets.hpp"

using namespace blit;

// Size of one map tile in pixels. 128 / 8 = 16 tiles across on VGC Zero.
constexpr int TILE_SIZE = 8;

// The whole city is bigger than the screen; the camera shows part of it.
constexpr int MAP_W = 64;
constexpr int MAP_H = 64;

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
    TILE_FIRE_STATION,
    TILE_POLICE_STATION,
};

// Where each tile's picture is in assets/tiles.png. The sheet is 8 sprites
// wide, so sprite n is at column n % 8, row n / 8.
constexpr int SPRITE_GRASS = 0;
constexpr int SPRITE_ROAD = 16;        // + which neighbours are roads (0..15)
constexpr int SPRITE_RESIDENTIAL = 32; // + level (0..3)
constexpr int SPRITE_POLICE_STATION = 36;
constexpr int SPRITE_COMMERCIAL = 40;  // + level
constexpr int SPRITE_INDUSTRIAL = 48;  // + level
constexpr int SPRITE_POWER_LINE = 56;  // + which neighbours carry power (0..15)
constexpr int SPRITE_POWER_PLANT = 72; // + quarter: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right
constexpr int SPRITE_NO_POWER = 76;    // bolt drawn over zones without power
constexpr int SPRITE_PARK = 77;
constexpr int SPRITE_FIRE_STATION = 78;
constexpr int SPRITE_FIRE = 79;        // flames drawn over a burning building

// What pressing A does. B switches to the next tool.
enum Tool {
    TOOL_ROAD = 0,
    TOOL_RESIDENTIAL,
    TOOL_COMMERCIAL,
    TOOL_INDUSTRIAL,
    TOOL_POWER_LINE,
    TOOL_POWER_PLANT,
    TOOL_PARK,
    TOOL_FIRE_STATION,
    TOOL_POLICE_STATION,
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
    500, // fire station
    500, // police station
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
    "FIRE STN",
    "POLICE",
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

// Months left until a burning building burns down; 0 = not on fire.
uint8_t fire[MAP_H][MAP_W];
constexpr int BURN_MONTHS = 3;

// Tiles a fire station reaches, worked out by update_land().
bool fire_covered[MAP_H][MAP_W];
constexpr int FIRE_STATION_RADIUS = 6; // tiles in each direction

// Crime, 0..255, worked out by update_land(): dense homes and shops breed
// it, police stations keep it down. Homes stop growing in high crime, and
// crime lowers land value.
uint8_t crime[MAP_H][MAP_W];
constexpr int CRIME_NO_GROWTH = 60;
constexpr int POLICE_STATION_RADIUS = 6;

// Disasters (fires) can be turned off in the pause menu.
bool disasters = true;

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
    OVERLAY_CRIME,
    OVERLAY_COUNT, // keep last
};
Overlay overlay = OVERLAY_NONE;
const char *overlay_names[OVERLAY_COUNT] = {"", "POLLUTION", "LAND VALUE", "CRIME"};

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
int plant_count = 0; // power plants, not tiles: each plant is 4 tiles
int fire_station_count = 0;
int police_station_count = 0;

// Money. Building costs money, taxes bring it in once a month.
int money = 5000;
int last_income = 0;    // taxes minus upkeep last month

// Tax rate in percent, set on the budget screen. Higher taxes bring in
// more money but make the city less attractive: every 2% above the
// default lowers the demand for all zones by one, every 2% below raises it.
constexpr int DEFAULT_TAX_RATE = 7;
constexpr int MAX_TAX_RATE = 20;
int tax_rate = DEFAULT_TAX_RATE;

// Monthly upkeep: every road tile and every power plant.
constexpr int ROAD_UPKEEP = 1;
constexpr int PLANT_UPKEEP = 50;
constexpr int FIRE_STATION_UPKEEP = 20;
constexpr int POLICE_STATION_UPKEEP = 20;

// Money in and out, added up over a year for the budget screen.
struct Budget {
    int taxes = 0;
    int roads = 0;
    int power = 0;
    int services = 0; // fire and police stations
};
Budget this_year, last_year;
bool have_last_year = false; // false until a year ends after a new game or load

// When the player last tried to build without enough money, to flash the HUD.
uint32_t broke_time = 0;
bool was_broke = false;

// Which screen the game is on. The stats screen (Y) is part of the city.
enum Screen {
    SCREEN_TITLE,
    SCREEN_CITY,
    SCREEN_PAUSE,
    SCREEN_BUDGET,
    SCREEN_WON,
    SCREEN_MINIMAP,
};
Screen current_screen = SCREEN_TITLE;

// Highlighted line in the title or pause menu.
int menu_choice = 0;

// true when a saved city was loaded, so the title offers CONTINUE.
bool has_save = false;

// Y shows the stats screen instead of the map.
bool show_stats = false;

// The goal of the game: grow from a hamlet to a capital. The rank only
// goes up, so a city that shrinks keeps the best rank it reached.
constexpr int RANK_COUNT = 5;
constexpr int RANK_CAPITAL = RANK_COUNT - 1;
const char *rank_names[RANK_COUNT] = {"HAMLET", "VILLAGE", "TOWN", "CITY", "CAPITAL"};
const int rank_population[RANK_COUNT] = {0, 500, 2000, 5000, 10000};
int rank = 0;

// The highest rank this population has earned.
int rank_for(int people) {
    int r = 0;
    while(r < RANK_CAPITAL && people >= rank_population[r + 1]) r++;
    return r;
}

// A short message in a box over the map, like "NOW A TOWN!".
std::string message;
uint32_t message_time = 0;
constexpr uint32_t MESSAGE_MS = 4000;

// Cursor position in map tiles, not pixels.
Point cursor(0, 0);

// Map tile shown in the top-left corner of the screen.
Point camera(0, 0);

// Holding a direction moves the cursor again after REPEAT_DELAY_MS, then
// every REPEAT_RATE_MS, like a keyboard key held down.
constexpr uint32_t REPEAT_DELAY_MS = 300;
constexpr uint32_t REPEAT_RATE_MS = 80;
uint32_t next_repeat_time = 0;

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
    // tiles still to visit; a tile is only added once, so MAP_W * MAP_H is
    // enough. Stored as y * MAP_W + x in 2 bytes, not as a Point (8 bytes),
    // to save RAM on the console.
    static uint16_t todo[MAP_W * MAP_H];
    int count = 0;

    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            powered[y][x] = map[y][x] == TILE_POWER_PLANT;
            if(powered[y][x]) {
                todo[count++] = y * MAP_W + x;
            }
        }
    }

    const Point steps[4] = {Point(0, -1), Point(1, 0), Point(0, 1), Point(-1, 0)};
    while(count > 0) {
        int index = todo[--count];
        Point p(index % MAP_W, index / MAP_W);
        for(auto &step : steps) {
            Point n = p + step;
            if(n.x < 0 || n.y < 0 || n.x >= MAP_W || n.y >= MAP_H) continue;
            if(powered[n.y][n.x] || !carries_power(map[n.y][n.x])) continue;
            powered[n.y][n.x] = true;
            todo[count++] = n.y * MAP_W + n.x;
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

// Marks every tile in the square of the given radius around (cx, cy).
void cover(bool grid[MAP_H][MAP_W], int cx, int cy, int radius) {
    for(int y = std::max(0, cy - radius); y <= std::min(MAP_H - 1, cy + radius); y++) {
        for(int x = std::max(0, cx - radius); x <= std::min(MAP_W - 1, cx + radius); x++) {
            grid[y][x] = true;
        }
    }
}

// Industry pollutes around it, more the bigger it is. Parks raise land
// value around them; pollution lowers it.
void update_land() {
    static uint8_t park_bonus[MAP_H][MAP_W];
    static bool police_covered[MAP_H][MAP_W];
    memset(pollution, 0, sizeof(pollution));
    memset(park_bonus, 0, sizeof(park_bonus));
    memset(fire_covered, 0, sizeof(fire_covered));
    memset(crime, 0, sizeof(crime));
    memset(police_covered, 0, sizeof(police_covered));

    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            if(map[y][x] == TILE_INDUSTRIAL && level[y][x] > 0) {
                // level 3 next door: 120 - 20 = 100, enough to drive homes out
                spread(pollution, x, y, level[y][x] * 40, 20);
            } else if(map[y][x] == TILE_PARK) {
                spread(park_bonus, x, y, 30, 10);
            } else if(map[y][x] == TILE_FIRE_STATION) {
                cover(fire_covered, x, y, FIRE_STATION_RADIUS);
            } else if(map[y][x] == TILE_POLICE_STATION) {
                cover(police_covered, x, y, POLICE_STATION_RADIUS);
            }
            // crowded homes and shops breed crime: only the bigger buildings
            // reach their neighbours, so it adds up where the city is dense
            if((map[y][x] == TILE_RESIDENTIAL || map[y][x] == TILE_COMMERCIAL) && level[y][x] > 0) {
                spread(crime, x, y, level[y][x] * 8, 8);
            }
        }
    }

    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            // police cut crime to a quarter in their reach
            if(police_covered[y][x]) {
                crime[y][x] /= 4;
            }
            int value = BASE_LAND_VALUE + park_bonus[y][x] - pollution[y][x] / 2 - crime[y][x] / 2;
            land_value[y][x] = std::max(0, std::min(255, value));
        }
    }
}

// Recounts the city totals shown on the HUD and the stats screen.
void count_city() {
    res_levels = com_levels = ind_levels = road_count = plant_count = fire_station_count = police_station_count = 0;
    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            switch(map[y][x]) {
                case TILE_RESIDENTIAL: res_levels += level[y][x]; break;
                case TILE_COMMERCIAL:  com_levels += level[y][x]; break;
                case TILE_INDUSTRIAL:  ind_levels += level[y][x]; break;
                case TILE_ROAD:        road_count++; break;
                case TILE_POWER_PLANT: if(level[y][x] == 0) plant_count++; break; // count top-left quarters
                case TILE_FIRE_STATION: fire_station_count++; break;
                case TILE_POLICE_STATION: police_station_count++; break;
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
constexpr uint8_t SAVE_VERSION = 5;

struct SaveData {
    uint32_t magic;
    uint8_t version;
    uint8_t map[MAP_H][MAP_W];
    uint8_t level[MAP_H][MAP_W];
    int32_t money;
    int32_t month;
    int32_t tax_rate; // new in version 2
    int32_t rank;     // new in version 3
    int32_t disasters; // new in version 4
};

// Versions 1 to 4 had a 32x32 map. Apart from that they had the same
// fields, each version adding some at the end, so an older file is the
// first part of SaveData32. This is how many bytes each one wrote;
// version 1 is Chili City v2.0.0. Version 5 grew the map to 64x64.
constexpr int OLD_MAP_SIZE = 32;
struct SaveData32 {
    uint32_t magic;
    uint8_t version;
    uint8_t map[OLD_MAP_SIZE][OLD_MAP_SIZE];
    uint8_t level[OLD_MAP_SIZE][OLD_MAP_SIZE];
    int32_t money;
    int32_t month;
    int32_t tax_rate;
    int32_t rank;
    int32_t disasters;
};
const size_t save32_sizes[5] = {
    0,                               // no version 0
    offsetof(SaveData32, tax_rate),  // 1
    offsetof(SaveData32, rank),      // 2
    offsetof(SaveData32, disasters), // 3
    sizeof(SaveData32),              // 4
};

// When the city was last saved, to show "SAVED" on the HUD for a moment.
uint32_t saved_time = 0;
bool was_saved = false;

void save_city() {
    // static: about 8 KB, better kept off the stack
    static SaveData save;
    save.magic = SAVE_MAGIC;
    save.version = SAVE_VERSION;
    memcpy(save.map, map, sizeof(map));
    memcpy(save.level, level, sizeof(level));
    save.money = money;
    save.month = month;
    save.tax_rate = tax_rate;
    save.rank = rank;
    save.disasters = disasters;
    write_save(save);
}

// Returns false when there is no usable save, so the game starts fresh.
bool load_city() {
    static SaveData save;
    static SaveData32 old;

    // read_save() fails when the file is shorter than asked for, so read
    // the magic and version first, then as many bytes as that version wrote
    if(!read_save(reinterpret_cast<char *>(&save), offsetof(SaveData, map))
       || save.magic != SAVE_MAGIC || save.version < 1 || save.version > SAVE_VERSION) {
        return false;
    }
    if(save.version == SAVE_VERSION) {
        if(!read_save(save)) return false;
    } else {
        // an older 32x32 city: newer fields keep these defaults when the file
        // is shorter, and the map goes in the top-left corner of the new one
        old = SaveData32{};
        old.tax_rate = DEFAULT_TAX_RATE;
        old.rank = -1; // -1: work it out from the population
        old.disasters = 1;
        if(!read_save(reinterpret_cast<char *>(&old), save32_sizes[save.version])) return false;

        save = SaveData{};
        for(int y = 0; y < OLD_MAP_SIZE; y++) {
            memcpy(save.map[y], old.map[y], OLD_MAP_SIZE);
            memcpy(save.level[y], old.level[y], OLD_MAP_SIZE);
        }
        save.money = old.money;
        save.month = old.month;
        save.tax_rate = old.tax_rate;
        save.rank = old.rank;
        save.disasters = old.disasters;
    }
    memcpy(map, save.map, sizeof(map));
    memcpy(level, save.level, sizeof(level));
    memset(fire, 0, sizeof(fire)); // fires are not saved; they go out
    money = save.money;
    month = save.month;
    disasters = save.disasters != 0;
    tax_rate = std::max(0, std::min(MAX_TAX_RATE, int(save.tax_rate)));
    this_year = last_year = Budget();
    have_last_year = false;
    count_city();
    rank = save.rank >= 0 ? std::min(int(save.rank), RANK_CAPITAL) : rank_for(population);
    message = "";
    return true;
}

// Empty map, starting money, January 1900.
void new_city() {
    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            map[y][x] = TILE_GRASS;
            level[y][x] = 0;
            fire[y][x] = 0;
        }
    }
    money = 5000;
    disasters = true;
    month = 0;
    last_income = 0;
    tax_rate = DEFAULT_TAX_RATE;
    this_year = last_year = Budget();
    have_last_year = false;
    rank = 0;
    message = "";
    cursor = Point(0, 0);
    camera = Point(0, 0);
    count_city();
}

// How much the tax rate pushes demand up (low taxes) or down (high taxes).
int tax_effect() {
    return (DEFAULT_TAX_RATE - tax_rate) / 2;
}

// How much the city wants more of each zone, from its building levels.
// Above 0 means that zone grows; the HUD meter shows the same numbers.
int res_demand(int res, int com, int ind) {
    return com + ind + 4 - res + tax_effect(); // people move in when there are jobs; the first few come anyway
}
int com_demand(int res, int com) {
    return res - com * 2 + tax_effect();       // shops need customers
}
int ind_demand(int res, int ind) {
    return res - ind + tax_effect();           // factories need workers
}

// Only buildings burn: zones with something built on them.
bool flammable(int x, int y) {
    return is_zone(map[y][x]) && level[y][x] > 0;
}

// One month of fires. Fire stations put out fires in their reach; other
// fires spread to the buildings next door and burn down to an empty lot.
// Returns true when a new fire broke out.
bool update_fires() {
    // fires that were burning at the start of the month; new ones wait
    static bool was_burning[MAP_H][MAP_W];
    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            was_burning[y][x] = fire[y][x] > 0;
        }
    }

    const Point steps[4] = {Point(0, -1), Point(1, 0), Point(0, 1), Point(-1, 0)};
    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            if(!was_burning[y][x]) continue;
            if(fire_covered[y][x] || !flammable(x, y)) {
                fire[y][x] = 0; // put out, or nothing left to burn
                continue;
            }
            // spread: one in three chance for each building next door
            for(auto &step : steps) {
                int nx = x + step.x, ny = y + step.y;
                if(nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
                if(fire[ny][nx] == 0 && flammable(nx, ny) && blit::random() % 3 == 0) {
                    fire[ny][nx] = BURN_MONTHS;
                }
            }
            if(--fire[y][x] == 0) {
                level[y][x] = 0; // burnt down to an empty lot
            }
        }
    }

    // now and then (one month in eight) a random building catches fire;
    // under a fire station's watch only one time in four
    if(!disasters || blit::random() % 8 != 0) return false;
    int x = blit::random() % MAP_W, y = blit::random() % MAP_H;
    if(!flammable(x, y) || fire[y][x] > 0) return false;
    if(fire_covered[y][x] && blit::random() % 4 != 0) return false;
    fire[y][x] = BURN_MONTHS;
    return true;
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

    // taxes are collected on last month's city, before it grows: at the
    // default 7% every building level pays $5 a month
    int taxes = (res + com + ind) * 5 * tax_rate / DEFAULT_TAX_RATE;

    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            uint8_t tile = map[y][x];
            if(tile != TILE_RESIDENTIAL && tile != TILE_COMMERCIAL && tile != TILE_INDUSTRIAL) {
                continue;
            }

            uint8_t &lv = level[y][x];
            if(fire[y][x] > 0) {
                continue; // nobody moves into a burning building
            }
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
            if(tile == TILE_RESIDENTIAL && crime[y][x] >= CRIME_NO_GROWTH) {
                continue; // too dangerous to move in
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

    int road_upkeep = road_count * ROAD_UPKEEP;
    int power_upkeep = plant_count * PLANT_UPKEEP;
    int service_upkeep = fire_station_count * FIRE_STATION_UPKEEP
                       + police_station_count * POLICE_STATION_UPKEEP;
    last_income = taxes - road_upkeep - power_upkeep - service_upkeep;
    money += last_income;

    this_year.taxes += taxes;
    this_year.roads += road_upkeep;
    this_year.power += power_upkeep;
    this_year.services += service_upkeep;
}

// Sound effects. Each is one short note on a 32blit audio channel: the
// note starts with trigger_attack() and fades out over decay_ms because
// sustain is 0. Actions use channel 0; the quiet month tick uses channel 1
// so it never cuts off a build sound.
enum Sound {
    SOUND_BUILD,
    SOUND_BULLDOZE,
    SOUND_BROKE,
    SOUND_MONTH,
    SOUND_MENU,
    SOUND_RANK,
    SOUND_FIRE,
};

void play(Sound sound) {
    struct Note { int channel; uint8_t waveform; uint16_t frequency; uint16_t decay_ms; uint16_t volume; };
    const Note notes[] = {
        {0, Waveform::SQUARE,   880,  80, 0x6000}, // build: short high blip
        {0, Waveform::NOISE,    600, 150, 0x8000}, // bulldoze: crunch
        {0, Waveform::SQUARE,   110, 250, 0x8000}, // not enough money: low buzz
        {1, Waveform::TRIANGLE, 1320, 30, 0x2000}, // new month: soft tick
        {0, Waveform::TRIANGLE, 660,  40, 0x6000}, // menu
        {0, Waveform::SINE,    1046, 600, 0x8000}, // new rank: long bright note
        {0, Waveform::NOISE,    200, 500, 0x9000}, // fire: low roar
    };
    const Note &n = notes[sound];
    AudioChannel &ch = channels[n.channel];
    ch.off();
    ch.waveforms = n.waveform;
    ch.frequency = n.frequency;
    ch.volume = n.volume;
    ch.attack_ms = 2;
    ch.decay_ms = n.decay_ms;
    ch.sustain = 0;
    ch.release_ms = 10;
    ch.trigger_attack();
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

    std::string next = rank < RANK_CAPITAL
        ? std::string("NEXT: ") + rank_names[rank + 1] + " AT " + std::to_string(rank_population[rank + 1])
        : "ALL RANKS REACHED";
    std::string lines[] = {
        std::string("CHILI CITY: ") + rank_names[rank],
        next,
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

const char *pause_items[] = {"RESUME", "SPEED", "BUDGET", "MINIMAP", "DISASTERS", "SAVE + TITLE"};
constexpr int PAUSE_ITEMS = 6;
enum PauseItem {PAUSE_RESUME, PAUSE_SPEED, PAUSE_BUDGET, PAUSE_MINIMAP, PAUSE_DISASTERS, PAUSE_SAVE};

// The pause menu, drawn as a box over the frozen city.
void render_pause() {
    Rect box(14, 12, screen.bounds.w - 28, 100);
    screen.pen = Pen(0, 0, 0);
    screen.rectangle(box);
    screen.pen = Pen(255, 255, 255);
    screen.text("PAUSED", minimal_font, Point(screen.bounds.w / 2, box.y + 4), true, TextAlign::top_center);

    // show the current speed in the SPEED line
    std::string speed_line = std::string("SPEED: ") + speed_names[speed];
    std::string disasters_line = std::string("DISASTERS: ") + (disasters ? "ON" : "OFF");
    const char *items[PAUSE_ITEMS] = {pause_items[0], speed_line.c_str(), pause_items[2],
                                      pause_items[3], disasters_line.c_str(), pause_items[5]};
    render_menu(items, PAUSE_ITEMS, box.y + 20);
}

// Money with a sign, like +120 or -40.
std::string signed_money(int value) {
    return (value < 0 ? "-$" : "+$") + std::to_string(abs(value));
}

// The budget screen: the tax rate, and where the money came from and went.
void render_budget() {
    screen.pen = Pen(0, 0, 40);
    screen.clear();
    screen.pen = Pen(255, 255, 255);

    // until a full year has been played, show this one so far
    bool full_year = have_last_year;
    const Budget &b = full_year ? last_year : this_year;

    std::string lines[] = {
        "BUDGET",
        "",
        "TAX RATE   < " + std::to_string(tax_rate) + "% >",
        "",
        full_year ? "LAST YEAR" : "THIS YEAR SO FAR",
        "TAXES      " + signed_money(b.taxes),
        "ROADS      -$" + std::to_string(b.roads),
        "POWER      -$" + std::to_string(b.power),
        "SERVICES   -$" + std::to_string(b.services),
        "TOTAL      " + signed_money(b.taxes - b.roads - b.power - b.services),
        "",
        "LEFT/RIGHT: TAX RATE",
        "A/B: BACK",
    };
    int y = 4;
    for(auto &line : lines) {
        screen.text(line, minimal_font, Point(4, y));
        y += 9;
    }
}

void render_city(uint32_t time);
void render_won();
void render_minimap(uint32_t time);

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
        case SCREEN_BUDGET:
            render_budget();
            break;
        case SCREEN_WON:
            render_won();
            break;
        case SCREEN_MINIMAP:
            render_minimap(time);
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
                case TILE_FIRE_STATION: sprite = SPRITE_FIRE_STATION; break;
                case TILE_POLICE_STATION: sprite = SPRITE_POLICE_STATION; break;
                case TILE_GRASS:
                default:               sprite = SPRITE_GRASS; break;
            }
            screen.sprite(sprite, Point(sx * TILE_SIZE, sy * TILE_SIZE));

            // flames over burning buildings, flickering left and right
            if(fire[my][mx] > 0) {
                screen.sprite(SPRITE_FIRE, Point(sx * TILE_SIZE, sy * TILE_SIZE),
                              (time / 200) % 2 ? SpriteTransform::HORIZONTAL : SpriteTransform::NONE);
            }
            // zones without power blink a bolt, half a second on, half off
            else if(is_zone(map[my][mx]) && !powered[my][mx] && (time / 500) % 2 == 0) {
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
            } else if(overlay == OVERLAY_CRIME && crime[my][mx] > 0) {
                screen.pen = Pen(230, 0, 0, std::min(220, crime[my][mx] * 3));
                screen.rectangle(r);
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

    // a message box in the middle of the screen for a few seconds
    if(!message.empty() && time - message_time < MESSAGE_MS) {
        Size size = screen.measure_text(message, minimal_font);
        Rect box(screen.bounds.w / 2 - size.w / 2 - 4, 40, size.w + 8, size.h + 8);
        screen.pen = Pen(0, 0, 0);
        screen.rectangle(box);
        screen.pen = Pen(255, 200, 0);
        screen.text(message, minimal_font, Point(box.x + 4, box.y + 4));
    }
}

// The minimap: the whole 64x64 city, each tile a 2x2 dot of its colour,
// with a frame showing the part that fits on the screen.
Point minimap_view(0, 0);
constexpr int MINIMAP_DOT = 2; // 64 tiles * 2 px = the whole 128 px screen

Pen minimap_colour(int x, int y) {
    if(fire[y][x] > 0) return Pen(255, 140, 0);
    switch(map[y][x]) {
        case TILE_ROAD:           return Pen(110, 110, 115);
        case TILE_RESIDENTIAL:    return level[y][x] ? Pen(110, 210, 110) : Pen(60, 140, 60);
        case TILE_COMMERCIAL:     return level[y][x] ? Pen(100, 150, 240) : Pen(60, 90, 160);
        case TILE_INDUSTRIAL:     return level[y][x] ? Pen(230, 200, 80) : Pen(150, 130, 60);
        case TILE_POWER_LINE:     return Pen(120, 80, 40);
        case TILE_POWER_PLANT:    return Pen(60, 60, 65);
        case TILE_PARK:           return Pen(40, 200, 60);
        case TILE_FIRE_STATION:   return Pen(220, 50, 40);
        case TILE_POLICE_STATION: return Pen(40, 70, 200);
        default:                  return Pen(20, 70, 20); // grass
    }
}

void render_minimap(uint32_t time) {
    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            screen.pen = minimap_colour(x, y);
            screen.rectangle(Rect(x * MINIMAP_DOT, y * MINIMAP_DOT, MINIMAP_DOT, MINIMAP_DOT));
        }
    }

    // the frame: what the screen will show, blinking so it stands out
    if((time / 300) % 2 == 0) {
        int view_w = screen.bounds.w / TILE_SIZE;
        int view_h = screen.bounds.h / TILE_SIZE;
        Rect frame(minimap_view.x * MINIMAP_DOT, minimap_view.y * MINIMAP_DOT,
                   view_w * MINIMAP_DOT, view_h * MINIMAP_DOT);
        screen.pen = Pen(255, 255, 255);
        screen.rectangle(Rect(frame.x, frame.y, frame.w, 1));
        screen.rectangle(Rect(frame.x, frame.y + frame.h - 1, frame.w, 1));
        screen.rectangle(Rect(frame.x, frame.y, 1, frame.h));
        screen.rectangle(Rect(frame.x + frame.w - 1, frame.y, 1, frame.h));
    }

    label("A: GO  B: BACK", Point(0, screen.bounds.h), TextAlign::bottom_left);
}

// Shown once, when the city becomes a capital. The game goes on after it.
void render_won() {
    screen.pen = Pen(0, 0, 40);
    screen.clear();

    screen.pen = Pen(255, 200, 0);
    screen.text("YOU WON!", outline_font, Point(screen.bounds.w / 2, 16), true, TextAlign::top_center);

    screen.pen = Pen(255, 255, 255);
    std::string lines[] = {
        "CHILI CITY IS A CAPITAL",
        "POPULATION " + std::to_string(population),
        std::string("IN ") + month_names[month % 12] + " " + std::to_string(1900 + month / 12),
        "",
        "A: KEEP PLAYING",
    };
    int y = 44;
    for(auto &line : lines) {
        screen.text(line, minimal_font, Point(screen.bounds.w / 2, y), true, TextAlign::top_center);
        y += 12;
    }
}

// Up and down move through a menu of count lines, wrapping around.
void move_menu_choice(int count) {
    if(buttons.pressed & Button::DPAD_UP)   menu_choice = (menu_choice + count - 1) % count;
    if(buttons.pressed & Button::DPAD_DOWN) menu_choice = (menu_choice + 1) % count;
    if(buttons.pressed & (Button::DPAD_UP | Button::DPAD_DOWN | Button::A)) {
        play(SOUND_MENU);
    }
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

    bool on_speed = menu_choice == PAUSE_SPEED;
    if(on_speed && (buttons.pressed & (Button::A | Button::DPAD_RIGHT))) {
        speed = Speed((speed + 1) % SPEED_COUNT);
    }
    if(on_speed && (buttons.pressed & Button::DPAD_LEFT)) {
        speed = Speed((speed + SPEED_COUNT - 1) % SPEED_COUNT);
    }
    bool on_disasters = menu_choice == PAUSE_DISASTERS;
    if(on_disasters && (buttons.pressed & (Button::A | Button::DPAD_LEFT | Button::DPAD_RIGHT))) {
        disasters = !disasters;
        if(!disasters) memset(fire, 0, sizeof(fire)); // off means off: put out every fire
    }

    if(buttons.pressed & Button::A) {
        if(menu_choice == PAUSE_RESUME) {
            current_screen = SCREEN_CITY;
        } else if(menu_choice == PAUSE_BUDGET) {
            current_screen = SCREEN_BUDGET;
        } else if(menu_choice == PAUSE_MINIMAP) {
            minimap_view = camera;
            current_screen = SCREEN_MINIMAP;
        } else if(menu_choice == PAUSE_SAVE) {
            save_city();
            has_save = true;
            current_screen = SCREEN_TITLE;
            menu_choice = 0;
        }
    }
}

// Uses the current tool on the tile under the cursor and pays for it.
void use_tool(uint32_t time) {
    uint8_t &tile = map[cursor.y][cursor.x];
    uint8_t before = tile;
    int cost = tool_costs[tool];
    if(money < cost) {
        was_broke = true;
        broke_time = time;
        play(SOUND_BROKE);
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
        case TOOL_FIRE_STATION:
            if(tile == TILE_GRASS) tile = TILE_FIRE_STATION;
            break;
        case TOOL_POLICE_STATION:
            if(tile == TILE_GRASS) tile = TILE_POLICE_STATION;
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
            fire[cursor.y][cursor.x] = 0;
            break;
        default:
            break;
    }
    // only pay when something actually changed
    if(tile != before) {
        money -= cost;
        play(tool == TOOL_BULLDOZER ? SOUND_BULLDOZE : SOUND_BUILD);

        // so the no-power bolts and overlays update right away
        update_power();
        update_land();
    }
}

// The D-pad as a move: a press moves once, and a held direction repeats
// after a short delay. Returns the directions to move this frame.
// buttons.pressed only has the buttons that went down since the last
// update; buttons.state has every button held right now.
uint32_t dpad_moves(uint32_t time) {
    const uint32_t dpad = Button::DPAD_LEFT | Button::DPAD_RIGHT | Button::DPAD_UP | Button::DPAD_DOWN;
    uint32_t move = buttons.pressed & dpad;
    if(move) {
        next_repeat_time = time + REPEAT_DELAY_MS;
    } else if((buttons.state & dpad) && time >= next_repeat_time) {
        move = buttons.state & dpad;
        next_repeat_time = time + REPEAT_RATE_MS;
    }
    return move;
}

// The D-pad moves the frame; A jumps there, B goes back to the pause menu.
void update_minimap(uint32_t time) {
    uint32_t move = dpad_moves(time);
    if(move & Button::DPAD_LEFT)  minimap_view.x--;
    if(move & Button::DPAD_RIGHT) minimap_view.x++;
    if(move & Button::DPAD_UP)    minimap_view.y--;
    if(move & Button::DPAD_DOWN)  minimap_view.y++;
    int view_w = screen.bounds.w / TILE_SIZE;
    int view_h = screen.bounds.h / TILE_SIZE;
    minimap_view.x = std::max(0, std::min(MAP_W - view_w, int(minimap_view.x)));
    minimap_view.y = std::max(0, std::min(MAP_H - view_h, int(minimap_view.y)));

    if(buttons.pressed & Button::A) {
        // the camera shows the frame, and the cursor goes to its middle
        camera = minimap_view;
        cursor = minimap_view + Point(view_w / 2, view_h / 2);
        current_screen = SCREEN_CITY;
        play(SOUND_MENU);
    } else if(buttons.pressed & (Button::B | Button::MENU)) {
        current_screen = SCREEN_PAUSE;
        play(SOUND_MENU);
    }
}

// Left and right change the tax rate; A or B go back to the pause menu.
void update_budget() {
    if(buttons.pressed & Button::DPAD_LEFT)  tax_rate = std::max(0, tax_rate - 1);
    if(buttons.pressed & Button::DPAD_RIGHT) tax_rate = std::min(MAX_TAX_RATE, tax_rate + 1);
    if(buttons.pressed & (Button::DPAD_LEFT | Button::DPAD_RIGHT)) {
        play(SOUND_MENU);
    }
    if(buttons.pressed & (Button::A | Button::B | Button::MENU)) {
        current_screen = SCREEN_PAUSE;
        play(SOUND_MENU);
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
        case SCREEN_BUDGET:
            update_budget();
            last_month_time = time;
            return;
        case SCREEN_MINIMAP:
            update_minimap(time);
            last_month_time = time;
            return;
        case SCREEN_WON:
            if(buttons.pressed & Button::A) {
                current_screen = SCREEN_CITY;
                play(SOUND_MENU);
            }
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

    uint32_t move = dpad_moves(time);
    Point old_cursor = cursor;
    if(move & Button::DPAD_LEFT)  cursor.x--;
    if(move & Button::DPAD_RIGHT) cursor.x++;
    if(move & Button::DPAD_UP)    cursor.y--;
    if(move & Button::DPAD_DOWN)  cursor.y++;

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
        bool new_fire = update_fires();
        simulate();
        if(new_fire) {
            message = "FIRE!";
            message_time = time;
            play(SOUND_FIRE);
        }

        // a new rank: tell the player, and at the top rank show the win screen
        int new_rank = rank_for(population);
        if(new_rank > rank) {
            rank = new_rank;
            message = std::string("NOW A ") + rank_names[rank] + "!";
            message_time = time;
            play(SOUND_RANK);
            if(rank == RANK_CAPITAL) {
                current_screen = SCREEN_WON;
            }
        }
        // a new year: this year's budget becomes last year's
        if(month % 12 == 0) {
            last_year = this_year;
            this_year = Budget();
            have_last_year = true;
        }
        play(SOUND_MONTH);

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

    // A: use the tool on the tile under the cursor. Holding A while moving
    // paints with the tool, one tile per step. Not for power plants: one
    // $1500 plant per step is never what you meant.
    bool moved = cursor != old_cursor;
    if(buttons.pressed & Button::A) {
        use_tool(time);
    } else if((buttons.state & Button::A) && moved && tool != TOOL_POWER_PLANT) {
        use_tool(time);
    }
}
