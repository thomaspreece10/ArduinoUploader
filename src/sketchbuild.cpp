#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "syspil.h"
#include "processpil.h"
#include "sketchbuild.h"

using namespace std;

void ConsoleOutput(const char* text, const char* s = 0);
extern char* appdir;

#define COMPILE_OPTS " -s -Os -pipe -fno-exceptions -ffunction-sections -fdata-sections -MMD -DARDUINO=104"

bool CArduinoBuilder::ParseSketch(const char* sketch)
{
    // parse and list referenced libraries
    const char* p = sketch;
    bool needMod = true;
    while ((p = strstr(p, "#include ")))
    {
        p += 9;
        while (*p && *p != '\"' && *p != '<')
            p++;
        if (*p)
        {
            char *q = strchr((char*)p, '.');
            if (q)
            {
                int l = q - p - 1;
                char buf[64];
                if (l >= sizeof(buf)) l = sizeof(buf) - 1;
                memcpy(buf, p + 1, l);
                buf[l] = 0;
#ifdef WIN32
                if (!_stricmp(buf, "Arduino")) {
#else
                if (!strcasecmp(buf, "Arduino")) {
#endif
                    // referenced Arduino.h, no need to mod
                    needMod = false;
                    continue;
                }
                bool found = false;
                // see if the referenced header file is one of stock libraries
                for (int i = 0; i < (int)syslibs.size(); i++)
                {
                    if (syslibs[i].compare(buf) == 0)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found) continue;
                found = false;
                // see if this library is already referenced
                for (int i = 0; i < (int)libs.size(); i++)
                {
                    if (libs[i].compare(buf) == 0)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    libs.push_back(buf);
                }
            }
        }
    }
    if ((p = strstr(sketch, "Serial.begin")) && (p = strchr(p, '(')))
    {
        baudrate = atoi(p + 1);
    }
    return needMod;
}

#define ISSPACE(c) (c == ' ' || c == '\r' || c == '\n' || c == '\t')
#define NONSPACE(c) (c != ' ' && c != '\r' && c != '\n' && c != '\t')

// generate source code file from sketch file
int CArduinoBuilder::GenSourceFiles(const char* sketch, const char* srcpath)
{
    FILE *fp = fopen(srcpath, "w");
    if (!strstr(sketch, "<Arduino.h>"))
    {
        fprintf(fp, "#line 1\r\n#include <Arduino.h>\r\n");
    }

    fprintf(fp, "%s", sketch);
    fclose(fp);
    return SOURCE_OK;
}

BOARD_CONFIG boards[] =
{
    {"Arduino Uno", "atmega328p", "standard", 115200, "uno", 32768, 2048, 1024},
    {"Arduino Leonardo", "atmega32u4", "leonardo", 57600, "leonardo", 32768, 2560, 1024},
    {"Arduino Esplora", "atmega32u4", "leonardo", 57600, "esplora", 32768, 2560, 1024},
    {"Arduino Micro", "atmega32u4", "micro", 57600, "micro", 32768, 2560, 1024},
    {"Arduino Duemilanove (328)", "atmega328p", "standard", 57600, "duemilanove328", 32768, 2048, 1024},
    {"Arduino Duemilanove (168)", "atmega168", "standard", 19200, "duemilanove168", 16384, 1024, 512},
    {"Arduino Nano (328)", "atmega328p", "eightanaloginputs", 57600, "nano328", 32768, 2048, 1024},
    {"Arduino Nano (168)", "atmega168", "eightanaloginputs", 19200, "nano168", 16384, 1024, 512},
    {"Arduino Mini (328)", "atmega328p", "eightanaloginputs", 57600, "mini328", 32768, 2048, 1024},
    {"Arduino Mini (168)", "atmega168", "eightanaloginputs", 19200, "mini168", 16384, 1024, 512},
    {"Arduino Pro Mini (328)", "atmega328p", "standard", 57600, "promini328", 32768, 2048, 1024},
    {"Arduino Pro Mini (168)", "atmega168p", "standard", 19200, "promini168", 16384, 1024, 512},
    {"Arduino Mega 2560/ADK", "atmega2560", "mega", 115200, "mega2560", 262144, 8192, 4096},
    {"Arduino Mega 1280", "atmega1280", "mega", 57600, "mega1280", 131072, 8192, 4096},
    {"Arduino Mega 8", "atmega8", "standard", 19200, "mega8", 8192, 1024, 512},
    {"Microduino Core+ (644P)", "atmega644p", "plus", 115200, "uduino644p", 65536, 4096, 2048},
    {0},
};

std::string CArduinoBuilder::GetCompileOpts()
{
    stringstream opts;
    stringstream dir;
    opts << COMPILE_OPTS
         << " -DF_CPU=" << freq << "000000L"
         << " -mmcu=" << board->mcu
         << " -Iarduino/hardware/arduino/cores/arduino -Iarduino/hardware/arduino/variants/" << board->variant;
    for (int i = 0; i < (int)libs.size(); i++)
    {
        if (libs[i].empty())
            continue;

        dir.str("");
        dir << "arduino/libraries/" << libs[i];
        if (IsDir(dir.str().c_str()))
        {
            opts << " -I" << dir.str();
            dir << "/utility";
            if (IsDir(dir.str().c_str()))
            {
                opts << " -I" << dir.str();
            }
        }
        else if (buildDir)
        {
            dir.str("");
            dir << buildDir << '/' << libs[i];
            if (IsDir(dir.str().c_str()))
            {
                opts << " -I\"" << dir.str() << '\"';
                dir << "/utility";
                if (IsDir(dir.str().c_str()))
                {
                    opts << " -I\"" << dir.str() << '\"';
                }
            }
            else
            {
                continue;
            }
        }
    }
    return opts.str();
}

const char* CArduinoBuilder::GetCompiler(const char* filename)
{
    char *tool = 0;
    char *p = strrchr((char*)filename, '.');
    if (p++)
    {
#ifdef WIN32
        if (!_stricmp(p, "cpp"))
            tool = "avr-g++";
        else if (!_stricmp(p, "pde") || !_stricmp(p, "ino"))
            tool = "avr-g++ -x c++";
        else if (!_stricmp(p, "c"))
            tool = "avr-gcc";
#else
        if (!strcasecmp(p, "cpp"))
            tool = "avr-g++";
        else if (!strcasecmp(p, "pde") || !strcasecmp(p, "ino"))
            tool = "avr-g++ -x c++";
        else if (!strcasecmp(p, "c"))
            tool = "avr-gcc";
#endif
    }
    return tool;
}

std::string CArduinoBuilder::GetFileName(const char* path)
{
    const char *p = strrchr((char*)path, '/');
    if (!p) p = strrchr((char*)path, '\\');
    if (!p++) p = path;
    return p;
}

std::string CArduinoBuilder::GetFileNameNoExt(const char* path)
{
    string name;
    const char *p = strrchr((char*)path, '/');
    if (!p)
        p = strrchr((char*)path, '\\');
    if (!p++)
        p = path;
    name = p;
    int i = name.find_last_of('.');
    if (i > 0)
        name.erase(i);
    return name;
}

static char* LoadTextFile(const char* filename)
{
    char *content;
    FILE* fp = fopen(filename, "rb");
    if (!fp)
        return 0;
    fseek(fp, 0, SEEK_END);
    int len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    content = (char*)malloc(len + 1);
    fread(content, 1, len, fp);
    content[len] = 0;
    fclose(fp);
    return content;
}

int CArduinoBuilder::BuildSketch()
{
    int ret = SUCCESS;
    stringstream cmd;
    char buf[MAX_PATH];
    char fn[MAX_PATH];
    char dir[MAX_PATH];
    int progress = 0;
    int totalsteps = 2;
    bool corebuilt;
    string srcfn;

    if (hexfile)
    {
        srcfn = GetFileName(sketchPath);
    }

    objs.clear();
    libs.clear();
    syslibs.clear();
    sources.clear();

    // scan stock libraries
    if (ReadDir("arduino/libraries", fn) == 0) do
    {
        if (fn[0] == '.') continue;
#ifdef WIN32
        _snprintf(buf, sizeof(buf), "arduino/libraries/%s/%s.h", fn, fn);
#else
        snprintf(buf, sizeof(buf), "arduino/libraries/%s/%s.h", fn, fn);
#endif
        if (!IsFileExist(buf))
            continue;
        syslibs.push_back(fn);
    } while(ReadDir(0, fn) == 0);

    // pre-processing
    if (ReadDir(buildDir, fn) != 0)
    {
        return ERROR_GENERIC;
    }
    // scan all source code files in the directory for referenced libarries
    do
    {
        char *p = strrchr((char*)fn, '.');
        if (!p)
            continue;
#ifdef WIN32
        bool isMain = (_stricmp(srcfn.c_str(), fn) == 0);
#else
        bool isMain = (strcasecmp(srcfn.c_str(), fn) == 0);
#endif

        sprintf(buf, "%s/%s", buildDir, fn);

#ifdef WIN32
        if (!isMain && (!_stricmp(p, ".c") || !_stricmp(p, ".cpp"))) {
#else
        if (!isMain && (!strcasecmp(p, ".c") || !strcasecmp(p, ".cpp"))) {
#endif
            char *content = LoadTextFile(buf);
            if (content)
            {
                ParseSketch(content);
                free(content);
                sources.push_back(buf);
            }
            continue;
        }
#ifdef WIN32
        if (!isMain && _stricmp(p, ".pde") && _stricmp(p, ".ino"))
#else
        if (!isMain && strcasecmp(p, ".pde") && strcasecmp(p, ".ino"))
#endif
            continue;

        // load sketch content
        char *content = LoadTextFile(buf);
        if (!content)
        {
            ConsoleOutput("Unable to load source code file - %s\r\n", buf);
            continue;
        }

        // avoid including other main sketch
        if (!srcfn.empty() && !isMain && (strstr(content, "void setup()") || strstr(content, "void loop()")))
        {
            free(content);
            continue;
        }

        if (ParseSketch(content))
        {
            // need to modify source code
            sprintf(buf, "%s/%s.cpp", workDir, fn);
            ret = GenSourceFiles(content, buf);
            free(content);
            if (ret != SOURCE_OK)
            {
                return ERROR_ANALYZE_SKETCH;
            }
        }
        sources.push_back(buf);
    } while (ReadDir(0, fn) == 0);
    totalsteps += sources.size();

    corebuilt = (corefile && IsFileExist(corefile));

    // scan core files
    if (!corebuilt)
    {
        if (ReadDir("arduino/hardware/arduino/cores/arduino", fn) != 0)
        {
            return ERROR_GENERIC;
        }
        do
        {
            char *p = strrchr((char*)fn, '.');
            if (!p) continue;
#ifdef WIN32
            if (!_stricmp(p, ".c") || !_stricmp(p, ".cpp")) {
#else
            if (!strcasecmp(p, ".c") || !strcasecmp(p, ".cpp")) {
#endif
                cores.push_back(fn);
            }
        } while (ReadDir(0, fn) == 0);
        totalsteps += cores.size();
    }


    ConsoleOutput("Referenced libraries:");
    // list referenced libraries
    for (int i = 0; i < (int)libs.size(); i++)
    {
        sprintf(dir, "arduino/libraries/%s", libs[i].c_str());
        if (!IsDir(dir))
        {
            sprintf(dir, "%s/%s", buildDir, libs[i].c_str());
            if (!IsDir(dir) != 0)
            {
                continue;
            }
        }
        ConsoleOutput(" [%s]", libs[i].c_str());
    }
    ConsoleOutput("\r\n");
    // remove library reference if available in sketch directory
    for (int i = 0; i < (int)libs.size(); i++)
    {
        // no need to compile system library if there is a local one so remove it's reference
        const char* libname = libs[i].c_str();
        totalsteps++;
        for (int j = 0; j < (int)sources.size(); j++)
        {
            string srcname = GetFileNameNoExt(sources[j].c_str());
            if (!stricmp(libname, srcname.c_str()))
            {
                // found reference library, remove it
                //ConsoleOutput("[%s] is custom library\r\n", libname);
                libs[i] = "";
                totalsteps--;
            }
        }
    }
    
    do
    {
        char* extraopts = "";
        const char* tool;

        // compile main cpp file
        if (strstr(board->name, "Leonardo"))
        {
            extraopts = " -DUSB_VID=0x2341 -DUSB_PID=0x8036";
        }
        else if (strstr(board->name, "Micro"))
        {
            extraopts = " -DUSB_VID=0x2341 -DUSB_PID=0x8037";
        }
        else if (strstr(board->name, "Esplora"))
        {
            extraopts = " -DUSB_VID=0x2341 -DUSB_PID=0x803C";
        }

        // compile all source code in the direcotry
        if (hexfile)
        {
            for (int i = 0; i < (int)sources.size(); i++)
            {
                if (!(tool = GetCompiler(sources[i].c_str())))
                    continue;

                string f = GetFileName(sources[i].c_str());
                ConsoleOutput("\r\nCompiling %s...\r\n", f.c_str());

                sprintf(buf, "%s/%s.o", workDir, f.c_str());
                objs.push_back(buf);	// save object path

                cmd.str("");
                cmd << tool	<< " -c" << extraopts << GetCompileOpts();
                if (buildDir)
                    cmd << " -I\"" << buildDir << '\"';
                cmd << " \"" << sources[i].c_str() << "\" -o \"" << buf << "\"";

                //#ifdef _DEBUG
                ConsoleOutput("%s\r\n", cmd.str().c_str());
                //#endif

                if (ShellRun(&proc, cmd.str().c_str()) != 0)
                {
                    ConsoleOutput("Error calling compiler");
                    break;
                }

                if (!IsFileExist(buf))
                {
                    ConsoleOutput("Error compiling source code.");
                    ret = ERROR_BUILD_LIB;
                    break;
                }
                ShowProgress(++progress * 100 / totalsteps);
            }
        }

        if (!corebuilt)
        {
            // compile core files
            for (int i = 0; i < (int)cores.size() && ret != ERROR_BUILD_LIB; i++)
            {
                if (!(tool = GetCompiler(cores[i].c_str())))
                    continue;

                string f = GetFileName(cores[i].c_str());
                ConsoleOutput("\r\nCompiling %s...\r\n", f.c_str());

                sprintf(buf, "%s/%s.o", workDir, f.c_str());

                cmd.str("");
                cmd << tool	<< " -c" << extraopts << GetCompileOpts() << " \"" << appdir << "arduino/hardware/arduino/cores/arduino/" << cores[i].c_str() << "\" -o \"" << buf << "\"";

                //#ifdef _DEBUG
                ConsoleOutput("%s\r\n", cmd.str().c_str());
                //#endif

                if (ShellRun(&proc, cmd.str().c_str()) != 0)
                {
                    ConsoleOutput("Error calling compiler");
                    break;
                }

                if (!IsFileExist(buf))
                {
                    ConsoleOutput("Error compiling core file.");
                    ret = ERROR_BUILD_LIB;
                    break;
                }

                if (!corefile)
                {
                    objs.push_back(buf);	// save object path
                }
                else
                {
                    cmd.str("");
                    cmd << "avr-ar rcs \"" << corefile << "\" \"" << buf << "\"";
#ifdef _DEBUG
                    ConsoleOutput("%s\r\n", cmd.str().c_str());
#endif
                    if (ShellRun(&proc, cmd.str().c_str()) != 0)
                    {
                        ConsoleOutput("Error generating core file");
                        break;
                    }
                }

                ShowProgress(++progress * 100 / totalsteps);
            }
        }

        if (libfile)
        {
            cmd.str("");
            cmd << "avr-ar rcs \"" << libfile;
            remove(libfile);
            ShellRun(&proc, cmd.str().c_str());
        }
        // compile system libraries
        for (int i = 0; i < (int)libs.size() && ret != ERROR_BUILD_LIB; i++)
        {
            if (libs[i].empty()) continue;
            sprintf(dir, "arduino/libraries/%s", libs[i].c_str());
            if (ReadDir(dir, fn) != 0)
            {
                sprintf(dir, "%s/%s", buildDir, libs[i].c_str());
                if (ReadDir(dir, fn) != 0)
                {
                    continue;
                }
            }

            ConsoleOutput("\r\nCompiling library [%s]...\r\n", libs[i].c_str());
            // compile library files
            do {
                if (!(tool = GetCompiler(fn)))
                    continue;

                sprintf(buf, "%s/%s.o", workDir, fn);
                objs.push_back(buf);	// save object path

                cmd.str("");
                cmd << tool << " -c" << extraopts << GetCompileOpts()
                    << " \"" << dir << "/" << fn
                    << "\" -o \"" << buf << "\"";

                //#ifdef _DEBUG
                ConsoleOutput("%s\r\n", cmd.str().c_str());
                //#endif

                if (ShellRun(&proc, cmd.str().c_str()) != 0)
                {
                    ConsoleOutput("Error calling compiler");
                    break;
                }

                if (!IsFileExist(buf))
                {
                    ConsoleOutput("Error compiling the library");
                    ret = ERROR_BUILD_LIB;
                    break;
                }

                if (libfile)
                {
                    cmd.str("");
                    cmd << "avr-ar rcs \"" << libfile << "\" \"" << buf << "\"";
#ifdef _DEBUG
                    ConsoleOutput("%s\r\n", cmd.str().c_str());
#endif
                    if (ShellRun(&proc, cmd.str().c_str()) != 0)
                    {
                        ConsoleOutput("Error generating core file");
                        break;
                    }
                }
            } while (ReadDir(0, fn) == 0);

            // compile utility
            sprintf(dir, "arduino/libraries/%s/utility", libs[i].c_str());
            if (ReadDir(dir, fn) != 0)
            {
                sprintf(dir, "%s/%s/utility", buildDir, libs[i].c_str());
                if (ReadDir(dir, fn) != 0)
                {
                    continue;
                }
            }
            do
            {
                if (!(tool = GetCompiler(fn)))
                    continue;

                cmd.str("");
                cmd << tool << " -c" << extraopts << GetCompileOpts()
                    << " \"" << dir << '/' << fn
                    << "\" -o \"" << workDir << "/" << fn << ".o\"";

                //#ifdef _DEBUG
                ConsoleOutput("%s\r\n", cmd.str().c_str());
                //#endif

                if (ShellRun(&proc, cmd.str().c_str()) != 0)
                {
                    ConsoleOutput("Error calling compiler");
                    break;
                }

                sprintf(buf, "%s/%s.o", workDir, fn);
                objs.push_back(buf);	// save object path

                if (!IsFileExist(buf))
                {
                    ConsoleOutput("Error compiling the library utility");
                    ret = ERROR_BUILD_LIB;
                    break;
                }
                if (libfile)
                {
                    cmd.str("");
                    cmd << "avr-ar rcs \"" << libfile << "\" \"" << buf << "\"";
#ifdef _DEBUG
                    ConsoleOutput("%s\r\n", cmd.str().c_str());
#endif
                    if (ShellRun(&proc, cmd.str().c_str()) != 0)
                    {
                        ConsoleOutput("Error generating core file");
                        break;
                    }
                }
            } while (ReadDir(0, fn) == 0);
            ShowProgress(++progress * 100 / totalsteps);
        }

        if (ret == ERROR_BUILD_LIB)
            break;

        // linking built objects
        if (hexfile)
        {
            ConsoleOutput("\r\nLinking all objects...\n");
            cmd.str("");
            cmd << "avr-gcc -Wl,--gc-sections -mmcu=" << board->mcu << " -lm";
            for (int i = 0; i < (int)objs.size(); i++)
            {
                cmd << " \"" << objs[i] << "\"";
            }

            if (corefile)
                cmd << " \"" << corefile << "\"";
            sprintf(buf, "%s/%s.elf", workDir, GetFileNameNoExt(hexfile).c_str());
            cmd << " -o \"" << buf << "\"";

#ifdef _DEBUG
            ConsoleOutput("%s\r\n", cmd.str().c_str());
#endif

            if (ShellRun(&proc, cmd.str().c_str()) != 0)
            {
                ConsoleOutput("Error calling linker");
                break;
            }

            if (!IsFileExist(buf))
            {
                ConsoleOutput("Error linking objects");
                ret = ERROR_LINK;
                break;
            }

            ShowProgress(++progress * 100 / totalsteps);

            GetBinaryInfo(buf);

            if (target.eepBytes > 0)
            {
                ConsoleOutput("\r\nGenerating EEPROM HEX (%s.eep)...", hexfile);
                cmd.str("");
                cmd << "avr-objcopy --no-change-warnings -j .eeprom --change-section-lma .eeprom=0 -O ihex \"" << buf << "\" \"" << hexfile << ".eep\"";
                ShellRun(&proc, cmd.str().c_str());
            }

            ConsoleOutput("\r\nGenerating program HEX (%s)... ", hexfile);
            cmd.str("");
            cmd << "avr-objcopy -O ihex -R .eeprom \"" << buf << "\" \"" << hexfile << '\"';
            ShellRun(&proc, cmd.str().c_str());

            // remove ELF file
            remove(buf);
            if (!IsFileExist(hexfile))
            {
                ConsoleOutput("failed");
                ret = ERROR_LINK;
                break;
            }
            ConsoleOutput("\r\n");
            ShowProgress(++progress * 100 / totalsteps);
            ConsoleOutput("\r\nCompiliation successful completed!");
            ret = SUCCESS;
        }
    } while(0);

    // cleanup
    for (int i = 0; i < (int)objs.size(); i++)
    {
        sprintf(buf, "%s/%s.o", workDir, objs[i].c_str());
        remove(buf);
        sprintf(buf, "%s/%s.d", workDir, objs[i].c_str());
        remove(buf);
    }
    objs.clear();
    ShowProgress(100);
    return ret;
}

string CArduinoBuilder::GetBinaryInfo(const char* elfpath)
{
    char buf[512] = {0};
    stringstream cmd;
    SHELL_PARAM proc2 = {SF_REDIRECT_STDOUT | SF_READ_STDOUT_ALL};
    cmd << "avr-size --common --mcu=" << board->mcu << " --format=avr \"" << elfpath << "\"";
    proc2.buffer = buf;
    proc2.iBufferSize = sizeof(buf);
    if (ShellRun(&proc2, cmd.str().c_str()) == 0 && buf[0])
    {
        char *p;
        memset(&target, 0, sizeof(target));
        if ((p = strstr(buf, "Program:")))
        {
            p += 8;
            while (*(++p) == ' ')
            {
                ;
            }
            target.progBytes = atoi(p);
        }
        if ((p = strstr(buf, "Data:")))
        {
            p += 5;
            while (*(++p) == ' ')
            {
                ;
            }
            target.dataBytes = atoi(p);
        }
        if ((p = strstr(buf, "EEPROM:")))
        {
            p += 7;
            while (*(++p) == ' ')
                ;
            target.eepBytes = atoi(p);
        }
        return buf;
    }
    return "";
}

bool CArduinoBuilder::ConvertELFtoHEX(const char* elfpath)
{
    stringstream cmd;
    GetBinaryInfo(elfpath);
    cmd << "avr-objcopy -O ihex -R .eeprom " << elfpath << " " << hexfile;
    ShellRun(&proc, cmd.str().c_str());
    if (target.eepBytes > 0)
    {
        cmd.str("");
        cmd << "avr-objcopy --no-change-warnings -j .eeprom --change-section-lma .eeprom=0 -O ihex " << elfpath << " " << hexfile << ".eep";
        ShellRun(&proc, cmd.str().c_str());
    }
    if (!IsFileExist(hexfile))
    {
        return false;
    }
    else
    {
        return true;
    }
}

int CArduinoBuilder::ReadProcessConsole()
{
    int bytesRead = ShellRead(&proc, 500);
    if (bytesRead <= 0)
    {
        return bytesRead;
    }
    // parse console
    char *p = proc.buffer;
    if (state == STATE_UPLOADING)
    {
        p = strstr(proc.buffer, "Writing ");
        if (p)
        {
            state = STATE_PROGRAMMING;
            progress = 0;
        }
        else
        {
            p = proc.buffer;
        }
    }
    if (state == STATE_PROGRAMMING)
    {
        for (; *p; p++)
        {
            if (*p == '#')
            {
                progress = min(progress + 2, 100);
            }
        }
    }
    return bytesRead;
}

void CArduinoBuilder::ConsoleOutput(const char* text, const char* s)
{
    fprintf(stderr, text, s);
	fflush(stderr);
}

void CArduinoBuilder::ShowProgress(int percentage)
{
    progress = percentage;
    if (hexfile)
    {
        printf("\r\n[Compiliation: %d%%]\r\n", percentage);
    }
}
