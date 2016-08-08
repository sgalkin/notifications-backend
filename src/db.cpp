#include <memory>
#include <leveldb/db.h>
#include <glog/logging.h>

namespace {
class A
{
public:
    A() {
        leveldb::Options options;
        options.create_if_missing = true;
        leveldb::DB* dbp = nullptr;
        CHECK(leveldb::DB::Open(options, "db", &dbp).ok());
        db_.reset(dbp);
        VLOG(3) << "DB opened";

        CHECK(db_->Put(leveldb::WriteOptions(), "foo", "Bar").ok());
        std::string data;
        CHECK(db_->Get(leveldb::ReadOptions(), "foo", &data).ok());
        CHECK_EQ(data, "Bar");
        
    }
    
    ~A() {
        db_.reset();
        VLOG(3) << "DB closed";
    }
    
private:
    std::unique_ptr<leveldb::DB> db_;
};
//    A a;
    
}
