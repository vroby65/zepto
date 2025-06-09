#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <limits.h>
#include <signal.h>

#define BUF_SIZE     65536
#define MAX_HISTORY   1024


#define KEY_UP        1000
#define KEY_DOWN      1001
#define KEY_LEFT      1002
#define KEY_RIGHT     1003

#define KEY_HOME      1004
#define KEY_END       1005
#define KEY_PAGEUP    1006 
#define KEY_PAGEDOWN  1007 

#define CTRL_C        1008
#define CTRL_V        1009
#define CTRL_X        1010

#define CTRL_Z        1011
#define CTRL_Y        1012

#define CTRL_A        1013

#define KEY_ESC       1015

#define TOP           1016
#define BOTTOM        1017

#define DELETE        1018

#define SAVE          1019
#define SEARCH        1020
#define EXITSAVE      1021

#define SELECTUP      1022
#define SELECTDOWN    1023
#define SELECTLEFT    1024  
#define SELECTRIGHT   1025
#define SELECTHOME    1026
#define SELECTEND     1027
#define SELECTALL     1028


#define MOUSE_MOVE    1100
#define DOUBLE_CLICK  1101
#define TRIPLE_CLICK  1102

 
struct termios orig;
char *filename = "no-name";
int term_rows = 24, term_cols = 80;
char status_msg[80] = "";

int scroll = 0;
int hscroll = 0;

//mouse
int mouse_x = 0,mouse_y=0;
char mouse_b;

//selection
int sel_anchor = -1;
int sel_mode = 0;
char clipboard[BUF_SIZE];

// history 
struct change {
  int pos;
  int len_before;
  int len_after;
  char before[64];
  char after[64];
};

static struct change undo_stack[MAX_HISTORY];
static int undo_top = 0;
static struct change redo_stack[MAX_HISTORY];
static int redo_top = 0;

void clear_redo() {
  redo_top = 0;
} 

void record_change(int pos, const char *before, int lenb, const char *after, int lena) {
  sprintf(status_msg,"record_change: pos=%d lenb=%d lena=%d", pos, lenb, lena);
  if (undo_top >= MAX_HISTORY) undo_top = 0; 
  struct change *c = &undo_stack[undo_top++];
  c->pos = pos;
  c->len_before = lenb;
  c->len_after = lena;
  memcpy(c->before, before, lenb);
  memcpy(c->after, after, lena);
  clear_redo();
}

int undo(char *buf, int *len, int *pos) {
  sprintf(status_msg,"undo");

  if (undo_top == 0) return 0;
  struct change *c = &undo_stack[--undo_top];
  if (redo_top < MAX_HISTORY) redo_stack[redo_top++] = *c;
  memmove(buf + c->pos + c->len_before, buf + c->pos + c->len_after, *len - (c->pos + c->len_after));
  memcpy(buf + c->pos, c->before, c->len_before);
  *len = *len - c->len_after + c->len_before;
  *pos = c->pos + c->len_before;
  return 1;
}

int redo(char *buf, int *len, int *pos) {
  sprintf(status_msg,"redo");
  if (redo_top == 0) return 0;
  struct change *c = &redo_stack[--redo_top];
  if (undo_top < MAX_HISTORY) undo_stack[undo_top++] = *c;
  memmove(buf + c->pos + c->len_after, buf + c->pos + c->len_before, *len - (c->pos + c->len_before));
  memcpy(buf + c->pos, c->after, c->len_after);
  *len = *len - c->len_before + c->len_after;
  *pos = c->pos + c->len_after;
  return 1;
}

// highlight sintax
char *language = "text";

struct Keyword {
  char word[32];
  char color[32];
};

#define MAX_KEYWORDS 256
struct Keyword keywords[MAX_KEYWORDS];
int keyword_count = 0;


#include <ctype.h>

