#include <algorithm>
#include <cmath>
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
    TILE_TREES, // a forest: bulldoze it to build
    TILE_WATER, // a lake: nothing can be built on it
};

// Where each tile's picture is in assets/tiles.png. The sheet is 8 sprites
// wide, so sprite n is at column n % 8, row n / 8.
constexpr int SPRITE_GRASS = 0;
constexpr int SPRITE_TREES = 1;
constexpr int SPRITE_WATER = 2;        // + wave frame (0..1)
constexpr int SPRITE_BULLDOZER = 4;    // tool icon in the tool bar
constexpr int SPRITE_BRIDGE = 5;       // + 0 east-west, + 1 north-south
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

// A road built on water is a bridge: a TILE_ROAD with level 1, so it
// joins roads like any other road and turns back into water when
// bulldozed. It costs more than a road on land.
constexpr int BRIDGE_COST = 50;

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

// Each tool's picture in the tool bar (hold B).
const int tool_icons[TOOL_COUNT] = {
    SPRITE_ROAD + 2 + 8,       // road, east-west
    SPRITE_RESIDENTIAL + 1,
    SPRITE_COMMERCIAL + 1,
    SPRITE_INDUSTRIAL + 1,
    SPRITE_POWER_LINE + 2 + 8, // power line, east-west
    SPRITE_POWER_PLANT,        // top-left quarter of the plant
    SPRITE_PARK,
    SPRITE_FIRE_STATION,
    SPRITE_POLICE_STATION,
    SPRITE_BULLDOZER,
};

// Tapping B picks the next tool. Holding B shows the tool bar, where left
// and right pick any tool in both directions; the cursor stays put.
constexpr uint32_t TOOL_BAR_DELAY_MS = 250;
bool b_held_in_city = false;  // B went down on the map
uint32_t b_down_time = 0;
bool tool_bar_used = false;   // left/right picked a tool while B was held

bool tool_bar_open(uint32_t time) {
    return b_held_in_city && (buttons.state & Button::B)
        && (tool_bar_used || time - b_down_time >= TOOL_BAR_DELAY_MS);
}

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
    SCREEN_HELP,
    SCREEN_INTRO, // first launch: how long the tutorial takes, start or skip
    SCREEN_CONFIRM, // NEW CITY or TUTORIAL over a saved city: really?
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

// The tutorial: in a new city, a box at the top of the screen walks
// through the first steps. Each step ends when its goal is met. The steps
// follow the order of the tools, so each one is a single B press away.
enum HintStep {
    HINT_SCROLL = 0,
    HINT_ROAD,
    HINT_HOMES,
    HINT_JOBS,
    HINT_LINES,
    HINT_PLANT,
    HINT_DONE, // keep last; also used for loaded or skipped cities: no hints
};
HintStep hint_step = HINT_DONE;
const char *hint_lines[HINT_DONE][2] = {
    {"D-PAD: MOVE CURSOR", "PUSH EDGE: MAP SCROLLS"},
    {"A: BUILD A ROAD", "HOLD A + MOVE: LONGER"},
    {"B: PICK RES ZONE", "BUILD IT BY THE ROAD"},
    {"B: PICK COM OR IND", "ZONE BY THE ROAD: JOBS"},
    {"B: PICK POWER LINE", "LAY IT FROM A ZONE"},
    {"B: PICK PLANT ($1500)", "PUT IT BY THE LINE"},
};
// shown instead of the last step when a plant is built but powers nothing
const char *hint_plant_unlinked[2] = {"LINK THE PLANT TO", "A ZONE WITH LINES"};

// Player settings that are not part of a city: whether the tutorial start
// screen was answered (shown on the very first launch) and whether music
// plays. Kept in their own save slot, so the city save (slot 0) keeps its
// format.
constexpr int SETTINGS_SLOT = 1;
constexpr uint32_t SETTINGS_MAGIC = 0x54455343; // "CSET"
constexpr uint8_t SETTINGS_VERSION = 2;
struct GameSettings {
    uint32_t magic;
    uint8_t version;
    uint8_t tutorial_seen; // the start screen was answered, start or skip
    uint8_t music;         // new in version 2 (v3.2.0)
};
bool tutorial_seen = false;
bool music_on = true;

void save_game_settings() {
    GameSettings settings{};
    settings.magic = SETTINGS_MAGIC;
    settings.version = SETTINGS_VERSION;
    settings.tutorial_seen = tutorial_seen;
    settings.music = music_on;
    write_save(settings, SETTINGS_SLOT);
}

// false when there is no settings slot yet (first launch, or a save made
// before v3.1.0)
bool load_game_settings() {
    GameSettings settings{};
    if(!read_save(settings, SETTINGS_SLOT) || settings.magic != SETTINGS_MAGIC) {
        return false;
    }
    tutorial_seen = settings.tutorial_seen != 0;
    music_on = settings.version < 2 || settings.music != 0; // version 1 had no music switch
    return true;
}

