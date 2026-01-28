#include "utl/DataPointMgr.h"
#include "obj/Data.h"
#include "os/System.h"
#include "os/NetworkSocket.h"
#include "stl/_pair.h"
#include "utl/JsonEncode.h"
#include "utl/UrlEncode.h"

DataPointMgr gDataPointMgr;
DataPointMgr &TheDataPointMgr = gDataPointMgr;

DataPointMgr::~DataPointMgr() {}

DataPointMgr::DataPointMgr()
    : mDataPointRecorder(nullptr), mDebugDataPointRecorder(nullptr), mHostName(nullptr),
      mApp(nullptr), mProject(nullptr), mVersion(0) {}

DataPointRecordFunc *DataPointMgr::SetDataPointRecorder(DataPointRecordFunc *recorder) {
    DataPointRecordFunc *old = mDataPointRecorder;
    mDataPointRecorder = recorder;
    return old;
}

void DataPointMgr::RecordDataPoint(DataPoint &point) {
    if (mDataPointRecorder) {
        mDataPointRecorder(point);
    }
}

void DataPointMgr::RecordDebugDataPoint(DataPoint &point) {
    if (mDebugDataPointRecorder) {
        mDebugDataPointRecorder(point);
    }
}

bool AddValToString(const DataNode &node, String &str, unsigned int type) {
    if (node.Type() == kDataString || node.Type() == kDataSymbol) {
        if (type == 2) {
            URLEncode(node.Str(), str, false);
            return true;
        } else if (type == 1) {
            JSONStrEncode(node.Str(), str);
            return true;
        } else {
            str += node.Str();
            return true;
        }
    } else if (node.Type() == kDataFloat) {
        str += MakeString("%f", node.Float());
        return true;
    } else if (node.Type() == kDataInt) {
        str += MakeString("%d", node.Int());
        return true;
    } else
        return false;
}

void DataPointMgr::Init() {
    if (SystemConfig()) {
        DataArray *cfg = SystemConfig()->FindArray("crucible", false);
        if (cfg) {
            mHostName = cfg->FindStr("hostname");
            mApp = cfg->FindStr("app");
            mProject = cfg->FindStr("project");
        }
        unk20 = SystemConfig()->File();
        SystemConfig()->FindData("version", mVersion, false);
    } else {
        unk20 = "<unknown>";
    }

    if (!mHostName) {
        static Symbol hostnames("hostnames");
        if (SystemConfig()) {
            mHostName = SystemConfig(hostnames, "crucible")->Str(1);
        }
    }

    unk18 = NetworkSocket::GetHostName();
}

void DataPoint::ToJSON(String &str) const {
    str += "{";
    bool b3 = true;
    for (std::map<Symbol, DataNode>::const_iterator it = mNameValPairs.begin();
         it != mNameValPairs.end();
         ++it) {
        if (!b3) {
            str += ",";
        }
        str += "\"";
        str += it->first.Str();
        str += "\"";
        str += ":";
        if (it->second.Type() == kDataString || it->second.Type() == kDataSymbol) {
            str += "\"";
        }
        b3 = AddValToString(it->second, str, 1);
        if (!b3) {
            MILO_FAIL("Unsupported type %d in DataPoint::ToJSON.\n", it->second.Type());
        }
        if (it->second.Type() == kDataString || it->second.Type() == kDataSymbol) {
            str += "\"";
        }
        b3 = false;
    }
    str += "}";
}

void DataPoint::AddPair(const char *name, DataNode value) {
    if (strlen(name) != 0) {
        std::pair<std::map<Symbol, DataNode>::iterator, bool> it =
            mNameValPairs.insert(std::make_pair(name, value));
        if (!it.second) {
            MILO_FAIL("Duplicate name [%s] in DP %s.", name, mType);
        }
    }
}

void DataPoint::AddPair(Symbol name, DataNode value) { AddPair(name.Str(), value); }

DataPoint::DataPoint() {}

DataPoint::DataPoint(const char *type) {
    // someone forgot their initializer lists at home
    mType = type;
}
