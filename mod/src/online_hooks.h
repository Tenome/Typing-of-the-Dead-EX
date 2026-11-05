#pragma once
int online_role_from_settings(const char *ini_path);
const char *online_log_name(int role);
void install_online_hooks(const char *ini_path, int role, const char *version);
int online_lockstep_active(void);