void load_keywords(const char *lang) {
  keyword_count = 0;

  char path[128];
  snprintf(path, sizeof(path), "%s/.config/zt/languages/%s.config", getenv("HOME"), lang);

  FILE *fp = fopen(path, "r");
  if (!fp) return;

  char line[128];
  while (fgets(line, sizeof(line), fp)) {
    if (keyword_count >= MAX_KEYWORDS) break;

    char *kw = strtok(line, " \t\r\n");
    char *col = strtok(NULL, " \t\r\n");

    if (kw && col) {
      strncpy(keywords[keyword_count].word, kw, 31);
      snprintf(keywords[keyword_count].color, 31, "\033[%sm", col);
      keyword_count++;
    }
  }

  fclose(fp);
}

const char *get_extension(const char *name) {
  const char *dot = strrchr(name, '.');
  return (!dot || dot == name) ? "" : dot + 1;
}

int match_keyword(char *buf, int i, int buflen, const char **color, const char **word) {
  for (int k = 0; k < keyword_count; k++) {
    int len = strlen(keywords[k].word);
    if (i + len <= buflen &&
        !strncmp(buf + i, keywords[k].word, len)) {

      char prev = (i > 0) ? buf[i - 1] : ' ';
      char next = (i + len < buflen) ? buf[i + len] : '\0';

      if (!isalnum((unsigned char)prev) && prev != '_' &&
          (isspace((unsigned char)next) || next == '\0' ||
           strchr("();{}[]<>+-*/%=!&|^,.", next))) {

        *color = keywords[k].color;
        *word = keywords[k].word;
        return len;
      }
    }
  }
  return 0;
}

void raw_mode(int enable) {
  static struct termios raw;
  if (enable) {
    tcgetattr(0, &orig);
    raw = orig;

    raw.c_lflag &= ~(ECHO | ICANON | ISIG);         
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP); 
    raw.c_oflag &= ~(OPOST);                        
    raw.c_cflag |= (CS8);                           

    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    tcsetattr(0, TCSANOW, &raw);

    printf("\033[?1000h"); 
    printf("\033[?1002h"); 
    printf("\033[?1006h"); 

  } else {
    tcsetattr(0, TCSANOW, &orig);
    printf("\033[0m\033[2J\033[H\033[?25h");
    printf("\033[?1000l"); 
    printf("\033[?1002l");
    printf("\033[?1006l");

    fflush(stdout);
    int tmp=system("stty sane"); 
  }
}

void cleanup() {
  raw_mode(0);  
  printf("\033[0 q");
  fflush(stdout);
}

void get_terminal_size() {
  struct winsize ws;
  if (ioctl(1, TIOCGWINSZ, &ws) == 0) {
    term_cols = ws.ws_col;
    term_rows = ws.ws_row;
  }
}

char *get_input(const char *label, char *buffer, int size) {
  int len = strlen(buffer);

  printf("\033[%d;1H\033[30;107m", term_rows);
  printf("\033[%d;1H\033[K%s%s\033[0m", term_rows, label, buffer);
  fflush(stdout);

  while (1) {
    int c = getchar();
    if (c == '\n' || c == '\r') break;

    if ((c == 8 || c == 127) && len > 0) {
      buffer[--len] = 0;
    } else if (c >= 32 && c < 127 && len < size - 1) {
      buffer[len++] = c;
      buffer[len] = 0;
    }

    printf("\033[%d;1H\033[30;107m", term_rows);
    printf("\033[%d;1H\033[K%s%s\033[0m", term_rows, label, buffer);
    fflush(stdout);
  }

  return buffer;
}

int search(char *buf, int len, int start, const char *needle) {
  for (int i = start; buf[i] && i < len; i++) {
    int j = 0;
    while (needle[j] && buf[i + j] == needle[j]) j++;
    if (!needle[j]) return i;
  }
  return -1;
}

