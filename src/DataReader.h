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

#include <string>
#include <sstream>
#include <map>

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

	// for displaying error messages
	//void setWindowHandle(HWND windowHandle);
	bool displayAnyErrors();

private:
	DataReader();

    map<string , map<string, int> > * m_pIntFilemap;
	map<string , map<string, string> > * m_pStrFilemap;

    bool readTextFile(const string & filename);
	inline bool isWhitespace(char c);
	inline bool isAlphaNum(char c);
	inline bool isAlpha(char c);
	inline bool isDigit(char c);
	bool isNumericString(string s);

	//HWND m_windowHandle;
	string m_errorString;
};

#endif
