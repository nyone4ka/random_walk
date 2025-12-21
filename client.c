#include "common.h"
#include "protocol.h"
#include <ncurses.h>
#include <pthread.h>

SOCKET g_socket = INVALID_SOCKET;
int g_running = 1;
ConfigMsg g_config;

void input_thread(void *arg) {
  while (g_running) {
    int ch = getch();
    if (ch == 'p') {
      Message msg;
      msg.type = MSG_CONTROL;
      msg.payload.control.cmd = CMD_PAUSE;
      send(g_socket, (char *)&msg, sizeof(msg), 0);
    } else if (ch == 'r') {
      Message msg;
      msg.type = MSG_CONTROL;
      msg.payload.control.cmd = CMD_RESUME;
      send(g_socket, (char *)&msg, sizeof(msg), 0);
    } else if (ch == 'm') {
      Message msg;
      msg.type = MSG_CONTROL;
      msg.payload.control.cmd = CMD_SWITCH_MODE;
      send(g_socket, (char *)&msg, sizeof(msg), 0);
    } else if (ch == 'q') {
      Message msg;
      msg.type = MSG_CONTROL;
      msg.payload.control.cmd = CMD_STOP;
      send(g_socket, (char *)&msg, sizeof(msg), 0);
      g_running = 0;
    }
  }
}

void draw_grid(int walker_x, int walker_y) {
  clear();
  mvprintw(0, 0, "Random Walk Sim - 'p' Pause, 'r' Resume, 'm' Mode, 'q' Quit");

  for (int y = 0; y < g_config.rows; y++) {
    for (int x = 0; x < g_config.cols; x++) {
      if (x == 0 && y == 0)
        attron(COLOR_PAIR(2));

      if (x == walker_x && y == walker_y) {
        mvprintw(y + 2, x * 3, " W ");
      } else if (x == 0 && y == 0) {
        mvprintw(y + 2, x * 3, " T ");
      } else {
        mvprintw(y + 2, x * 3, " . ");
      }

      if (x == 0 && y == 0)
        attroff(COLOR_PAIR(2));
    }
  }
  refresh();
}

int main() {
  init_sockets();

  printf("1. New Simulation\n2. Exit\nChoice: ");
  int choice;
  scanf("%d", &choice);
  if (choice != 1)
    return 0;

  g_config.rows = 10;
  g_config.cols = 10;
  g_config.replications = 10;
  g_config.max_steps_k = 50;
  g_config.prob_up = 0.25;
  g_config.prob_down = 0.25;
  g_config.prob_left = 0.25;
  g_config.prob_right = 0.25;
  strcpy(g_config.save_filename, "results.csv");
  g_config.use_obstacles = 0;
  g_config.initial_mode = MODE_INTERACTIVE;

  pid_t pid = fork();
  if (pid == 0) {

    execl("./server", "./server", NULL);
    perror("Exec failed");
    exit(1);
  }

  sleep(1);

  struct sockaddr_in serv_addr;
  if ((g_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("\n Socket creation error \n");
    return -1;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);
  inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

  if (connect(g_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    printf("\nConnection Failed \n");
    return -1;
  }

  Message msg;
  msg.type = MSG_CONFIG;
  msg.payload.config = g_config;
  send(g_socket, (char *)&msg, sizeof(msg), 0);

  initscr();
  cbreak();
  noecho();
  curs_set(0);
  start_color();
  init_pair(1, COLOR_WHITE, COLOR_BLACK);
  init_pair(2, COLOR_GREEN, COLOR_BLACK);

  nodelay(stdscr, TRUE);

  while (g_running) {
    int ch = getch();
    if (ch != ERR) {
      Message ctrl;
      ctrl.type = MSG_CONTROL;
      if (ch == 'q') {
        ctrl.payload.control.cmd = CMD_STOP;
        g_running = 0;
      }
      if (ch == 'p')
        ctrl.payload.control.cmd = CMD_PAUSE;
      if (ch == 'r')
        ctrl.payload.control.cmd = CMD_RESUME;
      if (ch == 'm')
        ctrl.payload.control.cmd = CMD_SWITCH_MODE;
      send(g_socket, (char *)&ctrl, sizeof(ctrl), 0);
    }

    Message update;
    int len = recv(g_socket, (char *)&update, sizeof(update), MSG_DONTWAIT);
    if (len > 0) {
      if (update.type == MSG_STATE_UPDATE) {
        draw_grid(update.payload.state.pos.x, update.payload.state.pos.y);
      }
      if (update.type == MSG_GAME_OVER) {
        mvprintw(g_config.rows + 3, 0, "Game Over. Press Any Key.");
        refresh();
        g_running = 0;
      }
    }
    usleep(10000);
  }

  nodelay(stdscr, FALSE);
  getch();
  endwin();

  CLOSE_SOCKET(g_socket);
  cleanup_sockets();
  return 0;
}
