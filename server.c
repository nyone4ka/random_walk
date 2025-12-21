#include "common.h"
#include "protocol.h"
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <time.h>


typedef struct {
  int rows;
  int cols;
  int *grid;
  long *total_steps;
  int *reached_center_count;
  int *walks_started;
} World;

typedef struct {
  ConfigMsg config;
  World world;
  int running;
  int paused;
  SimMode current_mode;
  SOCKET client_socket;
} ServerState;

ServerState g_state;

int get_idx(int x, int y) { return y * g_state.config.cols + x; }

void generate_world() {
  int size = g_state.config.rows * g_state.config.cols;
  g_state.world.grid = (int *)calloc(size, sizeof(int));
  g_state.world.total_steps = (long *)calloc(size, sizeof(long));
  g_state.world.reached_center_count = (int *)calloc(size, sizeof(int));
  g_state.world.walks_started = (int *)calloc(size, sizeof(int));

  if (g_state.config.use_obstacles) {
    srand(time(NULL));
    int obstacles_placed = 0;
    int target_obstacles = size / 5;

    while (obstacles_placed < target_obstacles) {
      int r = rand() % g_state.config.rows;
      int c = rand() % g_state.config.cols;
      if (r == 0 && c == 0)
        continue;

      if (g_state.world.grid[get_idx(c, r)] == 0) {
        g_state.world.grid[get_idx(c, r)] = 1;
        obstacles_placed++;
      }
    }
  }
}

void init_server() {
  memset(&g_state, 0, sizeof(g_state));
  g_state.running = 1;
}

void cleanup_server() {
  if (g_state.world.grid)
    free(g_state.world.grid);
  if (g_state.world.total_steps)
    free(g_state.world.total_steps);
  if (g_state.world.reached_center_count)
    free(g_state.world.reached_center_count);
  if (g_state.world.walks_started)
    free(g_state.world.walks_started);
}

int check_client_messages() {
  Message msg;
  ssize_t bytes =
      recv(g_state.client_socket, (char *)&msg, sizeof(msg), MSG_DONTWAIT);
  if (bytes > 0) {
    if (msg.type == MSG_CONTROL) {
      if (msg.payload.control.cmd == CMD_PAUSE)
        g_state.paused = 1;
      if (msg.payload.control.cmd == CMD_RESUME)
        g_state.paused = 0;
      if (msg.payload.control.cmd == CMD_SWITCH_MODE) {
        g_state.current_mode = (g_state.current_mode == MODE_INTERACTIVE)
                                   ? MODE_SUMMARY
                                   : MODE_INTERACTIVE;
      }
      if (msg.payload.control.cmd == CMD_STOP)
        return -1;
    }
  }
  return 0;
}