int read_key() {
  static int last_click_time = 0;
  static int click_count = 0;

  int c = getchar();

  if (c== 1) return SELECTALL; //CTRL+A
  if (c == 13) return 10; // Return

  if (c == 3) return CTRL_C; // Ctrl+C
  if (c == 22) return CTRL_V; // Ctrl+V
  if (c == 24) return CTRL_X; // Ctrl+X
  
  if (c == 25) return CTRL_Y; // Ctrl+Y
  if (c == 26) return CTRL_Z; // Ctrl+Z
  
  if (c == 31) return SEARCH;   // Ctrl+7 - find
  if (c == 0) return SAVE; // Ctrl+2 - save
  

  if (c == 27) { // ESC    
    fcntl(0, F_SETFL, O_NONBLOCK); int seq1 = getchar(); fcntl(0, F_SETFL, 0);
    if (seq1 ==-1) return KEY_ESC;
    if (seq1=='O' && getchar()=='Q')return SAVE; //F2


    if (seq1 == 'h') return TOP; // Alt+h → "begin file"
    if (seq1 == 'e') return BOTTOM; // Alt+e → "end file"
    
    
    
        
    if (seq1 == '[') {
      int seq2 = getchar();
      
      
      if ( seq2 == '<') {
        int btn = 0;
        char c;
        int tmp = scanf("%d;%d;%d%c", &btn, &mouse_x, &mouse_y, &c);
        
        if (c == 'm') {
          mouse_b = 0;
          click_count++;
          return 0;
        }
        else{
          if (btn == 0 || (btn & 32)) {
            mouse_b=1;
            struct timeval tv;
            gettimeofday(&tv, NULL);
            int now = tv.tv_sec * 1000 + tv.tv_usec / 1000;
            
            if (now - last_click_time > 400){
              last_click_time = 0;
              click_count=0;
              sel_mode=0;
              sel_anchor=-1;
            }

            if (click_count<2) {
              click_count=1;
              last_click_time = now;
              return MOUSE_MOVE;
            }            
            
            if (click_count == 2 && now - last_click_time < 400) {
              sel_mode=0;
              last_click_time = now;
              return DOUBLE_CLICK;
            }

            if (click_count == 3 && now - last_click_time < 400) {
              sel_mode=0;
              click_count = 0;
              last_click_time = 0;
              return TRIPLE_CLICK;
            }
          }
        }
        if (btn == 64) return KEY_UP; // wheel Up
        if (btn == 65) return KEY_DOWN; // Wheel Down
      }
      
      switch (seq2) {
        case 'A': return KEY_UP;     // Up
        case 'B': return KEY_DOWN;   // Down
        case 'C': return KEY_RIGHT;  // Right
        case 'D': return KEY_LEFT;   // Left

        case 'H': return KEY_HOME;   // Home
        case 'F': return KEY_END;    // End

        case '5':
          if (getchar() == '~') return KEY_PAGEUP; // PageUp
          break;
        case '6':
          if (getchar() == '~') return KEY_PAGEDOWN; // PageDown
          break;
                
        case '3':
          if (getchar() == '~') return DELETE; // Delete
          break;
        case '2':
          if (getchar() == '1' && getchar() == '~') return EXITSAVE; // F10
          break;
        case '1': {
          int next = getchar();
          if (next == '8' && getchar() == '~') return SEARCH;//F7
          if (next == ';') {
            int mod = getchar();
            int final = getchar();
            if (mod == '2') {
              switch (final) {
                case 'A': return SELECTUP; // Shift+Up
                case 'B': return SELECTDOWN; // Shift+Down
                case 'C': return SELECTRIGHT; // Shift+Right
                case 'D': return SELECTLEFT; // Shift+Left
                
                case 'H': return SELECTHOME;  // Shift+Home
                case 'F': return SELECTEND;   // Shift+End
              }
            }            
            if (mod == '3') {
              if (final == 'H') return SELECTHOME; // ALT+Home
              if (final == 'F') return SELECTEND; // ALT+End
            }
          }
          break;
        }
      }
    } else if (seq1 == 'O') {
      int seq2 = getchar();
      switch (seq2) {
        case 'P': return 0xF1; // F1
        case 'Q': return 0xF2; // F2
        case 'R': return 0xF3; // F3
        case 'S': return 0xF4; // F4
      }
    }
    return 0;// unknow
  }

  return c; 
}

int line_start(char *buf, int len, int pos) {
  while (pos > 0 && buf[pos - 1] != '\n') pos--;
  return pos;
}

int line_end(char *buf, int len, int pos) {
  while (pos < len && buf[pos] != '\n') pos++;
  return pos;
}

