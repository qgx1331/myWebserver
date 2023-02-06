#ifndef CONFIGC_H
#define CONFIGC_H

#include <vector>
#include "global.h"

using namespace std;


//懒汉式单例设计模式,不必考虑线程安全问题，因为只初始化一次
class ConfigReader
{
private:
    ConfigReader();
public:
    ~ConfigReader();
private:
    static ConfigReader *m_instance;

public:
    static ConfigReader* getInstance() {
        if(m_instance == nullptr) {
            //加锁
            if(m_instance == nullptr) {
                m_instance = new ConfigReader();
            }
            //解锁
        }
        return m_instance;
    }

public:
    bool load(const char *confName);  //装载配置文件
    char* getString(const char *itemName);
    int getInt(const char *itemName, const int def);

public:
    vector<PConfItem> m_conf_items;  //存储配置信息的列表

};

#endif