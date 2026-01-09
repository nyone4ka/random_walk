#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define MAX_GRID_SIZE 100
#define MAX_FILENAME 256

typedef enum {
  MSG_CONFIG,
  MSG_LOAD_CONFIG,
  MSG_STATE_UPDATE,
  MSG_STATS_UPDATE,
  MSG_CONTROL,
  MSG_GAME_OVER,
  MSG_ERROR
} MessageType;

typedef enum { MODE_INTERACTIVE, MODE_SUMMARY } SimMode;

typedef struct {
  int rows;
  int cols;
  int replications;
  int max_steps_k;
  float prob_up;
  float prob_down;
  float prob_left;
  float prob_right;
  char save_filename[MAX_FILENAME];
  int use_obstacles;
  int obstacle_map[MAX_GRID_SIZE][MAX_GRID_SIZE];
  SimMode initial_mode;
} ConfigMsg;

typedef struct {
  int x;
  int y;
} Position;

typedef struct {
  Position pos;
  int step_count;
  int replication_id;
  int walker_id;
  int total_replications;
} StateUpdateMsg;

typedef struct {
  int x;
  int y;
  float avg_steps_to_center;
  float prob_reach_center_k;
  int is_obstacle;
} CellStats;

#define STATS_CHUNK_SIZE 50
typedef struct {
  int num_cells;
  CellStats cells[STATS_CHUNK_SIZE];
  int total_replications_done;
  int total_replications_target;
  int final_update;
} StatsUpdateMsg;

typedef enum {
  CMD_PAUSE,
  CMD_RESUME,
  CMD_SWITCH_MODE,
  CMD_STOP
} ControlCommand;

typedef struct {
  ControlCommand cmd;
} ControlMsg;

typedef struct {
  MessageType type;
  union {
    ConfigMsg config;
    StateUpdateMsg state;
    StatsUpdateMsg stats;
    ControlMsg control;
    char error_msg[256];
    char game_over_msg[256];
  } payload;
} Message;

#endif