// HELP in the pause menu: pages of controls and rules, left/right to turn.
int help_page = 0;
constexpr int HELP_PAGES = 3;
const char *help_text[HELP_PAGES][11] = {
    {
        "CONTROLS        1/3",
        "",
        "D-PAD  MOVE CURSOR",
        "  AT EDGE: MAP SCROLLS",
        "A      BUILD",
        "  HOLD A + MOVE: PAINT",
        "B      NEXT TOOL",
        "  HOLD B + < >: TOOL BAR",
        "X      OVERLAY",
        "Y      STATS",
        "MENU   PAUSE MENU",
    },
    {
        "HOW TO PLAY     2/3",
        "",
        "ZONES ONLY GROW NEXT",
        "TO A ROAD, WITH POWER.",
        "A PLANT POWERS ZONES",
        "LINKED BY LINES OR",
        "BY OTHER ZONES.",
        "R C I METER: GREEN =",
        "CITY WANTS MORE OF IT,",
        "RED = TOO MUCH.",
        "X: SEE LAND VALUE",
    },
    {
        "TIPS            3/3",
        "FACTORIES POLLUTE:",
        "KEEP HOMES AWAY.",
        "PARKS, TREES, LAKES",
        "RAISE LAND VALUE.",
        "FIRE AND POLICE",
        "STATIONS GUARD 6 TILES",
        "HIGH TAX SLOWS GROWTH",
        "BULLDOZE TREES FIRST",
        "ROAD ON WATER: BRIDGE",
        "GOAL: 10000 PEOPLE",
    },
};

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

// Where the camera was when the city opened. Until the player scrolls
// away from it, blinking arrows at the screen edges show that the map
// goes on past the screen.
Point start_camera(0, 0);
bool scrolled = false;

// Puts the cursor on a tile and the camera around it, as close to the
// middle of the screen as the map edges allow.
void view_at(Point tile) {
    int view_w = screen.bounds.w / TILE_SIZE;
    int view_h = screen.bounds.h / TILE_SIZE;
    cursor = tile;
    camera = Point(std::max(0, std::min(MAP_W - view_w, int(cursor.x) - view_w / 2)),
                   std::max(0, std::min(MAP_H - view_h, int(cursor.y) - view_h / 2)));
    start_camera = camera;
    scrolled = false;
}

// The cursor in the middle of the map, so there is room to build in every
// direction and the edge arrows show.
void center_view() {
    view_at(Point(MAP_W / 2, MAP_H / 2));
}

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
            } else if(map[y][x] == TILE_TREES || map[y][x] == TILE_WATER || (map[y][x] == TILE_ROAD && level[y][x])) {
                // nature is nice to live by: +10 next door
                spread(park_bonus, x, y, 20, 10);
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
constexpr uint8_t SAVE_VERSION = 6;

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
    int32_t cursor_x;  // new in version 6; -1: centre the view
    int32_t cursor_y;
    uint8_t fire[MAP_H][MAP_W]; // new in version 6
};

// Versions 1 to 4 had a 32x32 map. Apart from that they had the same
// fields, each version adding some at the end, so an older file is the
// first part of SaveData32. This is how many bytes each one wrote;
// version 1 is Chili City v2.0.0. Version 5 grew the map to 64x64, and
// version 6 (v3.2.0) added the cursor and the fires at the end.
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
    save.cursor_x = cursor.x;
    save.cursor_y = cursor.y;
    memcpy(save.fire, fire, sizeof(fire));
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
    if(save.version >= 5) {
        // a 64x64 city: version 5 ends before the cursor, which then stays
        // -1 (centre the view), and fires stay out
        size_t size = save.version == SAVE_VERSION ? sizeof(SaveData) : offsetof(SaveData, cursor_x);
        save = SaveData{};
        save.cursor_x = save.cursor_y = -1;
        if(!read_save(reinterpret_cast<char *>(&save), size)) return false;
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
        save.cursor_x = save.cursor_y = -1;
    }
    memcpy(map, save.map, sizeof(map));
    memcpy(level, save.level, sizeof(level));
    memcpy(fire, save.fire, sizeof(fire)); // all zero (out) before version 6
    money = save.money;
    month = save.month;
    disasters = save.disasters != 0;
    tax_rate = std::max(0, std::min(MAX_TAX_RATE, int(save.tax_rate)));
    this_year = last_year = Budget();
    have_last_year = false;
    count_city();
    rank = save.rank >= 0 ? std::min(int(save.rank), RANK_CAPITAL) : rank_for(population);
    message = "";
    hint_step = HINT_DONE; // a saved city needs no tutorial
    if(save.cursor_x >= 0 && save.cursor_x < MAP_W && save.cursor_y >= 0 && save.cursor_y < MAP_H) {
        view_at(Point(save.cursor_x, save.cursor_y));
    } else {
        center_view();
    }
    return true;
}

