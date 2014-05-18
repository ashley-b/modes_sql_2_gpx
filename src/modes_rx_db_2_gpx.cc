#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>       /* time_t, struct tm, time, localtime, strftime */
#include <string>
#include <iostream>
#include <exception>

/* Boost options */
#include "boost/program_options.hpp"

/* Boost shared pointers */
#include <boost/shared_ptr.hpp>

/* SQL Lite */
#include <sqlite3.h>

/* XML */
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>


/* Default file names to use */
const char db_file[] = "air_modes.db";
const char gpx_file[] = "air_modes.gpx";

sqlite3 *db;
xmlTextWriterPtr xml;

/* GPX XML text encoding to use */
#define GPX_ENCODING "UTF-8"

/**
 * Simple wrapper class around xmlChar
 * to take care of allocated memory.
 */
class XML_Char {
private:
	xmlChar *m_xml_char;
public:
	XML_Char(xmlChar* xml_char) : m_xml_char(xml_char) {
	}

	~XML_Char() {
		if (m_xml_char != NULL) {
			xmlFree(m_xml_char);
		}
	}

	const xmlChar* get_xmlChar() const {
		return m_xml_char;;
	}
};  /* class XML_Char */

/**
 * Simple class to wrap XML char encoding convertions
 */
class XML_Convert {
private:
	xmlCharEncodingHandlerPtr m_handler;
public:
	XML_Convert(const char* encoding) {
		m_handler = xmlFindCharEncodingHandler(encoding);

		if (!m_handler) {
			std::cerr << "ConvertInput: no encoding handler found for " <<  (encoding ? encoding : "") << std::endl;
//			throw 
		}
	}


	/**
	 * ConvertInput:
	 * \param in: string in a given encoding
	 * @encoding: the encoding used
	 *
	 * Converts @in into UTF-8 for processing with libxml2 APIs
	 *
	 * Returns the converted UTF-8 string, or NULL in case of error.
	 */
	boost::shared_ptr< XML_Char > ConvertInput(const char *in) {
		xmlChar *out;
		int ret;
		int size;
		int out_size;
		int temp;

		if (in == 0) {
			throw std::runtime_error("Input string is NULL");
		}

		size = (int) strlen(in) + 1;
		out_size = size * 2 - 1;
		out = (unsigned char *) xmlMalloc((size_t) out_size);
		if (out == NULL) {
			throw std::runtime_error("ConvertInput: no mem");
		}


		temp = size - 1;
		ret = m_handler->input(out, &out_size, (const xmlChar *) in, &temp);
		if ((ret < 0) || (temp - size + 1)) {
			if (ret < 0) {
				std::cerr << "ConvertInput: conversion wasn't successful." << std::endl;
			} else {
				std::cerr << "ConvertInput: conversion wasn't successful. converted: " << temp << " octets." << std::endl;
			}
			xmlFree(out);
			throw std::runtime_error("ConvertInput: conversion wasn't successful");
			out = 0;
		} else {
			out = (unsigned char *) xmlRealloc(out, out_size + 1);
			out[out_size] = 0;  /*null terminating out */
		}

	    return boost::shared_ptr< XML_Char >( new XML_Char( out ) );
	}
};  /* class XML_Convert */

boost::shared_ptr< XML_Convert > xmlConvert;

/*---------------------------------------------------------------------------*/
/* table: vectors
 * argv = { "icao", "seen", "speed", "heading", "vertical" }
 */

static int sql_callback_vector(void *userData, int argc, char **argv, char **azColName) {
	int i;
	int rc;

	boost::shared_ptr< XML_Char > xml_char;

	xml_char = xmlConvert->ConvertInput(argv[2]);
	/* Add element */
	rc = xmlTextWriterWriteElement(xml, BAD_CAST "speed", xml_char->get_xmlChar());
	if (rc < 0) {
		throw std::runtime_error("testXmlwriterFilename: Error at xmlTextWriterStartElement");
	}

	xml_char = xmlConvert->ConvertInput(argv[3]);
	/* Add element */
	rc = xmlTextWriterWriteElement(xml, BAD_CAST "course", xml_char->get_xmlChar());
	if (rc < 0) {
		throw std::runtime_error("testXmlwriterFilename: Error at xmlTextWriterStartElement");
	}

	return 0;
}

