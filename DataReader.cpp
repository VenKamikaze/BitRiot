#include "DataReader.h"

DataReader::DataReader()
{
	m_pIntFilemap = new map<string , map<string, int> >;
	m_pStrFilemap = new map<string , map<string, string> >;

	m_errorString = "";
	m_windowHandle = NULL;
}

DataReader::~DataReader()
{
	delete m_pIntFilemap;
	delete m_pStrFilemap;
}

DataReader * DataReader::getInstance()
{
	static DataReader instance;
	return &instance;
}

void DataReader::setWindowHandle(HWND mainWindowHandle)
{
	m_windowHandle = mainWindowHandle;    
}

bool DataReader::displayAnyErrors()
{
	if (m_windowHandle != NULL)
	{
		if (m_errorString.length() == 0)
			return false;
		else
		{
            MessageBox(m_windowHandle, 
				m_errorString.c_str(), 
				NULL, MB_ICONEXCLAMATION);
			SendMessage(m_windowHandle,WM_CLOSE,0,0);
			return true;
		}
	} else {
		return false;
	}
}

void DataReader::clearCache()
{
	m_pIntFilemap->clear();
	m_pStrFilemap->clear();
}

const int DataReader::getIntFromFile(const string & constantName, const string & filename)
{
	map<string , map<string, int> >::iterator filemapIterator = m_pIntFilemap->find(filename);
	if (filemapIterator == m_pIntFilemap->end())
	{
		// filename was not found in filemap, try loading it in
		bool fileLoaded = readTextFile(filename);
		if (fileLoaded)
		{
			// use a bit of recursion (one call)
			return getIntFromFile(constantName, filename);
		} else {
			// error message set by readTextFile()
			displayAnyErrors();
			return 1;
		}
	} else {
		// filename is found, try getting constant value by constant name string
		map<string, int> & valuemap = filemapIterator->second;

		map<string, int>::iterator valuemapIterator = valuemap.find(constantName);
		if (valuemapIterator == valuemap.end())
		{
			// constant name not found in file listing
			m_errorString = "Constant: " + constantName + " not found in " + filename + "!";
			displayAnyErrors();
			return 1;
		} else {
			// constant name was found, return the value :D
			return valuemapIterator->second;
		}
	} // end if filename found
}

const string DataReader::getStringFromFile(const string & constantName, const string & filename)
{
	map<string , map<string, string> >::iterator filemapIterator = m_pStrFilemap->find(filename);
	if (filemapIterator == m_pStrFilemap->end())
	{
		// filename was not found in filemap, try loading it in
		bool fileLoaded = readTextFile(filename);
		if (fileLoaded)
		{
			// use a bit of recursion (one call)
			return getStringFromFile(constantName, filename);
		} else {
			// error message set by readTextFile()
			displayAnyErrors();
			return "";
		}
	} else {
		// filename is found, try getting constant value by constant name string
		map<string, string> & valuemap = filemapIterator->second;

		map<string, string>::iterator valuemapIterator = valuemap.find(constantName);
		if (valuemapIterator == valuemap.end())
		{
			// constant name not found in file listing
			m_errorString = "Constant: " + constantName + " not found in " + filename + "!";
			displayAnyErrors();
			return "";
		} else {
			// constant name was found, return the value :D
			return valuemapIterator->second;
		}
	} // end if filename found
}