void run_walk(int start_x, int start_y, int repl_id) {
  int x = start_x;
  int y = start_y;
  int steps = 0;

  if (g_state.world.grid[get_idx(x, y)] == 1)
    return;

  if (g_state.current_mode == MODE_INTERACTIVE && !g_state.paused) {
    Message msg;
    msg.type = MSG_STATE_UPDATE;
    msg.payload.state.pos.x = x;
    msg.payload.state.pos.y = y;
    msg.payload.state.step_count = steps;
    msg.payload.state.replication_id = repl_id;
    send(g_state.client_socket, (char *)&msg, sizeof(msg), 0);
#ifdef _WIN32
    Sleep(100);
#else
    usleep(100000);
#endif
  }

  while (steps < g_state.config.max_steps_k) {
    while (g_state.paused) {
      if (check_client_messages() == -1)
        return;
      usleep(100000);
    }
    if (check_client_messages() == -1)
      return;

    if (x == 0 && y == 0) {
      g_state.world.total_steps[get_idx(start_x, start_y)] += steps;
      g_state.world.reached_center_count[get_idx(start_x, start_y)]++;
      break;
    }

    float r = (float)rand() / RAND_MAX;
    int next_x = x;
    int next_y = y;

    if (r < g_state.config.prob_up)
      next_y--;
    else if (r < g_state.config.prob_up + g_state.config.prob_down)
      next_y++;
    else if (r < g_state.config.prob_up + g_state.config.prob_down +
                     g_state.config.prob_left)
      next_x--;
    else
      next_x++;

    if (!g_state.config.use_obstacles) {
      if (next_x < 0)
        next_x = g_state.config.cols - 1;
      if (next_x >= g_state.config.cols)
        next_x = 0;
      if (next_y < 0)
        next_y = g_state.config.rows - 1;
      if (next_y >= g_state.config.rows)
        next_y = 0;
    } else {
      if (next_x < 0 || next_x >= g_state.config.cols || next_y < 0 ||
          next_y >= g_state.config.rows) {
        next_x = x;
        next_y = y;
      } else if (g_state.world.grid[get_idx(next_x, next_y)] == 1) {
        next_x = x;
        next_y = y;
      }
    }

    x = next_x;
    y = next_y;
    steps++;

    if (g_state.current_mode == MODE_INTERACTIVE) {
      Message msg;
      msg.type = MSG_STATE_UPDATE;
      msg.payload.state.pos.x = x;
      msg.payload.state.pos.y = y;
      msg.payload.state.step_count = steps;
      send(g_state.client_socket, (char *)&msg, sizeof(msg), 0);
      usleep(100000);
    }
  }
}

void simulation_loop() {
  for (int r = 0; r < g_state.config.replications; r++) {
    for (int y = 0; y < g_state.config.rows; y++) {
      for (int x = 0; x < g_state.config.cols; x++) {
        if (x == 0 && y == 0)
          continue;

        g_state.world.walks_started[get_idx(x, y)]++;
        run_walk(x, y, r);

        if (check_client_messages() == -1)
          return;
      }
    }

    if (g_state.current_mode == MODE_SUMMARY) {
    }
  }

  printf("Saving results to %s\n", g_state.config.save_filename);
  FILE *f = fopen(g_state.config.save_filename, "w");
  if (f) {
    fprintf(f, "X,Y,AvgSteps,ProbReachK\n");
    for (int y = 0; y < g_state.config.rows; y++) {
      for (int x = 0; x < g_state.config.cols; x++) {
        int idx = get_idx(x, y);
        int n = g_state.world.walks_started[idx];
        double avg = (n > 0) ? (double)g_state.world.total_steps[idx] / n : 0;
        double prob =
            (n > 0) ? (double)g_state.world.reached_center_count[idx] / n : 0;
        fprintf(f, "%d,%d,%.2f,%.2f\n", x, y, avg, prob);
      }
    }
    fclose(f);
  }

  Message end_msg;
  end_msg.type = MSG_GAME_OVER;
  snprintf(end_msg.payload.game_over_msg, sizeof(end_msg.payload.game_over_msg),
           "Simulation Completed.");
  send(g_state.client_socket, (char *)&end_msg, sizeof(end_msg), 0);
}

int main() {
  init_sockets();
  init_server();

  SOCKET server_fd, client_fd;
  struct sockaddr_in address;
  int opt = 1;
  int addrlen = sizeof(address);

  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
             sizeof(opt));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  if (listen(server_fd, 3) < 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  printf("Server listening...\n");
  if ((client_fd = accept(server_fd, (struct sockaddr *)&address,
                          (socklen_t *)&addrlen)) < 0) {
    perror("accept");
    exit(EXIT_FAILURE);
  }
  g_state.client_socket = client_fd;

  Message msg;
  int read_size = recv(client_fd, (char *)&msg, sizeof(msg), 0);
  if (read_size > 0 && msg.type == MSG_CONFIG) {
    g_state.config = msg.payload.config;
    g_state.current_mode = msg.payload.config.initial_mode;
    generate_world();
    simulation_loop();
  } else if (msg.type == MSG_LOAD_CONFIG) {
  }

  cleanup_server();
  CLOSE_SOCKET(client_fd);
  CLOSE_SOCKET(server_fd);
  cleanup_sockets();
  return 0;
}
