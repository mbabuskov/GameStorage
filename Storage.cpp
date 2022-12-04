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
class StorageBuffer
{
private:
	size_t writes;
	size_t bytes;
	Uint8 *buffer;
	size_t index;

	StorageBuffer(StorageBuffer& copy);
	StorageBuffer(const StorageBuffer& copy);
	StorageBuffer& operator= (const StorageBuffer&);

public:
	StorageBuffer()
	{
		buffer = new Uint8[1048576];	// 1MB
		reset();
	}

	virtual ~StorageBuffer()
	{
		delete [] buffer;
	}

	void reset()
	{
		index = 0;
		writes = 0;
		bytes = 0;
	}

	size_t getWrites()
	{
		return writes;
	}

	size_t getBytes()
	{
		return bytes;
	}

	bool flush(FILE *fp)
	{
		if (index == 0)
			return true;

		writes++;
		size_t sz = fwrite(buffer, 1, index, fp);
		bytes += sz;
	    return (index == sz);
	}

	bool write(const void *source, size_t len, FILE *fp)
	{
		const Uint8 *p = (const Uint8 *)source;
		for (size_t i = 0; i < len; i++)
		{
			buffer[index] = p[i];
			index++;

			if (index > 1048569)
			{
			    if (!flush(fp))
			    	return false;
				index = 0;
			}
		}
	    return true;
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

	StorageBuffer& buffer()
	{
		static StorageBuffer b;		// shared between all instances
		return b;
	}

	bool storeByte(Uint8 dataByte)
	{
		return buffer().write(&dataByte, 1, fp);
	}
	
	bool loadByte(Uint8 &dataByte)
	{
	    return 1 == fread(&dataByte, 1, 1, fp);
	}
	
	bool storeWord(Uint32 dataWord)
	{
		return buffer().write(&dataWord, 4, fp);
	}
	
	bool loadWord(Uint32 &dataWord)
	{
	    return 1 == fread(&dataWord, 4, 1, fp);
	}

	bool storeString(const std::string& s)
	{
	    size_t len = s.length();
	    if (!storeWord(len))
	    	return false;
		
		return buffer().write(s.c_str(), len, fp);
	}
	
	bool loadString(std::string& s)
	{
	    //std::cout << "LOADING STRING..." << std::endl;
	    
	    Uint32 sz = 0;
	    if (!loadWord(sz))
		return false;
		
	    //std::cout << "STRING LEN = " << sz << std::endl;
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

	bool storeObject(FILE *flp)
	{
	    fp = flp;
	    
		// save attributes
		for (std::map<std::string, std::string>::iterator it = values.begin(); it != values.end(); ++it)
		{
			if (!storeByte(1) || !storeString((*it).first) || !storeString((*it).second))
				return false;
		}

		// save array
		if (array.size() > 0)
		{
			if (!storeByte(3))
			    return false;

			if (!storeWord(array.size()))
			    return false;

			for (std::vector<Storage *>::iterator it = array.begin(); it != array.end(); ++it)
			{
				if (!(*it)->storeObject(fp))
				    return false;
			}
		}

		// recurse into objects
		for (std::map<std::string, Storage *>::iterator it = objects.begin(); it != objects.end(); ++it)
		{
			if (!storeByte(2) || !storeString((*it).first))
				return false;

			if (!(*it).second->storeObject(fp))
			    return false;
		}

		return storeByte(99);
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
	std::string header;		// saved at the beginning of the file, for quick file info
	std::vector<Storage *> array;
	bool hasChanges;	// used by external code to track when it needs saving, bundled here for convenience

	Storage()
		:hasChanges(false),fp(0)
	{
	}
	
	virtual ~Storage()
	{
		clear();
	}

	void clear()
	{
		for (std::vector<Storage *>::iterator it = array.begin(); it != array.end(); ++it)
			delete (*it);
		array.clear();

		for (std::map<std::string, Storage *>::iterator it = objects.begin(); it != objects.end(); ++it)
			delete (*it).second;
		objects.clear();

		values.clear();
	}

	bool isEmpty()
	{
		return values.empty() && objects.empty() && array.empty();
	}
	
	bool save(const std::string& filename)
	{
	    fp = fopenutf8(filename.c_str(), "w+b");
	    if (!fp)
	    {
			std::cout << "ERROR: Saving failed. File: " << filename << std::endl;
	    	return false;
	    }

	    buffer().reset();
	    bool ok = storeString(header) && storeObject(fp) && buffer().flush(fp);
	    fclose(fp);
	    if (ok)
	    	std::cout << "Saved data to: " << filename;
	    else
	    	std::cout << "ERROR: Failed saving data to: " << filename;
		std::cout << std::endl;
	    return ok;
	}

	int64_t getFileSize()
	{
#ifdef WIN32
		_fseeki64(fp, 0L, SEEK_END);
		int64_t p = _ftelli64(fp);
		_fseeki64(fp, 0L, SEEK_SET);
		return p;
#else
		fseek(fp, 0L, SEEK_END);
		int64_t p = ftello(fp);
		fseek(fp, 0L, SEEK_SET);
		return p;
#endif
	}

	bool load(const std::string& filename, bool reportMissingFile)
	{
	    fp = fopenutf8(filename.c_str(), "rb");
	    if (!fp)
	    {
	    	if (reportMissingFile)
	    	{
				std::cout << "ERROR: Loading storage " << filename 
                    << ". Error: " << strerror(errno) << std::endl;
	    	}
	    	return false;
	    }

	    bool ok = loadString(header) && loadObject(fp);
	    fclose(fp);
		if (!ok)
		{
			std::cout << "ERROR: Loading storage failed: " << filename
                      << std::endl;
		}
	    return ok;
	}

	bool loadHeader(const std::string& filename)
	{
	    fp = fopenutf8(filename.c_str(), "rb");
	    if (!fp)
	    	return false;

	    bool ok = loadString(header);
	    fclose(fp);
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

	void setObject(const std::string& key, Storage *s)
	{
		std::map<std::string, Storage *>::iterator it = objects.find(key);
		if (it != objects.end())
		{
			std::cout << "WARNING: Overwriting storage object" << std::endl;
			delete (*it).second;
		}

		objects[key] = s;
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

	void setString(const std::string& key, const std::string& value)
	{
		values[key] = value;
	}

	bool getBool(const std::string& key, bool dflt = false)
	{
		return getInt(key, dflt ? 1 : 0) != 0;
	}

	void setBool(const std::string& key, bool value)
	{
		values[key] = value ? "1" : "0";
	}

	int getInt(const std::string& key, int dflt = 0)
	{
		std::string s(getString(key, ""));
		if (s.empty())
			return dflt;
		std::stringstream ss;
		int i;
		ss << s;
		ss >> i;
		return i;
	}

	bool remove(const std::string& key)
	{
		std::map<std::string, std::string>::iterator it = values.find(key);
		if (it == values.end())
			return false;

		values.erase(it);
		return true;
	}

	void setInt(const std::string& key, int value)
	{
		std::stringstream ss;
		ss << value;
		values[key] = ss.str();
	}

	float getFloat(const std::string& key, float dflt = 0.0f)
	{
		std::string s(getString(key, ""));
		if (s.empty())
			return dflt;

		double db(dflt);
		sscanf(s.c_str(), "%lf", &db);
		return (float)db;
	}

	void setFloat(const std::string& key, float value)
	{
		char stor[100];
		double db = value;
		snprintf(stor, 100, "%a", db);
		values[key] = stor;
	}

	double getDouble(const std::string& key, double dflt)
	{
		std::string s(getString(key, ""));
		if (s.empty())
			return dflt;

		double db(dflt);
		sscanf(s.c_str(), "%lf", &db);
		return db;
	}

	void setDouble(const std::string& key, double value)
	{
		char stor[100];
		snprintf(stor, 100, "%a", value);
		values[key] = stor;
	}

	void dump()
	{
		for (std::map<std::string, std::string>::iterator it = values.begin(); 
            it != values.end(); ++it)
		{
			std::cout << "STORAGE: " << (*it).first << " = " << (*it).second 
                      << std::endl;
		}
	}

	std::map<std::string, std::string>::const_iterator begin() const
	{
		return values.cbegin();
	}

	std::map<std::string, std::string>::const_iterator end() const
	{
		return values.cend();
	}

};
//------------------------------------------------------------------------------
// Add your main() here...
//------------------------------------------------------------------------------