bool DataReader::readTextFile(const string & filename)
{
	ifstream inputFile;
	inputFile.open(filename.c_str());
	if (!inputFile.is_open())
	{
		m_errorString = "Could not open file: " + filename;
		return false;
	}

	// set up map<string, int> for this file
	map<string, int> intValueMap;
	map<string, string> strValueMap;

	// loop through input file as char types
	char input;
	inputFile.get(input);
	while ((input != '\0') && inputFile)
	{
		if (isWhitespace(input))
		{
			inputFile.get(input);
			continue;
		}
		if (input == '#')
		{
			// this line is a comment, ignore it
			while ((input != '\n') && (input != '\0'))
			{
				inputFile.get(input);
			}
			continue;
		}
		if (input == '\n')
		{
			inputFile.get(input);
			continue; // blank line
		}

		// get first string as constant name
		string constantName;
		while (isWhitespace(input) == false)
		{
			// add input to string, after checking for unexpected end of string or line
			if (input == '\0')
			{
				m_errorString = "Unexpected end of file: " + filename + " after string: " + constantName;
				inputFile.close();
				return false;
			}
			if (input == '\n')
			{
				m_errorString = "Unexpected end of line in file: " + filename + " after string: " + constantName;
				inputFile.close();
				return false;
			}
			constantName.push_back(input);
			inputFile.get(input);
		}

		// whitespace found, constantName is complete
		// remove inbetween whitespace
		while (isWhitespace(input))
		{
			inputFile.get(input);		
			if (!inputFile)
				break;
		}
		// check for unexpected end of file
		if (!inputFile)
		{
			m_errorString = "Unexpected end of file: " + filename + " after string: " + constantName;
			inputFile.close();
			return false;
		}
		if (input == '\n')
		{
			m_errorString = "Unexpected end of line in file: " + filename + " after string: " + constantName;
			inputFile.close();
			return false;
		}

		// get int or string
		string valueString;

		// loop till end of line (and ignore whitespace)
		while (input != '\n')
		{
			valueString.push_back(input);
			inputFile.get(input);
			if (!inputFile)
				break;
		}		
		if (valueString.length() == 0)
		{
			// error reading int
			m_errorString = "Error reading value in file: " + filename + " after: " + constantName;
			inputFile.close();
			return false;
		}
		if ((input != '\n') && (!(isAlphaNum(input))))
		{
			m_errorString = "Error reading value in file: " + filename + " after: " + constantName;
			inputFile.close();
			return false;
		}

		// test for string or int
		if (isAlpha(valueString.at(0)))
		{
			// this is a string constant
			pair<string, string> entry(constantName, valueString);
			strValueMap.insert(entry);

		} else {
			// must begin with a number
			if(!isNumericString(valueString))
			{
				m_errorString = "Value after string: " + constantName + " in file: " 
					+ filename + " begins with a number but is not an int.";
				inputFile.close();
				return false;
			} else {
				// int value is ok, add to valueMap
				stringstream ss(valueString);
				int constantValue;
				ss >> constantValue;

				pair<string, int> entry(constantName, constantValue);
				intValueMap.insert(entry);
			}
		}
		

		// remove whitespace
		while (isWhitespace(input))
		{
			inputFile.get(input);	
			if (!inputFile)
				break;
		}

		// test for eof
		if (!inputFile)
			break;
		else
			// begin loop again
			inputFile.get(input);
	}

	// we've now parsed the input file
	inputFile.close();

	// add the constant valueMaps to the object's filemaps
	pair<string, map<string, int> > intFilemapEntry(filename, intValueMap);
	m_pIntFilemap->insert(intFilemapEntry);
	pair<string, map<string, string> > strFilemapEntry(filename, strValueMap);
	m_pStrFilemap->insert(strFilemapEntry);

	return true;
}

inline bool DataReader::isWhitespace(char c)
{
	return ((c == ' ') || (c == '\t'));
}

inline bool DataReader::isAlphaNum(char c)
{
	return (isAlpha(c) || isDigit(c));
}

inline bool DataReader::isAlpha(char c)
{
	return ((('A' <= c) && (c <= 'Z')) || (('a' <= c) && (c <= 'z')));
}

inline bool DataReader::isDigit(char c)
{
	return (('0' <= c) && (c <= '9'));
}

bool DataReader::isNumericString(string s)
{
	for (unsigned int i = 0; i < s.length(); i++)
	{
		if (!isDigit(s.at(i)))
			return false;
	}

	return true;
}