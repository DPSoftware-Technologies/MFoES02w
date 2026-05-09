#pragma once

#define FRAME_PIXELS (SCREEN_W * SCREEN_H)
#define FRAME_SIZE   (FRAME_PIXELS * 2)

#define MSG_FRAME        0xF0   // raw full frame (legacy)
#define MSG_TILE         0xF1   // tiled full frame (legacy)
#define MSG_FRAME_TILED  0xF2   // single-send full frame
#define MSG_TILE_UPDATE  0xF5   // DTS: single tile update
#define MSG_UPDATE_VALUE 0xD0   // Update value

#define TILES_X     8
#define TILES_Y     4
#define TILE_W      (SCREEN_W / TILES_X)   // 160
#define TILE_H      (SCREEN_H / TILES_Y)   // 180
#define TILE_SIZE   (TILE_W * TILE_H * 2)  // 57,600 bytes

#define DTS_TILES_X  16
#define DTS_TILES_Y  9
#define DTS_TILE_W   (SCREEN_W / DTS_TILES_X)   // 80px
#define DTS_TILE_H   (SCREEN_H / DTS_TILES_Y)   // 80px
#define DTS_TILE_SIZE (DTS_TILE_W * DTS_TILE_H * 2)  // 12,800 bytes

#pragma pack(push, 1)

struct FrameTiledHeader {
    uint8_t  type;      // MSG_FRAME_TILED
    uint8_t  tiles_x;
    uint8_t  tiles_y;
    uint16_t screen_w;
    uint16_t screen_h;
};  // 7 bytes

struct TileHeader {
    uint8_t  type;      // MSG_TILE
    uint8_t  tx, ty;
    uint8_t  tiles_x, tiles_y;
    uint16_t tile_w, tile_h;
    float    reserved;
};  // 13 bytes

struct TileUpdateHeader {
    uint8_t  type;   // MSG_TILE_UPDATE
    uint8_t  tx, ty;
    uint16_t tw, th;
};  // 7 bytes

#pragma pack(pop)