int move_vert(char *buf, int len, int pos, int dir) {
  int start = line_start(buf, len, pos);
  int col = pos - start;
  int new_start;

  if (dir < 0 && start > 0) new_start = line_start(buf, len, start - 1);
  else if (dir > 0 && line_end(buf, len, pos) < len) new_start = line_end(buf, len, pos) + 1;
  else return pos;

  int new_end = line_end(buf, len, new_start);
  int new_len = new_end - new_start;
  if (col > new_len) col = new_len;
  return new_start + col;
}

void draw(char *buf, int len, int pos) {
  printf("\033[?25l");  // hide cursor
  get_terminal_size();

  int sel_from = -1, sel_to = -1;
  if (sel_mode) {
    sel_from = pos < sel_anchor ? pos : sel_anchor;
    sel_to   = pos > sel_anchor ? pos : sel_anchor;
  }

  printf("\033[H"); // home
  int line = 0, y = 0;
  int show_line = scroll;
  int new_line = 1;

  int cx = 1, cy = 1;
  int l = 0, col = 0;
  for (int i = 0; i < pos && i < len; i++) {
    if (buf[i] == '\n') {
      l++;
      col = 0;
    } else {
      col++;
    }
  }

  if (l < scroll) scroll = l;
  else if (l >= scroll + term_rows - 1) scroll = l - term_rows + 2;

  if (col < hscroll) hscroll = col;
  else if (col >= hscroll + term_cols - 6) hscroll = col - (term_cols - 6) + 1;

  for (int i = 0; i < len && y < term_rows - 1; i++) {
    if (line >= scroll) {
      static int visual_col = 0;
      if (new_line) {
        visual_col = 0;
        printf("\033[K\033[48;5;236;38;5;250m%4d │\033[0m", show_line + 1);
        new_line = 0;
      }

      int selected = (sel_mode && i >= sel_from && i < sel_to);
      const char *kw_color, *kw_word;
      int delta = match_keyword(buf, i, len, &kw_color, &kw_word);

      if (delta > 0) {
          for (int j = 0; j < delta && i + j < len; j++) {
              int selected = (sel_mode && i + j >= sel_from && i + j < sel_to);
              if (visual_col >= hscroll && visual_col - hscroll < term_cols - 6) {
                  if (selected)
                      printf("\033[7m"); // inverti solo
                  else
                      printf("%s", kw_color); // colora solo se non selezionato

                  putchar(buf[i + j]);
                  printf("\033[0m");
              }
              visual_col++;
          }
          i += delta - 1;
          continue;
      }

      if (buf[i] == '\n') {
        printf("\033[0m\r\n");
        y++;
        show_line++;
        new_line = 1;
        line++;
      } else {
        if (visual_col >= hscroll && visual_col - hscroll < term_cols - 6) {
          if (selected) printf("\033[7m");
          putchar(buf[i]);
          if (selected) printf("\033[0m");
        }
        visual_col++;
        if (i == len - 1) {
          printf("\033[0m\r\n");
          y++;
          show_line++;
        }
      }
    } else if (buf[i] == '\n') {
      line++;
    }
  }

  for (; y < term_rows - 1; y++, show_line++) {
    printf("\033[K\033[48;5;236;38;5;250m%4d │\033[0m\r\n", show_line + 1);
  }

  // status bar
  char status_line[term_cols + 1];
  snprintf(status_line, term_cols + 1, "file:%s  %s", filename ? filename : "[senza nome]", status_msg);
  printf("\033[%d;1H\033[7m", term_rows);
  printf("%-*.*s", term_cols, term_cols, status_line);
  printf("\033[0m");

  // cursor
  cx = col - hscroll + 7;
  cy = l - scroll + 1;
  printf("\033[%d;%dH", cy, cx);
  printf("\033[?25h");  // show cursor
}

