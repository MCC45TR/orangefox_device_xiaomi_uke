// SPDX-License-Identifier: GPL-3.0-or-later
#include <cassert>
#include <chrono>
#include <string>
#include <sqlite3.h>
#include <sys/statvfs.h>
#include <iostream>
using userid_t=unsigned;
static std::string database;
static bool readonly=true;
extern "C" int __real_sqlite3_open_v2(const char*,sqlite3**,int,const char*);
extern "C" int __wrap_statvfs(const char* path,struct statvfs* state) {
    assert(std::string(path)=="/data"); state->f_flag=readonly ? ST_RDONLY : 0; return 0;
}
extern "C" int __wrap_sqlite3_open_v2(const char* path,sqlite3** db,int flags,const char* vfs) {
    assert(std::string(path)=="/data/system/locksettings.db" && (flags&SQLITE_OPEN_READONLY) &&
        !(flags&(SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE)) && (flags&SQLITE_OPEN_NOFOLLOW));
    return __real_sqlite3_open_v2(database.c_str(),db,flags,vfs);
}
class KeystoreInfo {
public:
    static std::string uint2hex(int64_t number) { return std::to_string(number); }
    std::string getHandle(userid_t user_id);
};
#include "locksettings-handle.inc"
int main(int argc,char** argv) {
    assert(argc==2); database=argv[1];
    sqlite3* writer=nullptr;
    assert(sqlite3_open(database.c_str(),&writer)==SQLITE_OK);
    const auto sql=[&](const char* text) { assert(sqlite3_exec(writer,text,nullptr,nullptr,nullptr)==SQLITE_OK); };
    sql("PRAGMA journal_mode=WAL; PRAGMA wal_autocheckpoint=0; CREATE TABLE locksettings(name TEXT,user INTEGER,value INTEGER); INSERT INTO locksettings VALUES('sp-handle',0,123);");
    KeystoreInfo reader;
    assert(reader.getHandle(0)=="123");
    sql("UPDATE locksettings SET value=456;");
    assert(reader.getHandle(0)=="456");
    assert(reader.getHandle(1).empty());
    sql("INSERT INTO locksettings VALUES('sp-handle',0,789);");
    assert(reader.getHandle(0).empty());
    readonly=false; assert(reader.getHandle(0).empty()); readonly=true;
    assert(reader.getHandle(10000).empty());
    sql("DROP TABLE locksettings; CREATE VIEW locksettings AS WITH RECURSIVE r(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM r WHERE x<100000000) SELECT 'sp-handle' AS name,0 AS user,max(x) AS value FROM r;");
    const auto began=std::chrono::steady_clock::now();
    assert(reader.getHandle(0).empty());
    assert(std::chrono::steady_clock::now()-began<std::chrono::seconds(3));
    assert(sqlite3_close(writer)==SQLITE_OK);
    std::cout<<"PASS read-only locksettings query observes committed WAL updates, bounds expensive queries and refuses duplicates, absent users, writable data and unsupported user IDs.\n";
}
