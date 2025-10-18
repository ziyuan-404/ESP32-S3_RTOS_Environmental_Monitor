#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

void init_spiffs();
void load_config();
void save_config();
void reset_all_settings();
void load_historical_data();
void save_historical_data();

#endif // STORAGE_MANAGER_H