// Lakes and forests, different in every new city. The middle of the map,
// where the cursor starts, stays grass so the first roads and zones fit.
constexpr int CLEAR_RADIUS = 6; // tiles from the middle kept as grass
constexpr int LAKES = 4;
constexpr int FORESTS = 12;

bool in_clear_middle(int x, int y) {
    return abs(x - MAP_W / 2) <= CLEAR_RADIUS && abs(y - MAP_H / 2) <= CLEAR_RADIUS;
}

// A river two tiles wide winds across the map, top to bottom or left to
// right. Every RIVER_FORD_EVERY tiles a ford of grass lets roads cross,
// and the clear middle makes one more.
constexpr int RIVER_FORD_EVERY = 12;

void generate_river() {
    bool down = blit::random() % 2; // flows top to bottom, else left to right
    int across = MAP_W / 4 + blit::random() % (MAP_W / 2); // where it starts
    int ford_offset = blit::random() % RIVER_FORD_EVERY;
    int length = down ? MAP_H : MAP_W;
    int width = down ? MAP_W : MAP_H;
    for(int along = 0; along < length; along++) {
        across = std::max(1, std::min(width - 3, across + int(blit::random() % 3) - 1));
        if((along + ford_offset) % RIVER_FORD_EVERY == 0) continue; // a ford
        for(int w = 0; w < 2; w++) {
            int x = down ? across + w : along;
            int y = down ? along : across + w;
            if(!in_clear_middle(x, y)) map[y][x] = TILE_WATER;
        }
    }
}

void generate_terrain() {
    generate_river();
    // lakes: rough circles of radius 2 to 5, away from the middle
    for(int i = 0; i < LAKES; i++) {
        int r = 2 + blit::random() % 4;
        int cx = blit::random() % MAP_W, cy = blit::random() % MAP_H;
        for(int y = cy - r - 1; y <= cy + r + 1; y++) {
            for(int x = cx - r - 1; x <= cx + r + 1; x++) {
                if(x < 0 || y < 0 || x >= MAP_W || y >= MAP_H || in_clear_middle(x, y)) continue;
                int d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                if(d <= r * r + int(blit::random() % (r + 1))) { // a ragged shore
                    map[y][x] = TILE_WATER;
                }
            }
        }
    }
    // forests: patches of radius 2 to 4 on grass, three trees in four
    for(int i = 0; i < FORESTS; i++) {
        int r = 2 + blit::random() % 3;
        int cx = blit::random() % MAP_W, cy = blit::random() % MAP_H;
        for(int y = cy - r; y <= cy + r; y++) {
            for(int x = cx - r; x <= cx + r; x++) {
                if(x < 0 || y < 0 || x >= MAP_W || y >= MAP_H || in_clear_middle(x, y)) continue;
                int d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                if(d <= r * r && map[y][x] == TILE_GRASS && blit::random() % 4 != 0) {
                    map[y][x] = TILE_TREES;
                }
            }
        }
    }
}

// A new map, starting money, January 1900. With tutorial, the hint box
// walks through the first steps.
void new_city(bool tutorial) {
    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            map[y][x] = TILE_GRASS;
            level[y][x] = 0;
            fire[y][x] = 0;
        }
    }
    generate_terrain();
    money = 5000;
    disasters = true;
    month = 0;
    last_income = 0;
    tax_rate = DEFAULT_TAX_RATE;
    this_year = last_year = Budget();
    have_last_year = false;
    rank = 0;
    message = "";
    hint_step = tutorial ? HINT_SCROLL : HINT_DONE;
    center_view();
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

// Background music: two short chiptune loops written for Chili City, a
// calm one for the title screens and a bouncy one for the city. Each step
// plays one note (MIDI number, 0 = rest) on channel 2 (melody) and
// channel 3 (bass), so they never cut off the sound effects on 0 and 1.
struct Track {
    const uint8_t *lead;
    const uint8_t *bass;
    int steps;
    uint32_t step_ms;
};

// Title: C, A minor, F and G arpeggios.
const uint8_t title_lead[32] = {
    72, 76, 79, 84, 79, 76, 72, 0,   69, 72, 76, 81, 76, 72, 69, 0,
    65, 69, 72, 77, 72, 69, 65, 0,   67, 71, 74, 79, 74, 71, 67, 0,
};
const uint8_t title_bass[32] = {
    48, 0, 0, 0, 55, 0, 0, 0,   45, 0, 0, 0, 52, 0, 0, 0,
    41, 0, 0, 0, 48, 0, 0, 0,   43, 0, 0, 0, 50, 0, 0, 0,
};

