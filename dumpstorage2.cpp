//------------------------------------------------------------------------------
#include <ctime>
#include <string>
#include <list>
#include <set>
#include <map>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <queue>
#include <algorithm>
#include <cerrno>
#define Uint8 uint8_t
#define Uint32 uint32_t
//------------------------------------------------------------------------------
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <string>
FILE *fopenutf8(const char *path, const char *opt)
{
	FILE *ret = 0;
    wchar_t wpath[4096];
    wchar_t wopt[8];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 4096) > 0 &&
    	MultiByteToWideChar(CP_UTF8, 0, opt, -1, wopt, 8) > 0)
    {
        ret = _wfopen(wpath, wopt);
    }
    if (!ret)	// try non-UTF8 version
    	ret = fopen(path, opt);
    return ret;
}
std::string WideCharToUTF8(const std::wstring& wstr)
{
	char path[4096];
	if (WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, path, 4096, 0, 0) > 0)
	{
		return path;
	}
	return "";
}
#else
#define fopenutf8 fopen
#endif
//------------------------------------------------------------------------------
std::string itos(int32_t in)
{
	std::stringstream ss;
	ss << in;
	return ss.str();
}
//------------------------------------------------------------------------------
class GameTime
{
private:
	struct tm t;
	double seconds;

public:
    bool valid;
    
	GameTime(double dt)
		:seconds(dt)
	{
		time_t s = dt;			// convert from double to time_t
		struct tm *timeInRange = localtime(&s);
		if (timeInRange)
        {
			t = *timeInRange;	// copy values
            valid = true;
        }
		else
		{
            valid = false;
			// On macOS / clang it works even beyond this date (tested)
			// On win64 / msvc  it returns 0 after year 3000   (tested)
			// https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/localtime-localtime32-localtime64?view=msvc-170
			// After 23:59:59, December 31, 3000, UTC (using _time64 and __time64_t).
			t.tm_hour = 0;
			t.tm_min = 0;
			t.tm_sec = 0;
			t.tm_isdst = -1;	// http://stackoverflow.com/questions/2598394/timestamp-issue-with-localtime-and-mktime
			t.tm_mon = 0;
			t.tm_year = 1000;	// +1000 = 2900. Allow for some extra years in case we add functions like "next year", etc.
			t.tm_mday = 1;		// 1.1.2900.
		}
	}

	std::string asString()
	{
        if (!valid)
            return "";
        
		// 2017-01-10 19:26:58
		// 0123456789012345678 = 19 chars
		char char30[30];
		snprintf(char30, 29, "%04d-%02d-%02d %02d:%02d:%02d", year(),
			month(), day(), hour(), minute(), second());
		return char30;
	}

	int month()
	{
		return t.tm_mon+1;
	}

	int year()
	{
		return t.tm_year+1900;
	}

	int day()
	{
		return t.tm_mday;
	}

	int hour()
	{
		return t.tm_hour;
	}

	int minute()
	{
		return t.tm_min;
	}

	int second()
	{
		return t.tm_sec;
	}
};
//------------------------------------------------------------------------------
class Storage
{
private:
	Storage(Storage& copy);
	Storage(const Storage& copy);
	Storage& operator= (const Storage&);

	std::map<std::string, std::string> values;
	std::map<std::string, Storage *> objects;
	FILE *fp;

	bool storeByte(Uint8 dataByte)
	{
	    return 1 == fwrite(&dataByte, 1, 1, fp);
	}
	
	bool loadByte(Uint8 &dataByte)
	{
	    return 1 == fread(&dataByte, 1, 1, fp);
	}
	
	
	bool loadWord(Uint32 &dataWord)
	{
	    return 1 == fread(&dataWord, 4, 1, fp);
	}

	bool loadString(std::string& s)
	{
	    Uint32 sz = 0;
	    if (!loadWord(sz))
			return false;
		
	    if (sz > 4095)
	    {
			std::cout << "ERROR: FOUND STRING LONGER THAN 4095 bytes" << std::endl;
			return false;
	    }
	    if (sz == 0)
	    {
			s = "";
			return true;
	    }
	    
	    char buf[4096];
	    size_t rlen = fread(&buf, 1, sz, fp);
	    buf[sz] = '\0';
	    
	    if (rlen == sz)
	    	s.assign(buf, sz);
	    
	    return rlen == sz;
	}

