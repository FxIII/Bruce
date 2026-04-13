#pragma once

typedef void (*forth_add_alias_fn)(const char *from, const char *to);

void forth_modules_init();
void forth_modules_deinit();
void forth_log_word(const char *line);
bool forth_register_load(const char *lib, const char *prefix);
void forth_set_add_alias(forth_add_alias_fn fn);
void forth_register_modules();
