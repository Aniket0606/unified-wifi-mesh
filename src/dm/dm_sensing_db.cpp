#include "dm_sensing_db.h"

#include <cstring>
#include <cstdio>

int dm_sensing_db_table_t::init(const char *table_name, std::initializer_list<db_column_t> columns)
{
    snprintf(m_table_name, sizeof(m_table_name), "%s", table_name);
    m_num_cols = 0U;
    for (const auto &column : columns) {
        if (m_num_cols >= EM_MAX_COLS) { return -1; }
        m_columns[m_num_cols++] = column;
    }
    return 0;
}

int dm_sensing_db_tables_t::init()
{
    int result = 0;
    result |= capabilities.init("SensingCapabilities", {
        db_column_t("RUID", db_data_type_char, 17),
        db_column_t("Layer3TransportFlags", db_data_type_tinyint, 0),
        db_column_t("BSSFlags", db_data_type_tinyint, 0),
        db_column_t("STAFlags", db_data_type_tinyint, 0),
        db_column_t("DataTypes", db_data_type_text, 0)});
    result |= agent_sta_interfaces.init("AgentSTAInterfaces", {
        db_column_t("RUID", db_data_type_char, 17),
        db_column_t("NumSTA", db_data_type_tinyint, 0),
        db_column_t("AgentSTAMAC", db_data_type_text, 0)});
    result |= layer3_paths.init("SensingLayer3Paths", {
        db_column_t("ServiceName", db_data_type_smallint, 0),
        db_column_t("TransportProtocol", db_data_type_tinyint, 0),
        db_column_t("DestinationPort", db_data_type_smallint, 0),
        db_column_t("SourcePort", db_data_type_smallint, 0),
        db_column_t("Active", db_data_type_tinyint, 0)});
    result |= exchanges.init("SensingExchanges", {
        db_column_t("ExchangeID", db_data_type_int, 0),
        db_column_t("ExchangeType", db_data_type_tinyint, 0),
        db_column_t("MeasurementsRequested", db_data_type_tinyint, 0),
        db_column_t("Period", db_data_type_smallint, 0),
        db_column_t("Bandwidth", db_data_type_smallint, 0),
        db_column_t("DataType", db_data_type_int, 0),
        db_column_t("ResultCode", db_data_type_tinyint, 0)});
    return result;
}
