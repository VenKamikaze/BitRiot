/*
DataReader is a simple singleton class for
reading in constant int (and now std::string) values from ascii text files
My first step in using data-driven design (yay! finally!)

If you're reading this, I haven't implemented other rather
necessary datatypes such as floats, I'll get round to it at some point...
But maybe after BitRiot is mostly finished
*/

/**
 * The constant value data text files are formatted as such:
 * each line has a string for the constant name, in capitals
 * followed by at least 1 char of whitespace and then the int or string value
 *
 * Whitespace chars are ' ' and '\t'
 * Lines beginning with '#' are counted as comments and ignored
 * strings must begin with an alphabetic char and end with an alphanumeric char
 *
 * Not following these rules will display a (hopefully useful) error message
 * and the game will quit
 **/
#ifndef _DATAREADER
#define _DATAREADER

#include "MickLogger.h"

#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <utility>

#include <iostream>
#include <fstream>

using namespace std;

class DataReader
{
  public:
    static DataReader * getInstance();
    ~DataReader();

    const int getIntFromFile(const string & constantName, const string & filename);
    const string getStringFromFile(const string & constantName, const string & filename);

    void clearCache(); // clears out the current DataReader object

    // Parses `filename` now instead of on the first lookup (brief 05: the
    // simulation's data is loaded eagerly so a missing or unreadable file fails at
    // start-up and not as a silent 1 in the middle of a round). Idempotent: a file
    // that is already cached is not read again. False if it could not be read or
    // parsed; lastError() says why, and errorCount() is NOT incremented (the caller
    // reports it).
    bool loadFile(const string & filename);

    // Every constant of a loaded file as normalised (name, value) text pairs sorted
    // by name: an int is written in decimal, a string as it is. Loads the file first.
    // False (and `out` empty) if it cannot be loaded. This is what the data hash
    // is computed over (Simulation::dataHash()).
    bool normalisedEntries(const string & filename, vector<pair<string, string> > & out);

    // for displaying error messages
    //void setWindowHandle(HWND windowHandle);
    bool displayAnyErrors();

    // How many lookups have failed so far (a file that could not be read, a constant
    // that is not in its file): each returned a default (1 or "") instead of a value.
    // The game ignores them; the test harness fails a test that makes one, so a
    // missing data/ directory or a mistyped constant is loud instead of silently
    // playing with 1s.
    int errorCount() const { return m_errorCount; }
    const string & lastError() const { return m_errorString; }

  private:
    DataReader();

    map<string, map<string, int> > * m_pIntFilemap;
    map<string, map<string, string> > * m_pStrFilemap;

    bool readTextFile(const string & filename);
    inline bool isWhitespace(char c);
    inline bool isAlphaNum(char c);
    inline bool isAlpha(char c);
    inline bool isDigit(char c);
    bool isNumericString(string s);

    //HWND m_windowHandle;
    string m_errorString;
    int m_errorCount = 0;
};

#endif
