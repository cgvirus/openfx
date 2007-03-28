#ifndef OFX_PLUGIN_CACHE_H
#define OFX_PLUGIN_CACHE_H

/*
Software License :

Copyright (c) 2007, The Foundry Visionmongers Ltd. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name The Foundry Visionmongers Ltd, nor the names of its 
      contributors may be used to endorse or promote products derived from this
      software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <string>
#include <vector>
#include <list>
#include <set>
#include <iostream>

#include <stdio.h>

#include "expat.h"

#include "ofxCore.h"
#include "ofxProperty.h"

#include "ofxhBinary.h"
#include "ofxhPluginAPICache.h"

namespace OFX {

  namespace Host {

    // forward delcarations
    class PluginDesc;   
    class Plugin;
    class PluginBinary;
    class PluginCache;

    /// C++ version of the information kept inside an OfxPlugin struct
    class PluginDesc  {
    protected :
      std::string _pluginApi;  ///< the API I implement
      int _apiVersion;         ///< the version of the API

      std::string _identifier; ///< the identifier of the plugin
      int _versionMajor;       ///< the plugin major version
      int _versionMinor;       ///< the plugin minor version

    public:

      const std::string &getPluginApi() {
        return _pluginApi;
      }
      
      int getApiVersion() {
        return _apiVersion;
      }
      
      const std::string &getIdentifier() {
        return _identifier;
        
      }
      
      int getVersionMajor() {
        return _versionMajor;
      }
      
      int getVersionMinor() {
        return _versionMinor;
      }

      PluginDesc() {
      }
      
      PluginDesc(const std::string &api,
                 int apiVersion,
                 const std::string &identifier,
                 int versionMajor,
                 int versionMinor)
        : _pluginApi(api)
        , _apiVersion(apiVersion)
        , _identifier(identifier)
        , _versionMajor(versionMajor)
        , _versionMinor(versionMinor)
      {
      }


      /// constructor for the case where we have already loaded the plugin binary and 
      /// are populating this object from it
      PluginDesc(OfxPlugin *ofxPlugin) {
        _pluginApi = ofxPlugin->pluginApi;
        _apiVersion = ofxPlugin->apiVersion;
        _identifier = ofxPlugin->pluginIdentifier;
        _versionMajor = ofxPlugin->pluginVersionMajor;
        _versionMinor = ofxPlugin->pluginVersionMinor;
      }

    };
    
    /// class that we use to manipulate a plugin
    class Plugin : public PluginDesc {
    /// owned by the PluginBinary it lives inside
    /// Plugins can only be pass about either by pointer or reference
    private :
      Plugin(const Plugin&) {}                          ///< hidden
      Plugin &operator= (const Plugin&) {return *this;} ///< hidden

    protected :
      PluginBinary *_binary; ///< the file I live inside
      int           _index;  ///< where I live inside that file
    public :
      Plugin();

      PluginBinary *getBinary()
      {
        return _binary;
      }

      int getIndex()
      {
        return _index;
      }

      /// construct this based on the struct returned by the getNthPlugin() in the binary
      explicit Plugin(PluginBinary *bin, int idx, OfxPlugin *o) : PluginDesc(o), _binary(bin), _index(idx)
      {
      }
      
      /// construct me from the cache
      explicit Plugin(PluginBinary *bin, int idx, const std::string &api,
                      int apiVersion, const std::string &identifier, 
                      int majorVersion, int minorVersion)
        : PluginDesc(api, apiVersion, identifier, majorVersion, minorVersion)
        , _binary(bin)
        , _index(idx) 
      {
      }

      virtual ~Plugin() {
      }

      bool trumps(Plugin *other) {
        int myMajor = getVersionMajor();
        int theirMajor = other->getVersionMajor();

        int myMinor = getVersionMinor();
        int theirMinor = other->getVersionMinor();

        if (myMajor > theirMajor) {
          return true;
        }
        
        if (myMajor == theirMajor && myMinor > theirMinor) {
          return true;
        }

        return false;
      }
    };

    class PluginHandle;

    /// class that represents a binary file which holds plugins
    class PluginBinary {
    /// has a set of plugins inside it and which it owns
    /// These are owned by a PluginCache
      friend class PluginHandle;

    protected :
      Binary _binary;                 ///< our binary object, abstracted layer ontop of OS calls, defined in ofxhBinary.h
      std::string _filePath;          ///< full path to the file
      std::string _bundlePath;        ///< path to the .bundle directory
      std::vector<Plugin *> _plugins; ///< my plugins
      time_t _fileModificationTime;   ///< used as a time stamp to check modification times, used for caching
      size_t _fileSize;               ///< file size last time we check, used for caching
      bool _binaryChanged;            ///< whether the timestamp/filesize in this cache is different from that in the actual binary
      
    public :

      /// create one from the cache.  this will invoke the Binary() constructor which
      /// will stat() the file.
      explicit PluginBinary(const std::string &file, const std::string &bundlePath, time_t mtime, size_t size)
        : _binary(file)
        , _filePath(file)
        , _bundlePath(bundlePath)
        , _fileModificationTime(mtime)
        , _fileSize(size)
        , _binaryChanged(false)
      {
        if (_fileModificationTime != _binary.getTime() || _fileSize != _binary.getSize()) {
          _binaryChanged = true;
        }
      }


      /// constructor which will open a library file, call things inside it, and then 
      /// create Plugin objects as appropriate for the plugins exported therefrom
      explicit PluginBinary(const std::string &file, const std::string &bundlePath, PluginCache *cache)
        : _binary(file)
        , _filePath(file)
        , _bundlePath(bundlePath)
        , _binaryChanged(false)
      {
        loadPluginInfo(cache);
      }
    
      /// dtor
      virtual ~PluginBinary();


      time_t getFileModificationTime() {
      	return _fileModificationTime;
      }
    
      size_t getFileSize() {
      	return _fileSize;
      }

      const std::string &getFilePath() {
        return _filePath;
      }
      
      const std::string &getBundlePath() {
        return _bundlePath;
      }
      
      bool hasBinaryChanged() {
        return _binaryChanged;
      }

      void addPlugin(Plugin *pe) {
        _plugins.push_back(pe);
      }

      void loadPluginInfo(PluginCache *);

      /// how many plugins?
      int getNPlugins() {return _plugins.size(); }

      /// get a plugin 
      Plugin &getPlugin(int idx) {return *_plugins[idx];}

      /// get a plugin 
      const Plugin &getPlugin(int idx) const {return *_plugins[idx];}
    };

    /// wrapper class for Plugin/PluginBinary.  use in a RAIA fashion to make sure the binary gets unloaded when needed and not before.
    class PluginHandle {
      Plugin *_p;
      PluginBinary *_b;
      OfxPlugin *_op;

    public:
      PluginHandle(Plugin *p);
      ~PluginHandle();

      OfxPlugin *operator->() {
        return _op;
      }
    };
    
    /// for later 
    struct PluginCacheSupportedApi {
      std::string api;
      int minVersion;
      int maxVersion;
      APICache::PluginAPICacheI *handler;

      PluginCacheSupportedApi(std::string _api, int _minVersion, int _maxVersion, APICache::PluginAPICacheI *_handler) :
        api(_api), minVersion(_minVersion), maxVersion(_maxVersion), handler(_handler)
      {
      }
      
      bool matches(std::string _api, int _version)
      {
        if (_api == api && _version >= minVersion && _version <= maxVersion) {
          return true;
        }
        return false;
      }
    };

    /// Where we keep our plugins.
    class PluginCache {
    protected :
      std::list<std::string>    _pluginPath;  ///< list of directories to look in
      std::list<PluginBinary *> _binaries; ///< all the binaries we know about, we own these
      std::list<Plugin *>       _plugins;  ///< all the plugins inside the binaries, we don't own these, populated from _binaries
      std::set<std::string>     _knownBinFiles;

      PluginBinary *_xmlCurrentBinary;
      Plugin *_xmlCurrentPlugin;

      std::list<PluginCacheSupportedApi> _apiHandlers;

      void scanDirectory(std::set<std::string> &foundBinFiles, const std::string &dir);
  
    public:
      /// ctor, which inits _pluginPath to default locations and not much else
      PluginCache();
      
      /// add a file to the plugin path
      void addFileToPath(const std::string &f);

      // populate the cache.  must call scanPluginFiles() after to check for changes.
      void readCache(std::istream &is);

      /// scan for plugins
      void scanPluginFiles();

      // write the plugin cache output file to the given stream
      void writePluginCache(std::ostream &os);
      
      // callback function for the XML
      void elementBeginCallback(void *userData, const XML_Char *name, const XML_Char **attrs);
      void elementCharCallback(void *userData, const XML_Char *data, int len);
      void elementEndCallback(void *userData, const XML_Char *name);

      /// register an API cache handler
      void registerAPICache(const std::string &api, int min, int max, APICache::PluginAPICacheI *apiCache) {
        _apiHandlers.push_back(PluginCacheSupportedApi(api, min, max, apiCache));
      }
      
      APICache::PluginAPICacheI* findApiHandler(const std::string &, int);

      /// find the API cache handler for the appropriate plugin
      APICache::PluginAPICacheI* findApiHandler(Plugin *plug);

      /// obtain a list of plugins to walk through
      const std::list<Plugin *> &getPlugins() {
        return _plugins;
      }
    };

    /// the global plugin cache
    extern PluginCache gPluginCache;

  }
}

#endif
