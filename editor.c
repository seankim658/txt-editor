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

/*** data ***/

// struct to store the editor state
struct editorConfig {
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

char editorReadKey() {
  /* Reads a single keypress from the user and returns it.
   *
   * Returns:
   *  the keypress
   */
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
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

void editorProcessKeyPress() {
  /* Processes a keypress from the user.
   */

  char c = editorReadKey();
  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
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
    abAppend(ab, "~", 1);

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
  abAppend(&ab, "\x1b[2J", 4);
  abAppend(&ab, "\x1b[H", 3);
  editorDrawRows(&ab);
  abAppend(&ab, "\x1b[H", 3);
  
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/*** init ***/

void initEditor() {
  /* Initializes the editor by getting the size of the terminal window and
   * storing it in the editorConfig struct.
   */
  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
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