// City: four bars of tune over C, G, A minor and F.
const uint8_t city_lead[64] = {
    72, 0, 76, 0, 79, 0, 76, 74,   72, 0, 74, 76, 74, 0, 0, 0,
    69, 0, 72, 0, 76, 0, 74, 72,   71, 0, 72, 74, 72, 0, 0, 0,
    77, 0, 76, 0, 74, 0, 72, 0,    74, 0, 76, 77, 79, 0, 0, 0,
    76, 0, 74, 0, 72, 0, 71, 0,    72, 0, 74, 71, 72, 0, 0, 0,
};
const uint8_t city_bass[64] = {
    48, 0, 55, 0, 48, 0, 55, 0,    53, 0, 60, 0, 55, 0, 50, 0,
    45, 0, 52, 0, 43, 0, 50, 0,    43, 0, 50, 0, 48, 0, 55, 0,
    53, 0, 60, 0, 53, 0, 60, 0,    55, 0, 50, 0, 55, 0, 50, 0,
    48, 0, 55, 0, 43, 0, 50, 0,    43, 0, 50, 0, 48, 0, 48, 0,
};

enum Music { MUSIC_NONE = -1, MUSIC_TITLE = 0, MUSIC_CITY };
const Track tracks[2] = {
    {title_lead, title_bass, 32, 220},
    {city_lead, city_bass, 64, 150},
};

// Pitch of a MIDI note: A4 (69) is 440 Hz, 12 notes to an octave.
uint16_t note_hz(uint8_t note) {
    return uint16_t(440.0f * powf(2.0f, float(note - 69) / 12.0f));
}

void play_note(int channel, uint8_t note, uint8_t waveform, uint16_t volume, uint16_t decay_ms) {
    AudioChannel &ch = channels[channel];
    ch.off();
    ch.waveforms = waveform;
    ch.pulse_width = 0x5000; // a slightly thin square, softer than 50%
    ch.frequency = note_hz(note);
    ch.volume = volume;
    ch.attack_ms = 5;
    ch.decay_ms = decay_ms;
    ch.sustain = 0;
    ch.release_ms = 20;
    ch.trigger_attack();
}

Music music = MUSIC_NONE;
int music_step = 0;
uint32_t next_step_time = 0;

// Called every update: picks the track for the screen and plays its notes
// on time. Quiet next to the sound effects, which stay easy to hear.
void update_music(uint32_t time, Music want) {
    if(!music_on) want = MUSIC_NONE;
    if(want != music) {
        music = want;
        music_step = 0;
        next_step_time = time;
        channels[2].off();
        channels[3].off();
    }
    if(music == MUSIC_NONE || time < next_step_time) return;

    const Track &track = tracks[music];
    if(track.lead[music_step]) play_note(2, track.lead[music_step], Waveform::SQUARE, 0x1800, 140);
    if(track.bass[music_step]) play_note(3, track.bass[music_step], Waveform::TRIANGLE, 0x3000, 260);
    music_step = (music_step + 1) % track.steps;
    next_step_time += track.step_ms;
    if(time >= next_step_time) next_step_time = time + track.step_ms; // fell behind: catch up, don't rush
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
void render_menu(const char *const *items, int count, int y, int spacing = 12) {
    for(int i = 0; i < count; i++) {
        std::string line = items[i];
        if(i == menu_choice) {
            line = "> " + line + " <";
        }
        screen.pen = i == menu_choice ? Pen(255, 255, 255) : Pen(150, 150, 150);
        screen.text(line, minimal_font, Point(screen.bounds.w / 2, y + i * spacing), true, TextAlign::top_center);
    }
}

// CONTINUE is only offered when there is a saved city. TUTORIAL starts a
// new city with the tutorial, for anyone who skipped it or wants it again.
const char *title_items_save[] = {"CONTINUE", "NEW CITY", "TUTORIAL"};
const char *title_items_fresh[] = {"NEW CITY", "TUTORIAL"};

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
        render_menu(title_items_save, 3, 52);
    } else {
        render_menu(title_items_fresh, 2, 56);
    }
}

// How many steps the tutorial has and about how long it takes, for the
// start screen. Six short steps, each one a button press or two away.
constexpr int TUTORIAL_STEPS = HINT_DONE;
const char *const TUTORIAL_LENGTH = "ABOUT 1 MINUTE";

