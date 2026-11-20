#pragma once
#include <stdbool.h>
bool journal_available(void);      // the Hunter gave it (hasJournal)
void journal_open(void);           // build the list as the pane comes up
void journal_close(void);          // free its pictures
void journal_update(float dt);
void journal_draw_top(void);
void journal_draw_bottom(void);
