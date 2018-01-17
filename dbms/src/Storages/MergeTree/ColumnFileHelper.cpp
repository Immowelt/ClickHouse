#include "ColumnFileHelper.h"


void _rename(std::string from, std::string to)
{
    Poco::File from_file(from);
    Poco::File to_file(to);
    if (to_file.exists())
    {
        to_file.remove(true);
    }

    from_file.setLastModified(Poco::Timestamp::fromEpochTime(time(nullptr)));
    from_file.renameTo(to);
}

void _finalizeColumnReplacement(std::string part_path, std::string column_name)
{
    Poco::Timestamp ts;
    std::string _ts = std::to_string(ts.epochMicroseconds());
    _rename(part_path + "/" + column_name + ".bin", part_path + "/_backup_" + _ts + "_" + column_name + ".bin");
    _rename(part_path + "/" + column_name + ".mrk", part_path + "/_backup_" + _ts + "_" + column_name + ".mrk");
    _rename(part_path + "/_new_" + column_name + ".bin", part_path + "/" + column_name + ".bin");
    _rename(part_path + "/_new_" + column_name + ".mrk", part_path + "/" + column_name + ".mrk");
}