// First launch: welcome, how long the tutorial takes, and how to skip it.
void render_intro(uint32_t time) {
    screen.pen = Pen(0, 0, 40);
    screen.clear();

    screen.pen = Pen(255, 200, 0);
    screen.text("CHILI CITY", outline_font, Point(screen.bounds.w / 2, 8), true, TextAlign::top_center);

    std::string steps = std::to_string(TUTORIAL_STEPS) + " STEPS, " + TUTORIAL_LENGTH;
    const char *lines[] = {
        "WELCOME, MAYOR!",
        "",
        "SHORT TUTORIAL:",
        steps.c_str(),
    };
    screen.pen = Pen(255, 255, 255);
    int y = 32;
    for(auto line : lines) {
        screen.text(line, minimal_font, Point(screen.bounds.w / 2, y), true, TextAlign::top_center);
        y += 10;
    }

    // the start choice blinks, so the screen is never still
    if((time / 400) % 2 == 0) {
        screen.pen = Pen(60, 220, 60);
        screen.text("A: START TUTORIAL", minimal_font, Point(screen.bounds.w / 2, 80), true, TextAlign::top_center);
    }
    screen.pen = Pen(255, 255, 255);
    screen.text("B: SKIP", minimal_font, Point(screen.bounds.w / 2, 92), true, TextAlign::top_center);
    screen.pen = Pen(150, 150, 150);
    screen.text("REPLAY: TITLE MENU", minimal_font, Point(screen.bounds.w / 2, 112), true, TextAlign::top_center);
}

// NEW CITY or TUTORIAL while a city is saved: the next save would replace
// it, so ask first. B (the safe answer) is the default.
bool confirm_tutorial = false; // what A starts: the tutorial or a plain new city

void render_confirm() {
    screen.pen = Pen(0, 0, 40);
    screen.clear();
    const char *lines[] = {
        "START A NEW CITY?",
        "",
        "YOUR SAVED CITY IS",
        "REPLACED AT THE",
        "NEXT SAVE.",
    };
    screen.pen = Pen(255, 255, 255);
    int y = 24;
    for(auto line : lines) {
        screen.text(line, minimal_font, Point(screen.bounds.w / 2, y), true, TextAlign::top_center);
        y += 10;
    }
    screen.pen = Pen(255, 120, 80);
    screen.text("A: YES, NEW CITY", minimal_font, Point(screen.bounds.w / 2, 86), true, TextAlign::top_center);
    screen.pen = Pen(60, 220, 60);
    screen.text("B: NO, KEEP IT", minimal_font, Point(screen.bounds.w / 2, 98), true, TextAlign::top_center);
}

const char *pause_items[] = {"RESUME", "SPEED", "BUDGET", "MINIMAP", "DISASTERS", "MUSIC", "HELP", "SAVE + TITLE"};
constexpr int PAUSE_ITEMS = 8;
enum PauseItem {PAUSE_RESUME, PAUSE_SPEED, PAUSE_BUDGET, PAUSE_MINIMAP, PAUSE_DISASTERS, PAUSE_MUSIC, PAUSE_HELP, PAUSE_SAVE};

