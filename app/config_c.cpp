#include <fstream>
#include <iostream>
#include <cstring>
#include "config_c.h"

ConfigReader* ConfigReader::m_instance = nullptr;

//截取字符串左边空格
void leftTrim(char *string) {
    if(string == nullptr || *string != ' ')
        return;
    int len = strlen(string);
    int pos = 0;
    while(string[pos] == ' ') {
        ++pos;
    }
    int i = 0;
    while(pos < len) {
        string[i++] = string[pos++];
    }
    string[i] = '\0';
}

//截取字符串右边空格
void rightTrim(char *string) {
    if(string == nullptr)
        return;
    int len = strlen(string);
    while(len > 0 && string[len-1] == ' ') {
        string[--len] = '\0';
    }
}
//构造函数
ConfigReader::ConfigReader()
{

}

//析构函数
ConfigReader::~ConfigReader()
{
    vector<PConfItem>::iterator pos;
    for(pos = m_conf_items.begin(); pos != m_conf_items.end(); ++pos) {
        delete (*pos);
    }
    m_conf_items.clear();
}

//装载配置文件
bool ConfigReader::load(const char *confName)
{
    ifstream fin;
    fin.open(confName, ios::in);
    if(!fin) {
        cerr << "不能打开文件" << endl;
        return false;
    }

    char linebuf[501] = {0};
    while(fin.getline(linebuf, 500)) {
        if(linebuf[0] == ' ' || linebuf[0] == '#' || linebuf[0] == '\t' || linebuf[0] == '\n')
            continue;

        char *temp = strchr(linebuf, '=');
        if(temp != NULL) {
            PConfItem item = new ConfItem;
            memset(item, 0, sizeof(ConfItem));
            strncpy(item->item_key, linebuf, (int)(temp-linebuf));
            strcpy(item->item_value, temp+1);

            rightTrim(item->item_value);
            leftTrim(item->item_value);
            rightTrim(item->item_key);
            m_conf_items.push_back(item);
        }
    }
    fin.close();
    return true;
}

char* ConfigReader::getString(const char* itemName) 
{
    for(auto item : m_conf_items) {
        if(strcasecmp(item->item_key, itemName) == 0)
            return item->item_value;
    }
    return NULL;
}

int ConfigReader::getInt(const char* itemName, const int def)
{
    for(auto item : m_conf_items) {
        if(strcasecmp(item->item_key, itemName) == 0)
            return atoi(item->item_value);
    }
    return def;
}