#pragma once
#include <stdbool.h>
void ach_load(void);
void ach_reload(void);                // after a language change
void ach_award(const char *key);      // AwardAchievement / QueueAchievement
void ach_check_all(void);             // GameManager.CheckAllAchievements (collection achievements)
int ach_count(void);
int ach_unlocked(void);
bool ach_get(int i, const char **title, const char **text);   // true when unlocked
void ach_update(float dt);
void ach_draw_popup(void);
