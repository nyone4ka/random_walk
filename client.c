#include "common.h"
#include "protocol.h"
#include <ctype.h>
#include <ncurses.h>
#include <pthread.h>

SOCKET g_socket = INVALID_SOCKET;
int g_running = 1;
ConfigMsg g_config;
int g_view_mode = 0;

CellStats g_stats_cache[MAX_GRID_SIZE][MAX_GRID_SIZE];
int g_stats_repl_done = 0;
int g_stats_repl_total = 0;

void reset_stats_cache() {
  memset(g_stats_cache, 0, sizeof(g_stats_cache));
  g_stats_repl_done = 0;
  g_stats_repl_total = 0;
}

void draw_text_centered(int y, char *text) {
  int max_y, max_x;
  getmaxyx(stdscr, max_y, max_x);
  mvprintw(y, (max_x - strlen(text)) / 2, "%s", text);
}

void get_input_string(int y, int x, char *promt, char *dest, int max_len) {
  echo();
  mvprintw(y, x, "%s: ", promt);
  getnstr(dest, max_len);
  noecho();
}

int get_input_int(int y, int x, char *promt) {
  char buf[32];
  get_input_string(y, x, promt, buf, 31);
  return atoi(buf);
}

float get_input_float(int y, int x, char *promt) {
  char buf[32];
  get_input_string(y, x, promt, buf, 31);
  return atof(buf);
}

void configure_simulation_ui() {
  clear();
  draw_text_centered(1, "--- New Simulation Configuration ---");

  g_config.rows = get_input_int(3, 2, "Rows (e.g. 10)");
  g_config.cols = get_input_int(4, 2, "Cols (e.g. 10)");
  g_config.max_steps_k = get_input_int(5, 2, "Max Steps K (e.g. 50)");
  g_config.replications = get_input_int(6, 2, "Replications (e.g. 100)");

  mvprintw(8, 2, "Probabilities (must sum to 1.0):");
  g_config.prob_up = get_input_float(9, 4, "Up    (e.g. 0.25)");
  g_config.prob_down = get_input_float(10, 4, "Down  (e.g. 0.25)");
  g_config.prob_left = get_input_float(11, 4, "Left  (e.g. 0.25)");
  g_config.prob_right = get_input_float(12, 4, "Right (e.g. 0.25)");

  g_config.use_obstacles =
      get_input_int(14, 2, "Use Obstacles? (0=No, 1=Random)");
  get_input_string(16, 2, "Save Filename (e.g. res.csv)",
                   g_config.save_filename, 64);

  g_config.initial_mode = MODE_INTERACTIVE;
}

int load_config_from_file_ui() {
  char filename[256];
  clear();
  draw_text_centered(1, "--- Load Simulation ---");
  get_input_string(3, 2, "Enter filename to load (e.g. results.csv)", filename,
                   255);

  FILE *f = fopen(filename, "r");
  if (!f) {
    mvprintw(5, 2, "Error: Could not open file!");
    getch();
    return 0;
  }

  memset(&g_config, 0, sizeof(g_config));

  char line[1024];
  int map_found = 0;
  int params_found = 0;

  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "# Params:", 9) == 0) {
      sscanf(line, "# Params: R=%d, C=%d, K=%d, Prob=%f/%f/%f/%f",
             &g_config.rows, &g_config.cols, &g_config.max_steps_k,
             &g_config.prob_up, &g_config.prob_down, &g_config.prob_left,
             &g_config.prob_right);
      params_found = 1;
    }
    if (strncmp(line, "# Map:", 6) == 0) {
      map_found = 1;
      for (int r = 0; r < g_config.rows; r++) {
        for (int c = 0; c < g_config.cols; c++) {
          int val;
          if (fscanf(f, "%d", &val) == 1) {
            g_config.obstacle_map[c][r] = val;
          }
        }
      }
    }
  }
  fclose(f);

  if (params_found) {
    mvprintw(5, 2, "Loaded Params: %dx%d, K=%d", g_config.rows, g_config.cols,
             g_config.max_steps_k);
    if (map_found) {
      mvprintw(6, 2, "Map loaded successfully.");
      g_config.use_obstacles = 2;
    } else {
      mvprintw(6, 2, "Map NOT found in file. Using Random.");
      g_config.use_obstacles = 1;
    }
  } else {
    mvprintw(5, 2, "No params found in file. Starting fresh.");
    configure_simulation_ui();
    return 1;
  }

  mvprintw(8, 2, "Enter New Replications (e.g. 100): ");
  char buf[10];
  echo();
  getnstr(buf, 9);
  noecho();
  g_config.replications = atoi(buf);

  get_input_string(10, 2, "Enter New Save Filename", g_config.save_filename,
                   64);

  g_config.initial_mode = MODE_INTERACTIVE;
  return 1;
}