void save(char *buf, int len) {
  if (!filename) return;

  FILE *f = fopen(filename, "w");
  if (f) {
    fwrite(buf, 1, len, f);
    fclose(f);
    snprintf(status_msg, sizeof(status_msg), "Saved in %s", filename);
    return;
  }

  if (errno == EACCES || errno == EPERM) {
    char tmpname[] = "/tmp/ztXXXXXX";
    int fd = mkstemp(tmpname);
    if (fd == -1) {
      snprintf(status_msg, sizeof(status_msg), "Temp file error");
      return;
    }

    if (write(fd, buf, len) != len) {
      close(fd);
      unlink(tmpname);
      snprintf(status_msg, sizeof(status_msg), "Write temp file failed");
      return;
    }
    close(fd);

    char password[128] = "";
    get_input("password: ", password, sizeof(password));

    snprintf(status_msg, sizeof(status_msg), "🔐 Writing with sudo...");
    draw(buf, len, 0);
    fflush(stdout);

    char cmd[PATH_MAX + 256];
    snprintf(cmd, sizeof(cmd),
      "echo '%s' | sudo -S dd if='%s' of='%s' conv=notrunc status=none",
      password, tmpname, filename);

    int r = system(cmd);

    if (r == 0) {
      snprintf(status_msg, sizeof(status_msg), "Saved with sudo: %s", filename);
    } else {
      snprintf(status_msg, sizeof(status_msg), "Save failed (sudo)");
    }

    unlink(tmpname);
  } else {
    snprintf(status_msg, sizeof(status_msg), "Save error");
  }
}

