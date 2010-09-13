/* hi_ncurses_main: Hexinspector
   Copyright (c) 2010 Jen Freeman

   $Id$

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

   Author contact information:

   Web: http://github.com/jenf/hexinspector

*/

/**
 * Ncurses main loop
 */

#include <hi_ncurses.h>
#include <signal.h>
#include <macros.h>
#include <stdlib.h>
#include <stdint.h>

/* 4 byte ruler currently needs 105 characters */
#define RULERCOLS_32BIT (106)
#define RULER_LINES ((COLS) > RULERCOLS_32BIT ? 5: 6)

#define PAGER_WIDTH_SOLO (COLS)
#define PAGER_WIDTH_PAIR ((COLS/2)-2)
#define PAGER_HEIGHT (LINES-RULER_LINES)


static void hi_ncurses_redraw_ruler(hi_ncurses *ncurses)
{
  unsigned char value8;
  uint16_t value16_be,value16_le;
  uint32_t value32_be,value32_le;
  unsigned int value;
  hi_file *file;
  off_t offset;
  
  offset = ncurses->focused_pager->offset;
  file = ncurses->focused_pager->file;
  
  value8 = file->memory[offset];
  
  werase(ncurses->ruler);
  mvwprintw(ncurses->ruler,0,0,"1b: %03u/%+03i/%#03o/0x%02x/'%c'",
            value8, (signed char) value8, value8, value8,
            isprint(file->memory[offset]) ? file->memory[offset] : ' ');
  if (offset+1 < file->size)
  {
    value16_be = file->memory[offset] | (file->memory[offset+1] << 8);
    value16_le = file->memory[offset+1] | (file->memory[offset] << 8);
    mvwprintw(ncurses->ruler,1,0,"2b: BE: %05u/%+06i/%#07o/0x%04x"
              " LE: %05u/%+06i/%#07o/0x%04x",
              value16_be, (int16_t)value16_be, value16_be, value16_be,
              value16_le, (int16_t)value16_le, value16_le, value16_le);
  }
  if (offset+3 < file->size)
  {
    value32_be = file->memory[offset] | (file->memory[offset+1] << 8) |
                 (file->memory[offset+2] << 16) | (file->memory[offset+3] << 24);
    value32_le = file->memory[offset+3] | (file->memory[offset+2] << 8) |
                  (file->memory[offset+1] << 16) | (file->memory[offset] << 24);    
    mvwprintw(ncurses->ruler,2,0,"4b: BE: %010u/%+011i/%#012o/0x%08x",
              value32_be, (int32_t)value32_be, value32_be, value32_be);
    mvwprintw(ncurses->ruler,RULER_LINES-3,(COLS) > RULERCOLS_32BIT ? 55 : 0,
              "4b: LE: %010u/%+011i/%#012o/0x%08x",
              value32_le, (int32_t)value32_le, value32_le, value32_le);
  }
  
  mvwprintw(ncurses->ruler,RULER_LINES-1,0,"0x%08x/0x%08x %i/%i (%.2f%%) \"%s\"",
            (unsigned int) offset, (unsigned int) file->size,
            (unsigned int) offset, (unsigned int) file->size,
            (((double) offset)/file->size)*100,
            ncurses->buffer);
  wrefresh(ncurses->ruler);
}

static void redraw(hi_ncurses *ncurses, gboolean need_resize)
{
  if (need_resize)
  {
    if (ncurses->dst != NULL)
    {
      hi_ncurses_fpager_resize(ncurses->src, PAGER_HEIGHT, PAGER_WIDTH_PAIR, 0, 0);     
      hi_ncurses_fpager_resize(ncurses->dst, PAGER_HEIGHT, PAGER_WIDTH_PAIR, 0, PAGER_WIDTH_PAIR);  
    }
    else
    {
      hi_ncurses_fpager_resize(ncurses->src, PAGER_HEIGHT, PAGER_WIDTH_SOLO, 0, 0);           
    }
    
    mvwin(ncurses->ruler, PAGER_HEIGHT, 0);
    wresize(ncurses->ruler, RULER_LINES, COLS);
    erase();
    refresh();
  }
  
  hi_ncurses_fpager_redraw(ncurses->src);
  if (ncurses->dst != NULL)
    hi_ncurses_fpager_redraw(ncurses->dst);
  
  hi_ncurses_redraw_ruler(ncurses);
  refresh();
}


