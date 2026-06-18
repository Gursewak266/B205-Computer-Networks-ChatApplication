#ifndef CLIENT_LOGGER_H
#define CLIENT_LOGGER_H

int  logger_init(const char *path);
void logger_close(void);
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);

#endif