static void write_vector(const char* icao, const char* time) {
	char *zErrMsg = 0;
	int rc;

	/* Create SQL statement */
	std::string sql = "SELECT icao, seen, speed, heading, vertical FROM vectors WHERE icao = '";
	sql += icao;
	sql += "' ORDER BY ABS(julianday(seen) - julianday('";
	sql += time;
	sql += "')) LIMIT 1;";
	//std::cout << "write_vector sql = [" << sql << "]" << std::endl;

	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql.c_str(), sql_callback_vector, 0, &zErrMsg);
	if ( rc != SQLITE_OK ) {
		std::cerr << "SQL error: " << zErrMsg << std::endl;
		sqlite3_free(zErrMsg);
		exit(1);
	}
}

/*---------------------------------------------------------------------------*/
/* Table: positions
 * argv = { "icao", "seen", "alt", "lat", "lon" }
 */

static int sql_callback_position(void *userData, int argc, char **argv, char **azColName) {
	int i;
	int rc;
	boost::shared_ptr< XML_Char > xml_char;

	/* Debug */
	if (0) {
		std::cout << "Column name = ";
		for (int i = 0; i < argc; ++i) {
			std::cout << azColName[i] << ", ";
		}
		std::cout << std::endl;

		for (int i = 0; i < argc; ++i) {
			std::cout << argv[i] << ", ";
		}
		std::cout << std::endl;
	}

	/* Start an element */
	rc = xmlTextWriterStartElement(xml, BAD_CAST "trkpt");


	xml_char = xmlConvert->ConvertInput(argv[3]);
	/* Add an attribute  */
	rc = xmlTextWriterWriteAttribute(xml, BAD_CAST "lat", xml_char->get_xmlChar());
	if (rc < 0) {
		throw std::runtime_error("sql_callback_position lat: Error at xmlTextWriterWriteAttribute");
	}

	xml_char = xmlConvert->ConvertInput(argv[4]);
	/* Add an attribute  */
	rc = xmlTextWriterWriteAttribute(xml, BAD_CAST "lon", xml_char->get_xmlChar());
	if (rc < 0) {
		throw std::runtime_error("sql_callback_position lat: Error at xmlTextWriterWriteAttribute");
	}


	struct tm tm;

	if ( strptime( argv[1], "%F %T", &tm ) == NULL ) {
		std::cout << "Error converting time: " << argv[1] << std::endl;;
		exit(1);
	}

	char strDateTime[256];
	strftime(strDateTime, sizeof(strDateTime), "%FT%TZ", &tm );


	xml_char = xmlConvert->ConvertInput( strDateTime );
	/* Add element */
	rc = xmlTextWriterWriteElement(xml, BAD_CAST "time", xml_char->get_xmlChar());
	if (rc < 0) {
		throw std::runtime_error("testXmlwriterFilename: Error at xmlTextWriterStartElement");
	}


	xml_char = xmlConvert->ConvertInput(argv[2]);
	/* Add element */
	rc = xmlTextWriterWriteElement(xml, BAD_CAST "ele", xml_char->get_xmlChar());
	if (rc < 0) {
		throw std::runtime_error("testXmlwriterFilename: Error at xmlTextWriterStartElement");
	}


	write_vector(argv[0], argv[1]);

	/* Close the element */
	rc = xmlTextWriterEndElement(xml);
	if (rc < 0) {
		throw std::runtime_error("sql_callback_position: Error at xmlTextWriterEndElement");
	}

	return 0;
}

static void write_track(const char* icao, const char* name, const char* type) {
	char *zErrMsg = 0;
	int rc;

	/* Create SQL statement */
	std::string sql = ("SELECT icao, seen, alt, lat, lon FROM positions WHERE icao == ");
	sql.append(icao);

	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql.c_str(), sql_callback_position, 0, &zErrMsg);
	if ( rc != SQLITE_OK ) {
		std::cerr << "SQL error: " << zErrMsg << std::endl;
		sqlite3_free(zErrMsg);
		exit(1);
	}

}

