#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>

#define BUF_SIZE 65536
#define MAX_HISTORY 1024

struct termios orig;
char *filename = "no-name";
int term_rows = 24, term_cols = 80;
char status_msg[80] = "";


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
  snprintf(path, sizeof(path), "%s/.config/zepto/language/%s.config", getenv("HOME"), lang);

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

int match_and_print_keyword(char *buf, int i, int buflen) {
  for (int k = 0; k < keyword_count; k++) {
    int len = strlen(keywords[k].word);
    if (i + len < buflen &&
        !strncmp(buf + i, keywords[k].word, len) &&
        isspace((unsigned char)buf[i + len])) {
      printf("%s%s\033[0m", keywords[k].color, keywords[k].word);
      return len - 1;
    }
  }
  return 0;
}

void raw_mode(int enable) {
  static struct termios raw;
  if (enable) {
    tcgetattr(0, &orig);
    raw = orig;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &raw);
  } else {
    tcsetattr(0, TCSANOW, &orig);
    printf("\033[0m\033[2J\033[H\033[?25h");
    fflush(stdout);
    system("stty sane");  
  }
}

void cleanup() {
  raw_mode(0);  // include system("stty sane")
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

void save(char *buf, int len) {
  if (!filename) return;
  FILE *f = fopen(filename, "w");
  if (f) {
    fwrite(buf, 1, len, f);
    fclose(f);
    snprintf(status_msg, sizeof(status_msg), "Saved in %s", filename);
  } else {
    snprintf(status_msg, sizeof(status_msg), "Save error");
  }
}

char *get_input(const char *label, char *buffer, int size) {
  int len = strlen(buffer);
  raw_mode(1);
  while (1) {
    printf("\033[%d;1H\033[K\033[7m%s%s\033[0m", term_rows, label, buffer);
    fflush(stdout);
    int c = getchar();
    if (c == '\n' || c == '\r') break;
    if ((c == 8 || c == 127) && len > 0) buffer[--len] = 0;
    else if (c >= 32 && c < 127 && len < size - 1) buffer[len++] = c, buffer[len] = 0;
  }
  raw_mode(0);
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
  int c = getchar();

  if (c == 3) return 3; // Ctrl+C
  if (c == 22) return 22; // Ctrl+V
  if (c == 24) return 24; // Ctrl+X
  if (c == 25) return 25; // Ctrl+y
  if (c == 26) return 26; // Ctrl+z
  
  if (c == 13) return 10; // Return

  if (c == 27) { // ESC
    
    fcntl(0, F_SETFL, O_NONBLOCK); int seq1 = getchar(); fcntl(0, F_SETFL, 0);
    if (seq1 ==-1) return 0x1B;

    if (seq1 == 'b') return 0x1F5; // Alt+h → "begin row"
    if (seq1 == 'l') return 0x1F6; // Alt+e → "end row"
    if (seq1 == 'h') return 0x1F7; // Alt+h → "begin file"
    if (seq1 == 'e') return 0x1F8; // Alt+e → "end file"
        
    if (seq1 == '[') {
      int seq2 = getchar();

      switch (seq2) {
        case 'A': return 'U';     // Up
        case 'B': return 'D';     // Down
        case 'C': return 'R';     // Right
        case 'D': return 'L';     // Left
        case 'H': return 0x1F5;    // Home
        case 'F': return 0x1F6;    // End

        case '5':
          if (getchar() == '~') return 0x1F9; // PageUp
          break;
        case '6':
          if (getchar() == '~') return 0x1FA; // PageDown
          break;
                
        case '3':
          if (getchar() == '~') return 0x7F7F; // Delete
          break;
        case '2':
          if (getchar() == '1' && getchar() == '~') return 0xF10; // F10
          break;
        case '1': {
          int next = getchar();
          if (next == '0' && getchar() == '~') return 0xF10;
          if (next == '1' && getchar() == '~') return 0xF1;
          if (next == '2' && getchar() == '~') return 0xF2;
          if (next == '8' && getchar() == '~') return 0xF7;
          if (next == ';') {
            int mod = getchar();
            int final = getchar();
            if (mod == '2') {
              switch (final) {
                case 'A': return 0xF13; // Shift+Up
                case 'B': return 0xF14; // Shift+Down
                case 'C': return 0xF12; // Shift+Right
                case 'D': return 0xF11; // Shift+Left
              }
            }            
            if (mod == '3') {
              if (final == 'H') return 0xF7; // Shift+Home
              if (final == 'F') return 0xF8; // Shift+End
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
  static int scroll = 0;
  static int hscroll = 0;

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
      if (selected) printf("\033[7m");
     
      int delta = 0;
      delta = match_and_print_keyword(buf, i, len);
      if (delta > 0) { i += delta; visual_col += delta + 1; continue; }
      

      if (buf[i] == '\n') {
        printf("\033[0m\r\n");
        y++;
        show_line++;
        new_line = 1;
        line++;
      } else {
        if (visual_col >= hscroll && visual_col - hscroll < term_cols - 6) {
          putchar(buf[i]);
        }
        visual_col++;
        if (i == len - 1) {
          printf("\033[0m\r\n");
          y++;
          show_line++;
        }
      }
      
      if (selected && buf[i] != '\n') printf("\033[0m");
    } else if (buf[i] == '\n') {
      line++;
    }
  }

  for (; y < term_rows - 1; y++, show_line++) {
    printf("\033[K\033[48;5;236;38;5;250m%4d │\033[0m\r\n", show_line + 1);
  }

  printf("\033[%d;1H\033[107m%*s\033[0m", term_rows, term_cols, "");
  printf("\033[K\033[%d;1H\033[7m", term_rows);
  printf("file:%s  %s", filename ? filename : "[senza nome]", status_msg);
  printf("\033[0m");

  // cursor
  cx = col - hscroll + 7;
  cy = l - scroll + 1;

  printf("\033[%d;%dH", cy, cx);
  printf("\033[?25h");  // show cursor
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
      case 0x1B:
        snprintf(status_msg, sizeof(status_msg), "exit without save");
        done = 1;
        break;
      case 0xF2:
        save(buf, *len);
        break;
      case 0xF7:
        get_input("search: ", search_term, sizeof(search_term));
        if (search_term[0]) {
          int found = search(buf, *len, pos + 1, search_term);
          if (found >= 0) {
            pos = found;
            sel_mode = 0;
            sprintf(status_msg, "found");
          } else {
            sprintf(status_msg, "not ");
          }
        }
        break;
      case 0xF10:
        save(buf, *len);
        done = 1;
        break;
        
      case 127: case 8: //backspace
        if (sel_mode) {
          int from = pos < sel_anchor ? pos : sel_anchor;
          int to = pos > sel_anchor ? pos : sel_anchor;
          int count = to - from;
          if (count > 0) {
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
        
      case 0x7F7F: // del
        if (sel_mode) {
          int from = pos < sel_anchor ? pos : sel_anchor;
          int to = pos > sel_anchor ? pos : sel_anchor;
          int count = to - from;
          if (count > 0) {
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
        
      case 10:
        if (*len < BUF_SIZE - 1) {
          memmove(buf + pos + 1, buf + pos, *len - pos);
          buf[pos++] = '\n';
          (*len)++;
        }
        break;
        
      case 'L': if (pos > 0) pos--; sel_mode = 0; break;
      case 'R': if (pos < *len) pos++; sel_mode = 0; break;
      case 'U': pos = move_vert(buf, *len, pos, -1); sel_mode = 0; break;
      case 'D': pos = move_vert(buf, *len, pos, +1); sel_mode = 0; break;
      case 0xF13:
        if (!sel_mode) sel_anchor = pos, sel_mode = 1;
        for (int i = 0; i < term_rows - 1 && pos > 0; i--) {
          pos = (pos > 0) ? pos - 1 : 0;
          if (buf[pos] == '\n') break;
        }
        break;
        
      case 0xF14:
        if (!sel_mode) sel_anchor = pos, sel_mode = 1;
        for (int i = 0; i < term_rows - 1 && pos < *len; i++) {
          if (buf[pos] == '\n') {
            pos++;
            break;
          }
          pos++;
        }
        break;
        
      case 0xF12:
        if (!sel_mode) sel_anchor = pos, sel_mode = 1;
        if (pos < *len) pos++;
        break;
        
      case 0xF11:
        if (!sel_mode) sel_anchor = pos, sel_mode = 1;
        if (pos > 0) pos--;
        break;
        
      case 3:
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
      
      case 26: // Ctrl+Z
        sprintf(status_msg,"undo");
        undo(buf, len, &pos);
        break;

      case 25: // Ctrl+Y
        sprintf(status_msg,"redo");
        redo(buf, len, &pos);
        break;
        
      case 24:
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
      case 22:
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
      case 0x1F5: pos = line_start(buf, *len, pos); break;
      case 0x1F6: pos = line_end(buf, *len, pos); break;
      case 0x1F7: pos = line_start(buf, *len, 0); break;
      case 0x1F8: pos = line_end(buf, *len, *len); break;
      case 0x1F9:
        lines = 0;
        for (int i = pos; i > 0 && lines < term_rows - 1; i--) {
          if (buf[i] == '\n') lines++;
          pos = i;
        }
        pos = line_start(buf, *len, pos);
        break;
      case 0x1FA:
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
    
    load_keywords(language);

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