// The pause menu, drawn as a box over the frozen city.
void render_pause() {
    Rect box(14, 6, screen.bounds.w - 28, 112);
    screen.pen = Pen(0, 0, 0);
    screen.rectangle(box);
    screen.pen = Pen(255, 255, 255);
    screen.text("PAUSED", minimal_font, Point(screen.bounds.w / 2, box.y + 4), true, TextAlign::top_center);

    // show the current speed in the SPEED line
    std::string speed_line = std::string("SPEED: ") + speed_names[speed];
    std::string disasters_line = std::string("DISASTERS: ") + (disasters ? "ON" : "OFF");
    std::string music_line = std::string("MUSIC: ") + (music_on ? "ON" : "OFF");
    const char *items[PAUSE_ITEMS] = {pause_items[0], speed_line.c_str(), pause_items[2],
                                      pause_items[3], disasters_line.c_str(), music_line.c_str(),
                                      pause_items[6], pause_items[7]};
    render_menu(items, PAUSE_ITEMS, box.y + 18, 11);
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
void render_help();
void save_on_quit();

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
        new_city(false);
    }

    // the very first launch opens on the tutorial start screen. A city
    // saved by an older version means the game was played before.
    on_quit = save_on_quit;

    if(!load_game_settings()) {
        tutorial_seen = has_save;
    }
    if(!tutorial_seen) {
        current_screen = SCREEN_INTRO;
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
        case SCREEN_HELP:
            render_help();
            break;
        case SCREEN_INTRO:
            render_intro(time);
            break;
        case SCREEN_CONFIRM:
            render_confirm();
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
                case TILE_ROAD:
                    if(level[my][mx]) {
                        // a bridge runs north-south when it only joins roads that way
                        int mask = road_mask(mx, my);
                        bool north_south = (mask & (1 | 4)) && !(mask & (2 | 8));
                        sprite = SPRITE_BRIDGE + (north_south ? 1 : 0);
                    } else {
                        sprite = SPRITE_ROAD + road_mask(mx, my);
                    }
                    break;
                case TILE_RESIDENTIAL: sprite = SPRITE_RESIDENTIAL + level[my][mx]; break;
                case TILE_COMMERCIAL:  sprite = SPRITE_COMMERCIAL + level[my][mx]; break;
                case TILE_INDUSTRIAL:  sprite = SPRITE_INDUSTRIAL + level[my][mx]; break;
                case TILE_POWER_LINE:  sprite = SPRITE_POWER_LINE + power_mask(mx, my); break;
                case TILE_POWER_PLANT: sprite = SPRITE_POWER_PLANT + level[my][mx]; break;
                case TILE_PARK:        sprite = SPRITE_PARK; break;
                case TILE_FIRE_STATION: sprite = SPRITE_FIRE_STATION; break;
                case TILE_POLICE_STATION: sprite = SPRITE_POLICE_STATION; break;
                case TILE_TREES:       sprite = SPRITE_TREES; break;
                // water ripples: neighbouring tiles swap frames out of step
                case TILE_WATER:       sprite = SPRITE_WATER + (time / 700 + mx + my) % 2; break;
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

    // cursor: a frame around the selected tile, in screen pixels. It
    // pulses, white and one pixel bigger, then yellow and tile sized, so
    // the eye finds it at once. It never disappears, so moving and
    // painting stay easy to follow.
    bool pulse = (time / 300) % 2 == 0;
    screen.pen = pulse ? Pen(255, 255, 255) : Pen(255, 200, 0);
    int grow = pulse ? 1 : 0;
    int px = (cursor.x - camera.x) * TILE_SIZE - grow;
    int py = (cursor.y - camera.y) * TILE_SIZE - grow;
    int size = TILE_SIZE + 2 * grow;
    screen.rectangle(Rect(px, py, size, 1));            // top
    screen.rectangle(Rect(px, py + size - 1, size, 1)); // bottom
    screen.rectangle(Rect(px, py, 1, size));            // left
    screen.rectangle(Rect(px + size - 1, py, 1, size)); // right

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

    if(tool_bar_open(time)) {
        // a row of tool pictures above the bottom labels, current one framed
        int icon = TILE_SIZE + 2;
        int bar_w = TOOL_COUNT * icon + 2;
        Rect bar((screen.bounds.w - bar_w) / 2, screen.bounds.h - 9 - 21, bar_w, 21);
        screen.pen = Pen(0, 0, 0);
        screen.rectangle(bar);
        screen.pen = Pen(150, 150, 150);
        screen.text("< > PICK TOOL", minimal_font, Point(screen.bounds.w / 2, bar.y + 1), true, TextAlign::top_center);
        for(int t = 0; t < TOOL_COUNT; t++) {
            Point p(bar.x + 2 + t * icon, bar.y + 11);
            screen.sprite(tool_icons[t], p);
            if(t == tool) {
                screen.pen = Pen(255, 200, 0);
                screen.rectangle(Rect(p.x - 1, p.y - 1, TILE_SIZE + 2, 1));
                screen.rectangle(Rect(p.x - 1, p.y + TILE_SIZE, TILE_SIZE + 2, 1));
                screen.rectangle(Rect(p.x - 1, p.y - 1, 1, TILE_SIZE + 2));
                screen.rectangle(Rect(p.x + TILE_SIZE, p.y - 1, 1, TILE_SIZE + 2));
            }
        }
    } else {
        render_demand();
    }

    // a short note after each autosave, just under the money
    if(was_saved && time - saved_time < 1000) {
        label("SAVED", Point(0, 9), TextAlign::top_left);
    }

    // until the player scrolls: blinking arrows on the edges where the
    // map goes on, to show it is bigger than the screen
    if(!scrolled && (time / 400) % 2 == 0) {
        int view_w = screen.bounds.w / TILE_SIZE;
        int view_h = screen.bounds.h / TILE_SIZE;
        int w = screen.bounds.w, h = screen.bounds.h;
        int cx = w / 2, cy = h / 2;
        screen.pen = Pen(255, 255, 255);
        if(camera.y > 0)              screen.triangle(Point(cx, 11), Point(cx - 4, 15), Point(cx + 4, 15));
        if(camera.y < MAP_H - view_h) screen.triangle(Point(cx, h - 12), Point(cx - 4, h - 16), Point(cx + 4, h - 16));
        if(camera.x > 0)              screen.triangle(Point(1, cy), Point(5, cy - 4), Point(5, cy + 4));
        if(camera.x < MAP_W - view_w) screen.triangle(Point(w - 2, cy), Point(w - 6, cy - 4), Point(w - 6, cy + 4));
    }

    // tutorial: step number and two lines in a box under the top labels
    if(hint_step < HINT_DONE) {
        Rect box(0, 18, screen.bounds.w, 29);
        screen.pen = Pen(0, 0, 0, 200);
        screen.rectangle(box);
        screen.pen = Pen(150, 150, 150);
        std::string header = "TUTORIAL " + std::to_string(hint_step + 1) + "/" + std::to_string(TUTORIAL_STEPS);
        screen.text(header, minimal_font, Point(screen.bounds.w / 2, box.y + 2), true, TextAlign::top_center);
        const char *const *lines = hint_step == HINT_PLANT && plant_count > 0
            ? hint_plant_unlinked : hint_lines[hint_step];
        screen.pen = Pen(255, 200, 0);
        for(int i = 0; i < 2; i++) {
            screen.text(lines[i], minimal_font,
                        Point(screen.bounds.w / 2, box.y + 11 + i * 9), true, TextAlign::top_center);
        }
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
        case TILE_TREES:          return Pen(10, 45, 15);
        case TILE_WATER:          return Pen(40, 90, 190);
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

void render_help() {
    screen.pen = Pen(0, 0, 40);
    screen.clear();
    screen.pen = Pen(255, 255, 255);
    int y = 4;
    for(auto line : help_text[help_page]) {
        screen.text(line, minimal_font, Point(4, y));
        y += 10;
    }
    screen.pen = Pen(150, 150, 150);
    screen.text("< > PAGE   A/B: BACK", minimal_font, Point(4, screen.bounds.h - 10));
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

// true once a city is on screen, so quitting saves it. Not before: behind
// the first title screen there may be only an empty map.
bool city_in_play = false;

void start_playing() {
    current_screen = SCREEN_CITY;
    show_stats = false;
    city_in_play = true;
}

// The SDK calls this when the player quits: Quit to home or holding HOME
// on the VGC Zero, closing the window on the desktop. Switching the
// console off is not caught, so the yearly autosave stays.
void save_on_quit() {
    if(city_in_play) {
        save_city();
    }
}

void update_title() {
    int count = has_save ? 3 : 2;
    move_menu_choice(count);

    if(buttons.pressed & Button::A) {
        // the same lines without CONTINUE when there is no save
        int item = has_save ? menu_choice : menu_choice + 1;
        if(item != 0 && has_save) {
            confirm_tutorial = item == 2;
            current_screen = SCREEN_CONFIRM;
            return;
        }
        if(item != 0) {
            new_city(item == 2);
        }
        start_playing();
    }
}

void update_confirm() {
    if(buttons.pressed & Button::A) {
        new_city(confirm_tutorial);
        start_playing();
        play(SOUND_MENU);
    } else if(buttons.pressed & (Button::B | Button::MENU)) {
        current_screen = SCREEN_TITLE;
        play(SOUND_MENU);
    }
}

// First launch: A starts a new city with the tutorial, B one without.
// Either way the answer is remembered and the screen never shows again.
void update_intro() {
    if(!(buttons.pressed & (Button::A | Button::B))) {
        return;
    }
    new_city(buttons.pressed & Button::A);
    tutorial_seen = true;
    save_game_settings();
    start_playing();
    play(SOUND_MENU);
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
    bool on_music = menu_choice == PAUSE_MUSIC;
    if(on_music && (buttons.pressed & (Button::A | Button::DPAD_LEFT | Button::DPAD_RIGHT))) {
        music_on = !music_on;
        save_game_settings();
    }

    if(buttons.pressed & Button::A) {
        if(menu_choice == PAUSE_RESUME) {
            current_screen = SCREEN_CITY;
        } else if(menu_choice == PAUSE_BUDGET) {
            current_screen = SCREEN_BUDGET;
        } else if(menu_choice == PAUSE_HELP) {
            help_page = 0;
            current_screen = SCREEN_HELP;
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
// tapped is true for a press of A (not painting): then a build that can't
// happen on trees or water says why.
void use_tool(uint32_t time, bool tapped) {
    uint8_t &tile = map[cursor.y][cursor.x];
    uint8_t before = tile;
    uint8_t before_level = level[cursor.y][cursor.x];
    int cost = tool_costs[tool];
    bool bridge = tool == TOOL_ROAD && tile == TILE_WATER;
    if(bridge) cost = BRIDGE_COST;
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
            if(bridge) {
                tile = TILE_ROAD;
                level[cursor.y][cursor.x] = 1;
            }
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
            if(tile == TILE_WATER) {
                break; // lakes stay
            }
            if(tile == TILE_ROAD && level[cursor.y][cursor.x]) {
                tile = TILE_WATER; // a bridge comes down into the water
                level[cursor.y][cursor.x] = 0;
                break;
            }
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
    if(tile == before && tapped && tool != TOOL_BULLDOZER) {
        if(before == TILE_TREES) {
            message = "BULLDOZE TREES FIRST";
            message_time = time;
        } else if(before == TILE_WATER) {
            message = "ONLY ROADS GO ON WATER";
            message_time = time;
        }
    }
    // only pay when something actually changed
    if(tile != before || level[cursor.y][cursor.x] != before_level) {
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

// Left and right turn the pages; A or B go back to the pause menu.
void update_help() {
    if(buttons.pressed & Button::DPAD_LEFT)  help_page = (help_page + HELP_PAGES - 1) % HELP_PAGES;
    if(buttons.pressed & Button::DPAD_RIGHT) help_page = (help_page + 1) % HELP_PAGES;
    if(buttons.pressed & (Button::DPAD_LEFT | Button::DPAD_RIGHT | Button::A | Button::B | Button::MENU)) {
        play(SOUND_MENU);
    }
    if(buttons.pressed & (Button::A | Button::B | Button::MENU)) {
        current_screen = SCREEN_PAUSE;
    }
}

// true when the goal of a tutorial step is met somewhere on the map
bool hint_goal_met(HintStep step) {
    switch(step) {
        case HINT_SCROLL: return scrolled;
        case HINT_ROAD:   return road_count > 0;
        default:          break;
    }
    for(int y = 0; y < MAP_H; y++) {
        for(int x = 0; x < MAP_W; x++) {
            uint8_t tile = map[y][x];
            switch(step) {
                case HINT_HOMES: if(tile == TILE_RESIDENTIAL) return true; break;
                case HINT_JOBS:  if(tile == TILE_COMMERCIAL || tile == TILE_INDUSTRIAL) return true; break;
                case HINT_LINES: if(tile == TILE_POWER_LINE) return true; break;
                case HINT_PLANT: if(is_zone(tile) && powered[y][x]) return true; break;
                default: break;
            }
        }
    }
    return false;
}

// Moves the tutorial on once the player has done each step, and says so
// when it is over.
void update_hints(uint32_t time) {
    if(!hint_goal_met(hint_step)) {
        return;
    }
    hint_step = HintStep(hint_step + 1);
    if(hint_step == HINT_DONE) {
        message = "DONE! NOW WATCH IT GROW";
        message_time = time;
        play(SOUND_RANK);
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
    bool on_title = current_screen == SCREEN_TITLE || current_screen == SCREEN_INTRO
                 || current_screen == SCREEN_CONFIRM;
    update_music(time, on_title ? MUSIC_TITLE : MUSIC_CITY);

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
        case SCREEN_HELP:
            update_help();
            last_month_time = time;
            return;
        case SCREEN_INTRO:
            update_intro();
            last_month_time = time;
            return;
        case SCREEN_CONFIRM:
            update_confirm();
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

    if(hint_step < HINT_DONE) {
        update_hints(time);
    }

    // MENU: pause menu. A B held now is let go on another screen, so
    // forget it: its release must not pick a tool later.
    if(buttons.pressed & Button::MENU) {
        b_held_in_city = false;
        current_screen = SCREEN_PAUSE;
        menu_choice = 0;
        return;
    }

    uint32_t move = dpad_moves(time);
    Point old_cursor = cursor;
    if(tool_bar_open(time) || (b_held_in_city && (buttons.state & Button::B))) {
        // B held: the D-pad belongs to the tool bar, not the cursor
        if(move & Button::DPAD_LEFT)  tool = Tool((tool + TOOL_COUNT - 1) % TOOL_COUNT);
        if(move & Button::DPAD_RIGHT) tool = Tool((tool + 1) % TOOL_COUNT);
        if(move & (Button::DPAD_LEFT | Button::DPAD_RIGHT)) {
            tool_bar_used = true;
            play(SOUND_MENU);
        }
        move = 0;
    }
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
    if(camera != start_camera) {
        scrolled = true; // the edge arrows did their job
    }

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

    // B: tap for the next tool, wrapping back to the first one; hold for
    // the tool bar. The tap counts when B comes back up, so a hold that
    // picked a tool in the bar doesn't also skip one.
    if(buttons.pressed & Button::B) {
        b_held_in_city = true;
        b_down_time = time;
        tool_bar_used = false;
    }
    if((buttons.released & Button::B) && b_held_in_city) {
        if(!tool_bar_used) {
            tool = Tool((tool + 1) % TOOL_COUNT);
        }
        b_held_in_city = false;
    }

    // A: use the tool on the tile under the cursor. Holding A while moving
    // paints with the tool, one tile per step. Not for power plants: one
    // $1500 plant per step is never what you meant.
    bool moved = cursor != old_cursor;
    if(buttons.pressed & Button::A) {
        use_tool(time, true);
    } else if((buttons.state & Button::A) && moved && tool != TOOL_POWER_PLANT) {
        use_tool(time, false);
    }
}
