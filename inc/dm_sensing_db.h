#ifndef DM_SENSING_DB_H
#define DM_SENSING_DB_H

#include "db_easy_mesh.h"
#include <initializer_list>

class dm_sensing_db_table_t : public db_easy_mesh_t {
public:
    int init(const char *table_name, std::initializer_list<db_column_t> columns);
    int sync_db(db_client_t &, void *) override { return 0; }
    int update_db(db_client_t &, dm_orch_type_t, void *) override { return 0; }
    bool search_db(db_client_t &, void *, void *) override { return false; }
    bool operator==(const db_easy_mesh_t &) override { return false; }
    int set_config(db_client_t &, const cJSON *, void *) override { return 0; }
    int get_config(cJSON *, void *, bool = false) override { return 0; }
    void init_table() override {}
    void init_columns() override {}
};

class dm_sensing_db_tables_t {
public:
    dm_sensing_db_table_t capabilities;
    dm_sensing_db_table_t agent_sta_interfaces;
    dm_sensing_db_table_t layer3_paths;
    dm_sensing_db_table_t exchanges;

    int init();
};

#endif