/*---------------------------------------------------------------------------*/
/* argv = { "icao", "ident", "type" } */

static int sql_callback_ident(void *userData, int argc, char **argv, char **azColName) {
	int i;
	int rc;
	boost::shared_ptr< XML_Char > xml_char;

	/* Debug */
	if (0) {
		std::cout << "Column name = ";
		for (int i = 0; i < argc; ++i) {
			std::cout << azColName[i] << ", ";
		}
		std::cout << std::endl;

		for (int i = 0; i < argc; ++i) {
			std::cout << argv[i] << ", ";
		}
		std::cout << std::endl;
	}

	/* Start an element */
	rc = xmlTextWriterStartElement(xml, BAD_CAST "trk");
	if (rc < 0) {
		throw std::runtime_error("sql_callback_ident: Error at xmlTextWriterStartElement");
	}


	xml_char = xmlConvert->ConvertInput(argv[1]);
	/* Add element */
	rc = xmlTextWriterWriteElement(xml, BAD_CAST "name", xml_char->get_xmlChar());
	if (rc < 0) {
		throw std::runtime_error("sql_callback_ident: Error at xmlTextWriterStartElement");
	}

	if (1) {

		std::string s;
		s = "ICAO: ";
		s += argv[0];
		s += ", Type: ";
		s += argv[2];

		xml_char = xmlConvert->ConvertInput(s.c_str());
		/* Add element */
		rc = xmlTextWriterWriteElement(xml, BAD_CAST "desc", xml_char->get_xmlChar());
		if (rc < 0) {
			throw std::runtime_error("sql_callback_ident: Error at xmlTextWriterStartElement");
		}

	}


	/* Start an element */
	rc = xmlTextWriterStartElement(xml, BAD_CAST "trkseg");
	if (rc < 0) {
		throw std::runtime_error("sql_callback_ident: Error at xmlTextWriterStartElement");
	}

	write_track ( argv[0], argv[1], argv[2] );


	/* Close the element */
	rc = xmlTextWriterEndElement(xml);
	if (rc < 0) {
		throw std::runtime_error("sql_callback_ident: Error at xmlTextWriterEndElement");
	}


	/* Close the element */
	rc = xmlTextWriterEndElement(xml);
	if (rc < 0) {
		throw std::runtime_error("sql_callback_ident: Error at xmlTextWriterEndElement");
	}

	return 0;
}

static void sql_query_ident_table() {
	char *zErrMsg = 0;
	int rc;

	/* Create SQL statement */
	const char* sql = "SELECT icao, ident, type FROM ident";

	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, sql_callback_ident, 0, &zErrMsg);
	if ( rc != SQLITE_OK ) {
		std::cerr << "SQL error: " << zErrMsg << std::endl;
		sqlite3_free(zErrMsg);
		exit(1);
	}
}

