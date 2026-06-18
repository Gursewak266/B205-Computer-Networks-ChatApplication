#ifndef UI_H
#define UI_H

void ui_init(void);
void ui_print_message(const char *room, const char *sender, const char *text);
void ui_print_notification(const char *text);
void ui_print_system(const char *fmt, ...);
void ui_print_error(const char *fmt, ...);
void ui_print_rooms(const char *rooms_csv);
void ui_print_users(const char *users_csv, const char *room);
void ui_print_help(void);
void ui_show_prompt(const char *room, const char *username);
void ui_close(void);

#endif