void draw_grid(int walker_x, int walker_y, int repl_id, int total_repl) {
  erase();
  mvprintw(0, 0,
           "Sim: %d/%d | 'p' Pause 'r' Resume 'm' Mode 'v' View(Stat) 'q' Quit",
           repl_id, total_repl);

  for (int y = 0; y < g_config.rows; y++) {
    for (int x = 0; x < g_config.cols; x++) {
      char sym = '.';
      int color = 0;

      if (x == 0 && y == 0) {
        sym = 'T';
        color = 2;
      }

      if (walker_x != -1) {
        if (x == walker_x && y == walker_y) {
          sym = 'W';
          color = 1;
        }
      } else {
        if (g_stats_cache[x][y].is_obstacle) {
          sym = '#';
        } else {
        }
      }

      if (color == 2)
        attron(COLOR_PAIR(2));
      else if (color == 1)
        attron(COLOR_PAIR(1));

      if (walker_x == -1 && !g_stats_cache[x][y].is_obstacle &&
          (x != 0 || y != 0)) {
        float val = (g_view_mode == 0)
                        ? g_stats_cache[x][y].avg_steps_to_center
                        : g_stats_cache[x][y].prob_reach_center_k;
        if (val > 99)
          val = 99;
        mvprintw(y + 2, x * 4, "%3.0f ", val);
      } else {
        mvprintw(y + 2, x * 4, " %c  ", sym);
      }

      if (color != 0)
        attroff(COLOR_PAIR(color));
    }
  }
  refresh();
}

void *input_thread_func(void *arg) {
  while (g_running) {
    int ch = getch();
    if (ch != ERR) {
      Message msg;
      msg.type = MSG_CONTROL;
      if (ch == 'q') {
        msg.payload.control.cmd = CMD_STOP;
        g_running = 0;
        send(g_socket, (char *)&msg, sizeof(msg), 0);
      } else if (ch == 'p') {
        msg.payload.control.cmd = CMD_PAUSE;
        send(g_socket, (char *)&msg, sizeof(msg), 0);
      } else if (ch == 'r') {
        msg.payload.control.cmd = CMD_RESUME;
        send(g_socket, (char *)&msg, sizeof(msg), 0);
      } else if (ch == 'm') {
        msg.payload.control.cmd = CMD_SWITCH_MODE;
        send(g_socket, (char *)&msg, sizeof(msg), 0);
      } else if (ch == 'v') {
        g_view_mode = !g_view_mode;
      }
    }
    usleep(50000);
  }
  return NULL;
}

int main() {
  init_sockets();

  initscr();
  cbreak();
  keypad(stdscr, TRUE);

  start_color();
  init_pair(1, COLOR_YELLOW, COLOR_BLACK);
  init_pair(2, COLOR_GREEN, COLOR_BLACK);

  int choice = 0;
  while (1) {
    clear();
    draw_text_centered(2, "=== Random Walk Simulation ===");
    draw_text_centered(4, "1. New Simulation");
    draw_text_centered(5, "2. Load/Re-run Simulation");
    draw_text_centered(6, "3. Exit");
    draw_text_centered(8, "Enter choice: ");

    echo();
    char buf[10];
    getnstr(buf, 9);
    choice = atoi(buf);
    noecho();

    if (choice == 3) {
      endwin();
      return 0;
    }
    if (choice == 1) {
      configure_simulation_ui();
      break;
    }
    if (choice == 2) {
      if (load_config_from_file_ui())
        break;
    }
  }

  pid_t pid = fork();
  if (pid == 0) {
    execl("./server", "./server", NULL);
    perror("Exec failed");
    exit(1);
  }

  sleep(1);

  struct sockaddr_in serv_addr;
  if ((g_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    endwin();
    printf("\n Socket creation error \n");
    return -1;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);
  inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

  if (connect(g_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    endwin();
    printf("\nConnection Failed \n");
    return -1;
  }

  Message msg;
  msg.type = MSG_CONFIG;
  msg.payload.config = g_config;
  send(g_socket, (char *)&msg, sizeof(msg), 0);

  pthread_t thread_id;
  pthread_create(&thread_id, NULL, input_thread_func, NULL);

  curs_set(0);
  nodelay(stdscr, TRUE);

  while (g_running) {
    Message update;
    int len = recv(g_socket, (char *)&update, sizeof(update), MSG_DONTWAIT);
    if (len > 0) {
      if (update.type == MSG_STATE_UPDATE) {
        draw_grid(update.payload.state.pos.x, update.payload.state.pos.y,
                  update.payload.state.replication_id,
                  update.payload.state.total_replications);
      } else if (update.type == MSG_STATS_UPDATE) {
        StatsUpdateMsg *stats = &update.payload.stats;
        g_stats_repl_done = stats->total_replications_done;
        g_stats_repl_total = stats->total_replications_target;

        for (int i = 0; i < stats->num_cells; i++) {
          int cx = stats->cells[i].x;
          int cy = stats->cells[i].y;
          g_stats_cache[cx][cy] = stats->cells[i];
        }

        draw_grid(-1, -1, g_stats_repl_done, g_stats_repl_total);
      } else if (update.type == MSG_GAME_OVER) {
        mvprintw(g_config.rows + 4, 0, "Simulation Complete. %s",
                 update.payload.game_over_msg);
        refresh();
        g_running = 0;
      }
    }
    usleep(10000);
  }

  nodelay(stdscr, FALSE);
  mvprintw(g_config.rows + 5, 0, "Press any key to exit...");
  getch();
  endwin();

  CLOSE_SOCKET(g_socket);
  cleanup_sockets();
  return 0;
}
