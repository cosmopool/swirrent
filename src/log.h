#pragma once

void logSetOutputPath(const char *path);
void logClose(void);

void logInfo(const char *fmt, ...);
void logError(const char *fmt, ...);
void logDebug(const char *fmt, ...);