void editor(char *buf, int *len) {
  int pos = 0;
  int lines = 0;
  int done = 0;
  static char search_term[64] = "";

  while (!done) {
    draw(buf, *len, pos);
    fflush(stdout);

    int ch = read_key();
    snprintf(status_msg, sizeof(status_msg), "  ESC exit | F2 save | F7 search | F10 save & exit");

    switch (ch) {
      case KEY_ESC: //exit without save
        done = 1;
        break;
      case SAVE: // save
        save(buf, *len);
        break;
      case SEARCH: // search
        get_input("search: ", search_term, sizeof(search_term));
        if (search_term[0]) {
          int found = search(buf, *len, pos + 1, search_term);
          if (found >= 0) {
            pos = found;
            sel_mode = 0;
            sprintf(status_msg, "found");
          } else {
            sprintf(status_msg, "not found");
          }
        }
        break;
      case EXITSAVE:
        save(buf, *len);
        done = 1;
        break;
        
      case 127: case 8: //backspace
        if (sel_mode) {
          int from = pos < sel_anchor ? pos : sel_anchor;
          int to = pos > sel_anchor ? pos : sel_anchor;
          int count = to - from;
          if (count > 0) {
            record_change(from, buf + from, count, NULL, 0);
            memmove(buf + from, buf + to, *len - to);
            *len -= count;
            buf[*len] = 0;
            pos = from;
            sel_mode = 0;
            sel_anchor = -1;
            break;
          }
        }
        if (pos > 0) {
          char removed = buf[pos - 1];
          record_change(pos - 1, &removed, 1, NULL, 0);
          pos--;
          (*len)--;
          memmove(buf + pos, buf + pos + 1, *len - pos);
          buf[*len] = 0;
        }
        break;
        
      case DELETE: // del
        if (sel_mode) {
          int from = pos < sel_anchor ? pos : sel_anchor;
          int to = pos > sel_anchor ? pos : sel_anchor;
          int count = to - from;
          if (count > 0) {
            record_change(from, buf + from, count, NULL, 0);
            memmove(buf + from, buf + to, *len - to);
            *len -= count;
            buf[*len] = 0;
            pos = from;
            sel_mode = 0;
            sel_anchor = -1;
            break;
          }
        }
        if (pos < *len) {
          char removed = buf[pos];
          record_change(pos, &removed, 1, NULL, 0);
          (*len)--;
          memmove(buf + pos, buf + pos + 1, *len - pos);
          buf[*len] = 0;
        }
        break;
        
      case 10: //RETURN
        if (*len < BUF_SIZE - 1) {
          memmove(buf + pos + 1, buf + pos, *len - pos);
          buf[pos++] = '\n';
          (*len)++;
        }
        break;
        
      case KEY_LEFT: if (pos > 0) pos--; sel_mode = 0; break;
      case KEY_RIGHT: if (pos < *len) pos++; sel_mode = 0; break;
      case KEY_UP: pos = move_vert(buf, *len, pos, -1); sel_mode = 0; draw(buf, *len, pos); break;
      case KEY_DOWN: pos = move_vert(buf, *len, pos, +1); sel_mode = 0; draw(buf, *len, pos); break;
      
      case MOUSE_MOVE:
        pos = 0;
        for (int i = 0; i < mouse_y + scroll - 1; i++) {
          pos = move_vert(buf, *len, pos, +1);
        }
        for (int i = 0; i < mouse_x - 7; i++) {
          if (pos < *len && buf[pos] != '\n') pos++;
          else break;
        }

        if (!sel_mode ) {
          sel_anchor = pos;
          sel_mode = 1;
        }
        
        //if (mouse_b==0)sel_mode=0;

        draw(buf, *len, pos);
        break;

      case DOUBLE_CLICK: {
        pos = 0;
        for (int i = 0; i < mouse_y + scroll - 1; i++) {
          pos = move_vert(buf, *len, pos, +1);
        }
        for (int i = 0; i < mouse_x - 7; i++) {
          if (pos < *len && buf[pos] != '\n') pos++;
          else break;
        }
        int start = pos;
        while (start > 0 && (isalnum(buf[start - 1]) || buf[start - 1] == '_')) {
          start--;
        }
        int end = pos;
        while (end < *len && (isalnum(buf[end]) || buf[end] == '_')) {
          end++;
        }
        sel_anchor = start;
        pos = end;
        sel_mode = 1;
        draw(buf, *len, pos);
        break;
      }
      
      case TRIPLE_CLICK: {
        pos = 0;
        for (int i = 0; i < mouse_y + scroll - 1; i++) {
          pos = move_vert(buf, *len, pos, +1);
        }
        for (int i = 0; i < mouse_x - 7; i++) {
          if (pos < *len && buf[pos] != '\n') pos++;
          else break;
        }

        int start = line_start(buf, *len, pos);
        int end = line_end(buf, *len, pos);
        sel_anchor = start;
        pos = end;
        sel_mode = 1;
        draw(buf, *len, pos);
        break;
      }
      
                    
      case SELECTUP: //selectup
        if (!sel_mode) sel_anchor = pos, sel_mode = 1;
        for (int i = 0; i < term_rows - 1 && pos > 0; i--) {
          pos = (pos > 0) ? pos - 1 : 0;
          if (buf[pos] == '\n') break;
        }
        break;
        
      case SELECTDOWN: //selectdown
        if (!sel_mode) sel_anchor = pos, sel_mode = 1;
        for (int i = 0; i < term_rows - 1 && pos < *len; i++) {
          if (buf[pos] == '\n') {
            pos++;
            break;
          }
          pos++;
        }
        break;
        
      case SELECTRIGHT: //selectright
        if (!sel_mode) sel_anchor = pos, sel_mode = 1;
        if (pos < *len) pos++;
        break;
        
      case SELECTLEFT: //selectleft
        if (!sel_mode) sel_anchor = pos, sel_mode = 1;
        if (pos > 0) pos--;
        break;
        
      case SELECTHOME: 
        while (pos > line_start(buf, *len, pos)){ 
          if (!sel_mode) sel_anchor = pos, sel_mode = 1;
          if (pos > 0) pos--;
        }
        break;

      case SELECTEND: 
        while(pos < line_end(buf, *len, pos)){ 
          if (!sel_mode) sel_anchor = pos, sel_mode = 1;
          if (pos < *len) pos++;
        }
        break;

      case SELECTALL:
        sel_anchor = 0;
        pos = *len;
        sel_mode = 1;
        draw(buf, *len, pos);
        break;
              
      case CTRL_C:
        if (sel_mode) {
          int start = (sel_anchor < pos) ? sel_anchor : pos;
          int end = (sel_anchor > pos) ? sel_anchor : pos;
          int len_copy = end - start;
          if (len_copy < BUF_SIZE - 1) {
            memcpy(clipboard, buf + start, len_copy);
            clipboard[len_copy] = '\0';
          }
        }
        break;
        
      case 9: // TAB
        if (*len + 2 < BUF_SIZE) {
          char text[2] = { ' ', ' ' };
          record_change(pos, NULL, 0, text, 2);
          memmove(buf + pos + 2, buf + pos, *len - pos);
          buf[pos++] = ' ';
          buf[pos++] = ' ';
          *len += 2;
        }
        break;
      
      case CTRL_Z: // Ctrl+Z
        sprintf(status_msg,"undo");
        undo(buf, len, &pos);
        break;

      case CTRL_Y: // Ctrl+Y
        sprintf(status_msg,"redo");
        redo(buf, len, &pos);
        break;
        
      case CTRL_X:
        if (sel_mode) {
          int start = sel_anchor < pos ? sel_anchor : pos;
          int end = sel_anchor > pos ? sel_anchor : pos;
          int len_sel = end - start;
          record_change(start, buf + start, len_sel, NULL, 0);
          if (len_sel > 0 && len_sel < BUF_SIZE - 1) {
            memcpy(clipboard, buf + start, len_sel);
            clipboard[len_sel] = '\0';
            memmove(buf + start, buf + end, *len - end);
            *len -= len_sel;
            pos = start;
            sel_mode = 0;
            sel_anchor = -1;
          }
        }
        break;
      case CTRL_V:
        {
          int len_clip = strlen(clipboard);
          record_change(pos, NULL, 0, clipboard, len_clip);
          if (*len + len_clip < BUF_SIZE) {
            memmove(buf + pos + len_clip, buf + pos, *len - pos);
            memcpy(buf + pos, clipboard, len_clip);
            pos += len_clip;
            *len += len_clip;
          }
        }
        break;
      case KEY_HOME: pos = line_start(buf, *len, pos); break;
      case KEY_END: pos = line_end(buf, *len, pos); break;
      
      case TOP: pos = line_start(buf, *len, 0); break;
      case BOTTOM: pos = line_end(buf, *len, *len); break;
      
      case KEY_PAGEUP:
        lines = 0;
        for (int i = pos; i > 0 && lines < term_rows - 1; i--) {
          if (buf[i] == '\n') lines++;
          pos = i;
        }
        pos = line_start(buf, *len, pos);
        break;
      case KEY_PAGEDOWN:
        lines = 0;
        for (int i = pos; i < *len && lines < term_rows - 1; i++) {
          pos = i;
          if (buf[i] == '\n') lines++;
        }
        pos = line_start(buf, *len, pos);
        break;
      case 0: break;
      default:
        if (ch >= 32 && ch < 127 && *len < BUF_SIZE - 1) {
          char after[1] = { ch };
          record_change(pos, buf + pos, 0, after, 1);
          memmove(buf + pos + 1, buf + pos, *len - pos);
          buf[pos++] = ch;
          (*len)++;
        }
    }
  }
}

