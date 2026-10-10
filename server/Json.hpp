#ifndef JSON_HPP
#define JSON_HPP

// Minimal JSON *writer*. The server never parses JSON (clients send plain text).
#include <string>
#include <stdio.h>

namespace json{

inline std::string str(const std::string &s){
    std::string out = "\"";
    for(unsigned char c : s){
        switch(c){
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if(c < 0x20){
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                    out += (char)c;
        }
    }
    out += "\"";
    return out;
}

inline std::string num(int v){ return std::to_string(v); }
inline std::string num(long v){ return std::to_string(v); }
inline std::string num(unsigned v){ return std::to_string(v); }
inline std::string num(double v){
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}
inline std::string boolean(bool b){ return b ? "true" : "false"; }

}

#endif