static void xml_gpx_setup(void) {
	int rc;
	boost::shared_ptr< XML_Char > xml_char;

	/* Start the document with the xml default for the version,
	 * encoding ISO 8859-1 and the default for the standalone
	 * declaration. */
	rc = xmlTextWriterStartDocument(xml, NULL, GPX_ENCODING, "no");
	if ( rc < 0 ) {
		throw std::runtime_error("xml_gpx_setup: Error at xmlTextWriterStartDocument");
	}

	/* Start an element */
	rc = xmlTextWriterStartElement(xml, BAD_CAST "gpx");
	if (rc < 0) {
		throw std::runtime_error("xml_gpx_setup: Error at xmlTextWriterStartElement");
	}

	/* Add an attribute  */
	rc = xmlTextWriterWriteAttribute(xml, BAD_CAST "version", BAD_CAST "1.0");
	if (rc < 0) {
		throw std::runtime_error("xml_gpx_setup: Error at xmlTextWriterWriteAttribute");
	}

	/* Add an attribute  */
	rc = xmlTextWriterWriteAttribute(xml, BAD_CAST "creator", BAD_CAST "Viking -- http://viking.sf.net/");
	if (rc < 0) {
		throw std::runtime_error("xml_gpx_setup: Error at xmlTextWriterWriteAttribute");
	}

	/* Add an attribute  */
	rc = xmlTextWriterWriteAttribute(xml, BAD_CAST "xmlns:xsi", BAD_CAST "http://www.w3.org/2001/XMLSchema-instance");
	if (rc < 0) {
		throw std::runtime_error("xml_gpx_setup: Error at xmlTextWriterWriteAttribute");
	}

	/* Add an attribute  */
	rc = xmlTextWriterWriteAttribute(xml, BAD_CAST "xmlns", BAD_CAST "http://www.topografix.com/GPX/1/0");
	if (rc < 0) {
		throw std::runtime_error("xml_gpx_setup: Error at xmlTextWriterWriteAttribute");
	}

	/* Add an attribute  */
	rc = xmlTextWriterWriteAttribute(xml, BAD_CAST "xsi:schemaLocation", BAD_CAST "http://www.topografix.com/GPX/1/0 http://www.topografix.com/GPX/1/0/gpx.xsd");
	if (rc < 0) {
		throw std::runtime_error("xml_gpx_setup: Error at xmlTextWriterWriteAttribute");
	}

	/* Add meta data */
	if (1)
	{
		/* Start an element */
		rc = xmlTextWriterStartElement(xml, BAD_CAST "metadata");
		if (rc < 0) {
			throw std::runtime_error("xml_gpx_setup: Error at xmlTextWriterStartElement");
		}
		/* Add an attribute  */
		rc = xmlTextWriterWriteAttribute(xml, BAD_CAST "desc", BAD_CAST "ADS-B data converted from air_modes.db");
		if (rc < 0) {
			throw std::runtime_error("xml_gpx_setup: Error at xmlTextWriterWriteAttribute");
		}
		/* Close the element */
		rc = xmlTextWriterEndElement(xml);
		if (rc < 0) {
			throw std::runtime_error("xml_gpx_setup: Error at xmlTextWriterEndElement");
		}
	}

	/* Get list of idenitys from air mode data base */
	sql_query_ident_table();

	/* Close the element */
	rc = xmlTextWriterEndElement(xml);
	if (rc < 0) {
		throw std::runtime_error("xml_gpx_setup: Error at xmlTextWriterEndElement");
	}

	/* Close XML document. */
	rc = xmlTextWriterEndDocument(xml);
	if (rc < 0) {
		throw std::runtime_error("xml_gpx_setup: Error at xmlTextWriterEndDocument");
	}

	xmlFreeTextWriter(xml);
}


int main(int argc, char** argv) {
	int rc;
	std::string inFileName;
	std::string outFileName;

	tzset();

	namespace po = boost::program_options;

	// Declare the supported options.
	po::options_description desc(
		"Converts modes_rx sqlite database file into portable GPX file format\n\n"
		"Allowed options"
	);
	desc.add_options()
		("help,h", "produce help message")
		("input,i", po::value< std::string >(&inFileName)->default_value(db_file), "Input sqlite db generated by modes_rx")
		("output,o", po::value< std::string >(&outFileName)->default_value(gpx_file), "Output GPX XML file")
	;
	try {
		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);    

		if (vm.count("help")) {
			std::cout << desc << "\n";
			exit(1); 
		}


		rc = sqlite3_open(inFileName.c_str(), &db);
		if ( rc ) {
			std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
			exit(0);
		} 
	} 
    catch(po::error& e) 
    { 
      std::cerr << "ERROR: " << e.what() << std::endl << std::endl; 
      std::cerr << desc << std::endl; 
      return 1;
    } 

	xmlConvert.reset( new XML_Convert( GPX_ENCODING ) );

	/* Create a new XmlWriter for uri, with no compression. */
	xml = xmlNewTextWriterFilename(outFileName.c_str(), 0);
	if ( xml == NULL ) {
		std::cerr << "testXmlwriterFilename: Error creating the xml writer" << std::endl;
		return 1;
	}
	xmlTextWriterSetIndent(xml, 2);

	xml_gpx_setup();

	sqlite3_close(db);
	return 0;
}