int main(int argc, char *argv[]) {
  char buf[BUF_SIZE];
  int len = 0;

  struct termios orig, raw;
  tcgetattr(0, &orig);
  raw = orig;
  cfmakeraw(&raw);
  tcsetattr(0, TCSANOW, &raw);
  signal(SIGINT, SIG_IGN); 
  
  atexit(cleanup);
  
  printf("\033[5 q"); 

  if (argc > 1) {
    filename = argv[1];
    
    const char *ext = get_extension(filename);
    if (strcmp(ext, "c") == 0 || strcmp(ext, "h") == 0) language = "c";
    if (strcmp(ext, "py") == 0) language = "python";
    
    load_keywords(ext);

    FILE *f = fopen(filename, "r");
    if (f) {
      len = fread(buf, 1, BUF_SIZE, f);
      fclose(f);
      snprintf(status_msg, sizeof(status_msg), "File %s loaded (%d byte)(%s)", filename, len,language);
    } else {
      snprintf(status_msg, sizeof(status_msg), "New file: %s", filename);
    }
  } else {
    snprintf(status_msg, sizeof(status_msg), "new file (no name)");
  }

  printf("\033[2J\033[H");       
  raw_mode(1);       
  get_terminal_size();          
  editor(buf, &len);           
  raw_mode(0);                  
  printf("\033[0m\033[2J\033[H"); 
  printf("\033[0 q"); 

  return 0;
}