static void finish(int sig)
{
  endwin();
  exit(0);
}



void hi_ncurses_main(hi_file *file, hi_file *file2, hi_diff *diff)
{
  WINDOW *window;
  WINDOW *mainwin;
  hi_ncurses *ncurses;
  int newch;
  gboolean quit = FALSE;
  gboolean need_resize = FALSE;
  gboolean key_claimed;
  long long buffer_val = 0;
  int len;
  
  ncurses = malloc(sizeof(hi_ncurses));
  ncurses->dst = NULL;
  ncurses->diff = diff;
  ncurses->buffer[0]=0;
  
  (void) signal(SIGINT, finish);
  ncurses->window = initscr();
  start_color();
  init_pair(hi_ncurses_colour_red ,COLOR_RED  ,COLOR_BLACK);
  init_pair(hi_ncurses_colour_blue,COLOR_CYAN ,COLOR_BLACK);
  init_pair(hi_ncurses_colour_green,COLOR_GREEN,COLOR_BLACK);
  init_pair(hi_ncurses_colour_yellow,COLOR_YELLOW,COLOR_BLACK);
  hi_ncurses_highlight_init();
  
  keypad(stdscr, TRUE);
  nonl();
  cbreak();
  echo();

  
  refresh();
  
  ncurses->highlighter = hi_ncurses_highlight_get(NULL,0);
  
  ncurses->ruler = newwin(RULER_LINES, 0, PAGER_HEIGHT, 0);
  
  if (file2 != NULL)
  {
    ncurses->src = hi_ncurses_fpager_new(ncurses, file,  diff, PAGER_HEIGHT, PAGER_WIDTH_PAIR,  0, 0);
    ncurses->dst = hi_ncurses_fpager_new(ncurses, file2, diff, PAGER_HEIGHT, PAGER_WIDTH_PAIR,  0, PAGER_WIDTH_PAIR);    
    ncurses->src->linked_pager = ncurses->dst;
    ncurses->dst->linked_pager = ncurses->src;
  }
  else
  {
    ncurses->src = hi_ncurses_fpager_new(ncurses, file,  diff, PAGER_HEIGHT, PAGER_WIDTH_SOLO,  0, 0); 
  }
  ncurses->focused_pager = ncurses->src;


  while (FALSE == quit)
  {
    redraw(ncurses, need_resize);    
    need_resize = FALSE;
    buffer_val = strtoll(ncurses->buffer, NULL, 0);  
    
    /* Special case imply 1 if there is no buffer */
    if (ncurses->buffer[0]==0)
    {
      buffer_val = 1;
    }
    
    newch = getch();
    key_claimed = hi_ncurses_fpager_key_event(ncurses->focused_pager, newch, buffer_val);
    
    if (FALSE == key_claimed)
    {
      if (isxdigit(newch) || newch=='x' || newch=='-')
      {
        len = strlen(ncurses->buffer);
        if (len+1 < KEYBUFFER_LEN)
        {
          ncurses->buffer[len]=newch;
          ncurses->buffer[len+1]=0;          
        }
      }
   
      switch (newch)
      {
        case 'q':
        case 'Q':
          quit = TRUE;
          break;
        
        case 'H':
          ncurses->highlighter = hi_ncurses_highlight_get(ncurses->highlighter,-1);
          break;          
        case 'h':
          ncurses->highlighter = hi_ncurses_highlight_get(ncurses->highlighter,1);
          break;
          
        case 'F':
        case 'f':
          if (ncurses->dst)
            ncurses->focused_pager = (ncurses->src==ncurses->focused_pager ? ncurses->dst : ncurses->src);
          break;
          
          /* Just temporary */

        case KEY_RESIZE:
          /* Need to resize the pagers */
          need_resize = TRUE;
          break;
      }
    }
  }
  
  finish(0);
}

