/*** includes ***/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)

#define TXT_VERSION "0.0.1"

enum editorKey {
  MOVE_LEFT = 1000,
  MOVE_RIGHT = 1001,
  MOVE_UP = 1002,
  MOVE_DOWN = 1003,
  PAGE_UP = 1004,
  PAGE_DOWN = 1005
};

/*** data ***/

// struct to store the editor state
struct editorConfig {
  int cx;
  int cy;
  int screenrows;
  int screencols;
  struct termios orig_termios;
};

// struct for a dynamic string to reduce write() calls
struct abuf {
  char *b;
  int len;
};
// simple abuf clean slate constant initializer
#define ABUF_INIT                                                              \
  { NULL, 0 }

struct editorConfig E;

/*** append buffer ***/

void abAppend(struct abuf *ab, const char *s, int len) {
  /* Adds a new string to the append buffer.
   *
   * ab: pointer to the append buffer to add to
   * s: pointer to the new string to add to the append buffer
   * len: length of the new string to append
   */
  char *new = realloc(ab->b, ab->len + len);
  if (new == NULL)
    return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  /* Frees the append buffer memory.
   *
   * ab: pointer to the append buffer to free from memory
   */
  free(ab->b);
}

/*** terminal ***/

void die(const char *s) {
  /* Prints an error message and exits the program.
   *
   * s: the error message to print
   */
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  perror(s);
  exit(1);
}

void disableRawMode() {
  /* Resets the terminal to its original state.
   */
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  /* Enables terminal raw mode and disables the ECHO, ICANON, ISIG, and IEXTEN
   * flags in the termios struct. Also disables the IXON and ICRNL flags in the
   * c_iflag member of the termios struct and sets the atexit function to
   * disableRawMode.
   */

  // save the original terminal attributes
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");

  // set the atexit function to disableRawMode to restore the terminal to its
  // original state when the program exits
  atexit(disableRawMode);

  // create a copy of the original termios struct
  struct termios raw = E.orig_termios;

  // disable the IXON (software flow control), ICRNL (carriage return to
  // newline), BRKINT (break condition), INPCK (parity check), and ISTRIP (strip
  // 8th bit) flags
  raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
  // bitwise or the cs8 flag to set the character size to 8 bits
  raw.c_cflag |= (CS8);
  // disable the OPOST flag to disable output processing
  raw.c_oflag &= ~(OPOST);
  // disable the ECHO, ICANON (canonical mode), ISIG (signals), and IEXTEN
  // (extended input processing) flags
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

  // set the VMIN value to 0 to set the minimum number of bytes of input needed
  // before read() can return
  raw.c_cc[VMIN] = 0;
  // set the VTIME value to 1/10th of a second to set the maximum amount of time
  // to wait before read() returns
  raw.c_cc[VTIME] = 1;

  // set the terminal attributes to the modified termios struct
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

int editorReadKey() {
  /* Reads a single keypress from the user and returns it.
   *
   * Returns:
   *  the keypress
   */
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) {
      die("read");
    }
  }

  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1) {
      return '\x1b';
    }
    if (read(STDIN_FILENO, &seq[1], 1) != 1) {
      return '\x1b';
    }
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) {
          return '\x1b';
        }
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          }
        }
      } else {
        switch (seq[1]) {
        case 'A':
          return MOVE_UP;
        case 'B':
          return MOVE_DOWN;
        case 'C':
          return MOVE_RIGHT;
        case 'D':
          return MOVE_LEFT;
        }
      }
    }
    return '\x1b';
  } else {
    return c;
  }

  return c;
}

int getWindowSize(int *rows, int *cols) {
  /* Gets the size of the terminal window and stores it in the rows and cols
   * pointers.
   *
   * rows: a pointer to the variable to store the number of rows
   * cols: a pointer to the variable to store the number of columns
   *
   * Returns:
   *  0 if successful, -1 if not
   */
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1 || w.ws_col == 0) {
    return -1;
  } else {
    *cols = w.ws_col;
    *rows = w.ws_row;
    return 0;
  }
}

/*** input ***/

void editorMoveCursor(int key) {
  /* Moves the cursor.
   *
   * key: motion keys (using vim motion keys)
   */
  switch (key) {
  case MOVE_LEFT:
    if (E.cx != 0) {
      E.cx--;
    }
    break;
  case MOVE_RIGHT:
    if (E.cx != E.screencols - 1) {
      E.cx++;
    }
    break;
  case MOVE_UP:
    if (E.cy != 0) {
      E.cy--;
    }
    break;
  case MOVE_DOWN:
    if (E.cy != E.screencols - 1) {
      E.cy++;
    }
    break;
  }
}

void editorProcessKeyPress() {
  /* Processes a keypress from the user.
   */

  int c = editorReadKey();
  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;
  case PAGE_UP:
  case PAGE_DOWN: {
    int times = E.screenrows;
    while (times--) {
      editorMoveCursor(c == PAGE_UP ? MOVE_UP : MOVE_DOWN);
    }
  } break;
  case MOVE_UP:
  case MOVE_DOWN:
  case MOVE_LEFT:
  case MOVE_RIGHT:
    editorMoveCursor(c);
    break;
  }
}

/*** output ***/

void editorDrawRows(struct abuf *ab) {
  /* Draws the rows of the editor.
   *
   * ab: the append buffer
   */
  int y;
  for (y = 0; y < E.screenrows; y++) {
    if (y == E.screenrows / 3) {
      char welcome[80];
      int welcomelen = snprintf(welcome, sizeof(welcome),
                                "txt editor --- version %s", TXT_VERSION);
      if (welcomelen > E.screencols) {
        welcomelen = E.screencols;
      }

      int padding = (E.screencols - welcomelen) / 2;
      if (padding) {
        abAppend(ab, "~", 1);
        padding--;
      }
      while (padding--) {
        abAppend(ab, " ", 1);
      }

      abAppend(ab, welcome, welcomelen);
    } else {
      abAppend(ab, "~", 1);
    }

    abAppend(ab, "\x1b[K", 3);
    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen() {
  /* Resets the screen by clearing and moving the cursor to the top, then draws
   * the tilde rows and moves the cursor back to the top.
   */
  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/*** init ***/

void initEditor() {
  /* Initializes the editor by getting the size of the terminal window and
   * storing it in the editorConfig struct.
   */
  E.cx = 0;
  E.cy = 0;
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
    die("getWindowSize");
  }
}

int main() {
  enableRawMode();
  initEditor();

  // continuously read from stdin
  while (1) {
    editorRefreshScreen();
    editorProcessKeyPress();
  }

  return 0;
}