	bool loadObject(FILE *flp)
	{
	    fp = flp;
	    
	    while (!feof(fp))
	    {
			// read type (1 byte)
			Uint8 type;
			if (!loadByte(type))
				return false;

			//std::cout << "LOADED TYPE BYTE: " << (int)type << std::endl;
			if (type == 1)	// value
			{
				std::string key, value;
				if (!loadString(key) || !loadString(value))
					return false;
				values[key] = value;
				continue;
			}
			else if (type == 2)  // objects
			{
				std::string key;
				if (!loadString(key))
					return false;

				Storage *s = new Storage();
				if (!s->loadObject(fp))		// advances file pointer!
				{
					delete s;
					return false;
				}
				objects[key] = s;
				continue;
			}
			else if (type == 3)		// array
			{
				Uint32 len = 0;
				if (!loadWord(len))
					return false;
				if (len > 0)
				{
					array.reserve(len);
					for (int i = 0; i < len; i++)	// read in LEN objects
					{
						Storage *s = new Storage();
						if (!s->loadObject(fp))
						{
							delete s;
							return false;
						}
						array.push_back(s);
					}
				}
				continue;
			}
			else if (type == 99)
			{
				return true;
			}
			else
			{
				std::cout << "ERROR: Unknown field type " << type << std::endl;
			}
			return false;
	    }
		std::cout << "ERROR: Reached end of file without 99 marker!" << std::endl;
	    return false;
	}

public:
	std::vector<Storage *> array;
	bool hasChanges;	// used by external code to track when it needs saving, bundled here for convenience

	Storage()
		:hasChanges(false),fp(0)
	{
	}
	
	virtual ~Storage()
	{
		for (std::vector<Storage *>::iterator it = array.begin(); it != array.end(); ++it)
			delete (*it);

		for (std::map<std::string, Storage *>::iterator it = objects.begin(); it != objects.end(); ++it)
			delete (*it).second;
	}

	bool isEmpty()
	{
		return values.empty() && objects.empty() && array.empty();
	}
	
	bool load(const std::string& filename, bool reportMissingFile)
	{
	    fp = fopenutf8(filename.c_str(), "rb");
	    if (!fp)
	    {
	    	if (reportMissingFile)
	    	{
				std::cout << "ERROR: Loading storage " << filename << ". Error: " << strerror(errno) << std::endl;
	    	}
	    	return false;
	    }
            std::string s;
            loadString(s);
	    std::cout << "Header: " << s << std::endl;
	    bool ok = loadObject(fp);
	    fclose(fp);
		if (!ok)
		{
			std::cout << "ERROR: Loading storage failed: " << filename << std::endl;
		}
	    return ok;
	}
	

	Storage *getObject(const std::string& key, bool create = false)
	{
		std::map<std::string, Storage *>::iterator it = objects.find(key);
		if (it == objects.end())
		{
			if (create)
			{
				Storage *s = new Storage;
				objects[key] = s;
				return s;
			}
			else
				return 0;
		}
		return (*it).second;
	}

	bool hasKey(const std::string& key)
	{
		return (values.find(key) != values.end());
	}

	std::string getString(const std::string& key)
	{
		return getString(key, "");
	}
	std::string getString(const std::string& key, const std::string& dflt)
	{
		std::map<std::string, std::string>::iterator it = values.find(key);
		if (it == values.end())
			return dflt;
		return (*it).second;
	}

	void dump(std::string prefix)
	{
		for (std::map<std::string, std::string>::iterator it = values.begin(); it != values.end(); ++it)
		{
            double db = 0;
            if (sscanf((*it).second.c_str(), "%lf", &db))
            {
                std::cout.precision(8);
                std::cout << prefix << "." << (*it).first << " = " << (*it).second 
                    << " (double: " << std::fixed << db;
                
                if (db > 0)
                {
                    GameTime gt(db);
                    if (gt.valid)
                        std::cout << " date: " << gt.asString();
                }
                
                std::cout << ")" << std::endl;
            }
            else
                std::cout << prefix << "." << (*it).first << " = " << (*it).second << std::endl;
		}
		
		for (std::map<std::string, Storage *>::iterator it = objects.begin(); it != objects.end(); ++it)
		{
			if ((*it).second)
				(*it).second->dump(prefix+"."+(*it).first);
			else
				std::cout << prefix << (*it).first << " = " << "[Empty object]" << std::endl;
		}
		
		int i = 0;
		for (std::vector<Storage *>::iterator it = array.begin(); it != array.end(); ++it, ++i)
		{
			std::string nm = prefix+".array["+itos(i)+"]";
			if (*it)
				(*it)->dump(nm);
			else
				std::cout << nm << " = " << "[Empty object]" << std::endl;
		}
	}
};


int main(int argc, char **argv)
{
	if (argc != 2)
	{
		std::cout << "Usage: dumpstorage2 file.storage\n" << std::endl;
		return -1;
	}
	Storage s;
	s.load(argv[1], true);
	s.dump(argv[1]);
    return 0;
}

