#include <iostream>
#include <fstream>
#include <algorithm>
#include <regex>
#include "lib/json.hpp"
#include "auth/authbasic.h"
#include "settings.h"
#include "resources.h"
#include "helpers.h"
#include "api/filesystem/filesystem.h"
#include "api/debug/debug.h"
#include "api/app/app.h"

#if defined(__linux__)
#define OS_NAME "Linux"

#elif defined(_WIN32)
#define OS_NAME "Windows"

#elif defined(__APPLE__)
#define OS_NAME "Darwin"

#elif defined(__FreeBSD__)
#define OS_NAME "FreeBSD"
#endif
#define NL_VERSION "2.7.0"

#define APP_CONFIG_FILE "/neutralino.config.json"

using namespace std;
using json = nlohmann::json;

json options;
json globalArgs;
bool loadResFromDir = false;
string appPath;

vector <settings::ConfigOverride> configOverrides;

namespace settings {

    string joinAppPath(string filename) {
        return appPath + filename;
    }

    string getFileContent(string filename) {
        string content;
        if(!loadResFromDir)
            return resources::getFileContent(filename);
        filename = settings::joinAppPath(filename);
        fs::FileReaderResult fileReaderResult = fs::readFile(filename);
        
        if(fileReaderResult.hasError)
            debug::log("ERROR", fileReaderResult.error);
        else
            content = fileReaderResult.data;
        return content;
    }

    json getConfig() {
        if(!options.is_null())
            return options;
        json config;
        try {
            config = json::parse(settings::getFileContent(APP_CONFIG_FILE));
            options = config;

            // Apply config overrides
            json patches;
            for(auto cfgOverride: configOverrides) {
                json patch;
                patch["op"] = "replace";
                patch["path"] = cfgOverride.key;
                
                // String to actual types
                if(cfgOverride.convertTo == "int") {
                    patch["value"] = stoi(cfgOverride.value);
                }
                else if(cfgOverride.convertTo == "bool") {
                    patch["value"] = cfgOverride.value == "true";
                }
                else {
                    patch["value"] = cfgOverride.value;
                }
                
                patches.push_back(patch);
            }
            if(!patches.is_null()) {
                options = options.patch(patches);
            }
        }
        catch(exception e){
            debug::log("ERROR", "Unable to load: " + std::string(APP_CONFIG_FILE));
        }
        return options;
    }

    string getGlobalVars(){
        string jsSnippet = "var NL_OS='" + std::string(OS_NAME) + "';";
        jsSnippet += "var NL_VERSION='" + std::string(NL_VERSION) + "';";
        jsSnippet += "var NL_APPID='" + options["applicationId"].get<std::string>() + "';";
        jsSnippet += "var NL_PORT=" + std::to_string(options["port"].get<int>()) + ";";
        jsSnippet += "var NL_MODE='" + options["defaultMode"].get<std::string>() + "';";
        jsSnippet += "var NL_TOKEN='" + authbasic::getToken() + "';";
        jsSnippet += "var NL_CWD='" + fs::getCurrentDirectory() + "';";
        jsSnippet += "var NL_ARGS=" + globalArgs.dump() + ";";
        jsSnippet += "var NL_PATH='" + appPath + "';";
        jsSnippet += "var NL_PID=" + std::to_string(app::getProcessId()) + ";";

        if(!options["globalVariables"].is_null()) {
            for ( auto it: options["globalVariables"].items()) {
                jsSnippet += "var NL_" + it.key() +  "='" + it.value().get<std::string>() + "';";
            }
        }
        return jsSnippet;
    }

    void setGlobalArgs(json args) {
        int argIndex = 0;
        globalArgs = args;
        for(string arg: args) {
            settings::CliArg cliArg = _parseArg(arg); 

            // Set default path
            if(argIndex == 0) {
                appPath = fs::getDirectoryName(arg);
                if(appPath == "")
                    appPath = fs::getCurrentDirectory();
            }
            
            // Resources read mode (res.neu or from directory)
            if(cliArg.key == "--load-dir-res") {
                loadResFromDir = true;
            }
            
            // Set app path context
            if(cliArg.key == "--path") {
                appPath = cliArg.value;
            }
            
            // Override app configs
            applyConfigOverride(cliArg);
            
            argIndex++;
        }
    }

    string getMode() {
        return options["defaultMode"].get<std::string>();
    }

    void setPort(int port) {
      options["port"] = port;
    }
    
    settings::CliArg _parseArg(string argStr) {
        settings::CliArg arg;
        vector<string> argParts = helpers::split(argStr, '=');
        if(argParts.size() == 2 && argParts[1].length() > 0) {
            arg.key = argParts[0];
            arg.value = argParts[1];
        }
        else {
            arg.key = argStr;
        }
        return arg;
    }
    
    void applyConfigOverride(settings::CliArg arg) {
        map<string, vector<string>> cliMappings = {
            // Top level
            {"--mode", {"/defaultMode", "string"}},
            {"--url", {"/url", "string"}},
            {"--port", {"/port", "int"}},
            // Window mode
            {"--window-title", {"/modes/window/title", "string"}},
            {"--window-width", {"/modes/window/width", "int"}},
            {"--window-height", {"/modes/window/height", "int"}},
            {"--window-min-width", {"/modes/window/minWidth", "int"}},
            {"--window-min-height", {"/modes/window/minHeight", "int"}},
            {"--window-max-width", {"/modes/window/maxWidth", "int"}},
            {"--window-max-height", {"/modes/window/maxHeight", "int"}},
            {"--window-full-screen", {"/modes/window/fullScreen", "bool"}},
            {"--window-always-on-top", {"/modes/window/alwaysOnTop", "bool"}},
            {"--window-enable-inspector", {"/modes/window/enableInspector", "bool"}},
            {"--window-borderless", {"/modes/window/borderless", "bool"}},
            {"--window-maximize", {"/modes/window/maximize", "bool"}},
            {"--window-hidden", {"/modes/window/hidden", "bool"}},
            {"--window-resizable", {"/modes/window/resizable", "bool"}},
            {"--window-maximizable", {"/modes/window/maximizable", "bool"}},
            {"--window-icon", {"/modes/window/icon", "string"}}
        }; 
        if(cliMappings.find(arg.key) != cliMappings.end()) {
            if(arg.key == "--mode") {
                if(arg.value != "browser" && arg.value != "window" && arg.value != "cloud") {
                    debug::log("ERROR", "Unsupported mode: '" + arg.value + "'. The default mode is selected.");
                    return;
                }
            }
            settings::ConfigOverride cfgOverride;
            cfgOverride.key = cliMappings[arg.key][0];
            cfgOverride.convertTo = cliMappings[arg.key][1];
            cfgOverride.value = arg.value;
            // Make cases like --window-full-screen -> window-full-screen=true
            if(cfgOverride.convertTo == "bool" && cfgOverride.value.empty()) {
                cfgOverride.value = "true";
            }
            configOverrides.push_back(cfgOverride);
        }
    }

}
